/* ESP-IDF's crypto behind plain functions, for mbedtls.rb.

   ESP-IDF v6 carries Mbed TLS 4, where PSA Crypto replaces the old
   primitives (mbedtls_sha256_*, mbedtls_aes_*, mbedtls_gcm_*). A PSA
   multipart operation cannot outlive the C call that starts it here: ESP's
   PSA drivers malloc their contexts, and Spinel's ffi pointers are not
   collected, nor is anything finalized. So no context here lives in memory
   of its own:

   - The streaming objects (Digest, HMAC, CMAC, Cipher) keep their context
     in an IO::Buffer the Ruby object owns, which the GC frees with it. That
     takes contexts that own no heap memory: SHA-1 and SHA-256 run on the
     SHA peripheral through ESP-IDF's sha/sha_core.h (the state is saved
     between calls, as ESP's own PSA driver does), AES and GCM on the AES
     peripheral through aes/esp_aes.h and aes/esp_aes_gcm.h, whose contexts
     are plain structs (GCM keeps the IV and AAD pointers only for the call
     that hands them over). HMAC and CMAC are built on those.
   - A key (PKey) is kept by Ruby as its PEM. Each call parses it, does its
     work and frees the context before returning; a generated key is made
     with PSA, copied out and destroyed in the same call.

   A call that produces bytes leaves them in one buffer, which
   espinel_tls_out() hands to Ruby (a :binstr return), and answers their
   count, or a negative error: Mbed TLS's or PSA's own, or one of TLS_ERR_*
   below (espinel_tls_error names either). */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "aes/esp_aes.h"
#include "aes/esp_aes_gcm.h"
#include "sha/sha_core.h"
#include "mbedtls/constant_time.h"
#include "mbedtls/error.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform_util.h"
#include "psa/crypto.h"

/* the byte count of a :binstr return (Spinel's sp_alloc.h) */
extern int sp_ffi_bin_len;

#define TLS_ERR_STATE (-0x10001)    /* called out of order */
#define TLS_ERR_LENGTH (-0x10002)   /* a key, IV or tag of the wrong size */
#define TLS_ERR_PADDING (-0x10003)  /* CBC: bad padding or a partial block */
#define TLS_ERR_KEY_TYPE (-0x10004) /* not the kind of key asked for */
#define TLS_ERR_CURVE (-0x10005)    /* an unknown curve name */
#define TLS_ERR_NO_MEM (-0x10006)
#define TLS_ERR_ALG (-0x10007)      /* an unsupported digest */

const char *espinel_tls_error(int err) {
  static char buf[128];
  switch (err) {
  case TLS_ERR_STATE: return "called out of order";
  case TLS_ERR_LENGTH: return "invalid length";
  case TLS_ERR_PADDING: return "bad decrypt";
  case TLS_ERR_KEY_TYPE: return "key type mismatch";
  case TLS_ERR_CURVE: return "invalid curve name";
  case TLS_ERR_NO_MEM: return "out of memory";
  case TLS_ERR_ALG: return "unsupported digest";
  }
  mbedtls_strerror(err, buf, sizeof buf);
  return buf;
}

/* ---- the result buffer ---- */

static unsigned char *out;
static size_t out_cap, out_len;

static unsigned char *out_reserve(size_t n) {
  if (n > out_cap) {
    unsigned char *p = realloc(out, n);
    if (!p) return NULL;
    out = p;
    out_cap = n;
  }
  return out;
}

static int out_set(size_t n) {
  out_len = n;
  return (int)n;
}

static int out_copy(const void *p, size_t n) {
  if (!out_reserve(n ? n : 1)) return TLS_ERR_NO_MEM;
  memcpy(out, p, n);
  return out_set(n);
}

const char *espinel_tls_out(void) {
  sp_ffi_bin_len = (int)out_len;
  return out ? (const char *)out : "";
}

/* ---- SHA-1 and SHA-256 on the SHA peripheral ----

   The peripheral is shared, so each call that has whole blocks takes it,
   loads the state saved by the call before (none for the first block),
   runs the blocks and saves the state again. The state, read out, is the
   digest's bytes. */

typedef struct {
  esp_sha_type type;
  bool started;          /* a block has been run */
  uint64_t total;        /* bytes taken */
  uint32_t state[8];
  unsigned char buf[64]; /* input short of a block */
} sha_t;

static size_t sha_len(const sha_t *s) { return s->type == SHA1 ? 20 : 32; }

static void sha_start(sha_t *s, esp_sha_type type) {
  memset(s, 0, sizeof *s);
  s->type = type;
}

static void sha_blocks(sha_t *s, const unsigned char *in, size_t blocks) {
  esp_sha_acquire_hardware();
  esp_sha_set_mode(s->type);
  if (s->started) esp_sha_write_digest_state(s->type, s->state);
  for (size_t i = 0; i < blocks; i++) {
    esp_sha_block(s->type, in + i * 64, !s->started);
    s->started = true;
  }
  esp_sha_read_digest_state(s->type, s->state);
  esp_sha_release_hardware();
}

static void sha_update(sha_t *s, const unsigned char *in, size_t n) {
  size_t left = (size_t)(s->total & 63);
  s->total += n;
  if (left) {
    size_t take = 64 - left < n ? 64 - left : n;
    memcpy(s->buf + left, in, take);
    in += take;
    n -= take;
    if (left + take < 64) return;
    sha_blocks(s, s->buf, 1);
  }
  if (n >= 64) sha_blocks(s, in, n / 64);
  memcpy(s->buf, in + n / 64 * 64, n % 64);
}

/* the digest into dst; s is spent */
static void sha_finish(sha_t *s, unsigned char *dst) {
  unsigned char pad[72] = { 0x80 };
  uint64_t bits = s->total * 8;
  size_t left = (size_t)(s->total & 63);
  size_t padn = left < 56 ? 56 - left : 120 - left;
  for (int i = 0; i < 8; i++) pad[padn + i] = (unsigned char)(bits >> (56 - 8 * i));
  sha_update(s, pad, padn + 8);
  memcpy(dst, s->state, sha_len(s));
  mbedtls_platform_zeroize(s->buf, sizeof s->buf);
}

static int sha_type_of(int alg, esp_sha_type *type) {
  if (alg == MBEDTLS_MD_SHA1) { *type = SHA1; return 0; }
  if (alg == MBEDTLS_MD_SHA256) { *type = SHA2_256; return 0; }
  return TLS_ERR_ALG;
}

/* ---- digests ---- */

int espinel_tls_md_sha1(void) { return MBEDTLS_MD_SHA1; }
int espinel_tls_md_sha256(void) { return MBEDTLS_MD_SHA256; }

typedef struct {
  sha_t sha;
  bool finished;
} digest_t;

int espinel_tls_digest_size(void) { return sizeof(digest_t); }

int espinel_tls_digest_start(digest_t *d, int alg) {
  esp_sha_type type;
  int err = sha_type_of(alg, &type);
  if (err) return err;
  memset(d, 0, sizeof *d);
  sha_start(&d->sha, type);
  return 0;
}

int espinel_tls_digest_update(digest_t *d, const unsigned char *in, size_t n) {
  if (d->finished) return TLS_ERR_STATE;
  sha_update(&d->sha, in, n);
  return 0;
}

int espinel_tls_digest_finish(digest_t *d) {
  if (d->finished) return TLS_ERR_STATE;
  if (!out_reserve(32)) return TLS_ERR_NO_MEM;
  d->finished = true;
  sha_finish(&d->sha, out);
  return out_set(sha_len(&d->sha));
}

/* the hash of data, for a signature */
static int hash_of(int alg, const unsigned char *in, size_t n, unsigned char *hash, size_t *len) {
  esp_sha_type type;
  int err = sha_type_of(alg, &type);
  if (err) return err;
  sha_t s;
  sha_start(&s, type);
  sha_update(&s, in, n);
  sha_finish(&s, hash);
  *len = sha_len(&s);
  return 0;
}

/* ---- HMAC-SHA256 (RFC 2104) ---- */

typedef struct {
  sha_t inner;
  unsigned char k0[64];
  bool finished;
} hmac_t;

int espinel_tls_hmac_size(void) { return sizeof(hmac_t); }

static void xor_pad(unsigned char *dst, const unsigned char *k0, unsigned char pad) {
  for (int i = 0; i < 64; i++) dst[i] = k0[i] ^ pad;
}

int espinel_tls_hmac_reset(hmac_t *h) {
  unsigned char ipad[64];
  xor_pad(ipad, h->k0, 0x36);
  h->finished = false;
  sha_start(&h->inner, SHA2_256);
  sha_update(&h->inner, ipad, 64);
  mbedtls_platform_zeroize(ipad, sizeof ipad);
  return 0;
}

int espinel_tls_hmac_start(hmac_t *h, const unsigned char *key, size_t n) {
  memset(h, 0, sizeof *h);
  if (n > 64) {
    size_t len;
    hash_of(MBEDTLS_MD_SHA256, key, n, h->k0, &len);
  } else {
    memcpy(h->k0, key, n);
  }
  return espinel_tls_hmac_reset(h);
}

int espinel_tls_hmac_update(hmac_t *h, const unsigned char *in, size_t n) {
  if (h->finished) return TLS_ERR_STATE;
  sha_update(&h->inner, in, n);
  return 0;
}

int espinel_tls_hmac_finish(hmac_t *h) {
  unsigned char ih[32], opad[64];
  sha_t outer;
  if (h->finished) return TLS_ERR_STATE;
  if (!out_reserve(32)) return TLS_ERR_NO_MEM;
  h->finished = true;
  sha_finish(&h->inner, ih);
  xor_pad(opad, h->k0, 0x5c);
  sha_start(&outer, SHA2_256);
  sha_update(&outer, opad, 64);
  sha_update(&outer, ih, 32);
  sha_finish(&outer, out);
  mbedtls_platform_zeroize(opad, sizeof opad);
  mbedtls_platform_zeroize(ih, sizeof ih);
  return out_set(32);
}

/* ---- AES-CMAC (RFC 4493) ---- */

typedef struct {
  esp_aes_context aes;
  unsigned char k1[16], k2[16];
  unsigned char x[16];   /* the chain so far */
  unsigned char m[16];   /* the last block, held back for the subkey */
  size_t m_len;
} cmac_t;

int espinel_tls_cmac_size(void) { return sizeof(cmac_t); }

static void dbl(unsigned char *dst, const unsigned char *src) {
  unsigned char carry = 0;
  for (int i = 15; i >= 0; i--) {
    unsigned char b = src[i];
    dst[i] = (unsigned char)(b << 1) | carry;
    carry = b >> 7;
  }
  if (src[0] & 0x80) dst[15] ^= 0x87;
}

int espinel_tls_cmac_reset(cmac_t *c) {
  memset(c->x, 0, 16);
  c->m_len = 0;
  return 0;
}

int espinel_tls_cmac_start(cmac_t *c, const unsigned char *key, size_t n) {
  unsigned char l[16] = { 0 };
  if (n != 16) return TLS_ERR_LENGTH;
  memset(c, 0, sizeof *c);
  esp_aes_init(&c->aes);
  int err = esp_aes_setkey(&c->aes, key, 128);
  if (!err) err = esp_aes_crypt_ecb(&c->aes, ESP_AES_ENCRYPT, l, l);
  if (err) return err;
  dbl(c->k1, l);
  dbl(c->k2, c->k1);
  mbedtls_platform_zeroize(l, sizeof l);
  return espinel_tls_cmac_reset(c);
}

static int cmac_chain(cmac_t *c, const unsigned char *block) {
  for (int i = 0; i < 16; i++) c->x[i] ^= block[i];
  return esp_aes_crypt_ecb(&c->aes, ESP_AES_ENCRYPT, c->x, c->x);
}

int espinel_tls_cmac_update(cmac_t *c, const unsigned char *in, size_t n) {
  while (n > 0) {
    if (c->m_len == 16) {
      int err = cmac_chain(c, c->m);
      if (err) return err;
      c->m_len = 0;
    }
    size_t take = 16 - c->m_len < n ? 16 - c->m_len : n;
    memcpy(c->m + c->m_len, in, take);
    c->m_len += take;
    in += take;
    n -= take;
  }
  return 0;
}

/* The MAC; the state starts over, as Mbed TLS's cmac_finish left it. */
int espinel_tls_cmac_finish(cmac_t *c) {
  unsigned char last[16];
  if (!out_reserve(16)) return TLS_ERR_NO_MEM;
  if (c->m_len == 16) {
    for (int i = 0; i < 16; i++) last[i] = c->m[i] ^ c->k1[i];
  } else {
    memset(last, 0, 16);
    memcpy(last, c->m, c->m_len);
    last[c->m_len] = 0x80;
    for (int i = 0; i < 16; i++) last[i] ^= c->k2[i];
  }
  int err = cmac_chain(c, last);
  if (!err) memcpy(out, c->x, 16);
  espinel_tls_cmac_reset(c);
  return err ? err : out_set(16);
}

/* ---- AES-CBC (PKCS#7 padding) and AES-GCM ---- */

#define MODE_CBC 1
#define MODE_GCM 2

int espinel_tls_mode_cbc(void) { return MODE_CBC; }
int espinel_tls_mode_gcm(void) { return MODE_GCM; }

typedef struct {
  int mode;
  int op;            /* ESP_AES_ENCRYPT, _DECRYPT, or -1 before either */
  size_t key_len, iv_len;
  bool key_set, iv_set, started, finished;
  unsigned char iv[16];
  unsigned char buf[16];  /* CBC: input short of a block, or the block held back */
  size_t buf_len;
  unsigned char tag[16];  /* GCM: after finish */
  union {
    esp_aes_context aes;
    esp_gcm_context gcm;
  } u;
} cipher_t;

int espinel_tls_cipher_size(void) { return sizeof(cipher_t); }

int espinel_tls_cipher_start(cipher_t *c, int mode, int key_len) {
  memset(c, 0, sizeof *c);
  c->mode = mode;
  c->op = -1;
  c->key_len = key_len;
  if (mode == MODE_CBC) {
    c->iv_len = 16;
    esp_aes_init(&c->u.aes);
  } else {
    c->iv_len = 12;
    esp_aes_gcm_init(&c->u.gcm);
  }
  return 0;
}

int espinel_tls_cipher_set_op(cipher_t *c, int encrypt) {
  if (c->key_set) return TLS_ERR_STATE;
  c->op = encrypt ? ESP_AES_ENCRYPT : ESP_AES_DECRYPT;
  return 0;
}

int espinel_tls_cipher_set_key(cipher_t *c, const unsigned char *key, size_t n) {
  if (c->op < 0 || c->key_set) return TLS_ERR_STATE;
  if (n != c->key_len) return TLS_ERR_LENGTH;
  /* esp_aes_gcm_setkey's cipher argument is not read: it is AES only */
  int err = c->mode == MODE_CBC ? esp_aes_setkey(&c->u.aes, key, n * 8)
                                : esp_aes_gcm_setkey(&c->u.gcm, 0, key, n * 8);
  if (!err) c->key_set = true;
  return err;
}

int espinel_tls_cipher_set_iv(cipher_t *c, const unsigned char *iv, size_t n) {
  if (c->op < 0 || c->iv_set) return TLS_ERR_STATE;
  if (n != c->iv_len) return TLS_ERR_LENGTH;
  memcpy(c->iv, iv, n);
  c->iv_set = true;
  return 0;
}

/* GCM starts at the first data, so the key and IV may come in either order */
static int gcm_ready(cipher_t *c) {
  if (!c->key_set || !c->iv_set || c->finished) return TLS_ERR_STATE;
  if (c->started) return 0;
  int err = esp_aes_gcm_starts(&c->u.gcm, c->op, c->iv, c->iv_len);
  if (!err) c->started = true;
  return err;
}

int espinel_tls_cipher_update_ad(cipher_t *c, const unsigned char *ad, size_t n) {
  if (c->mode != MODE_GCM) return TLS_ERR_STATE;
  int err = gcm_ready(c);
  return err ? err : esp_aes_gcm_update_ad(&c->u.gcm, ad, n);
}

static int cbc_block(cipher_t *c, const unsigned char *in, unsigned char *dst) {
  return esp_aes_crypt_cbc(&c->u.aes, c->op, 16, c->iv, in, dst);
}

int espinel_tls_cipher_update(cipher_t *c, const unsigned char *in, size_t n) {
  if (!out_reserve(n + 16)) return TLS_ERR_NO_MEM;
  if (c->mode == MODE_GCM) {
    size_t olen = 0;
    int err = gcm_ready(c);
    if (!err) err = esp_aes_gcm_update(&c->u.gcm, in, n, out, n + 16, &olen);
    return err ? err : out_set(olen);
  }
  if (!c->key_set || !c->iv_set || c->finished) return TLS_ERR_STATE;
  size_t produced = 0;
  while (n > 0) {
    /* a full block goes out once more input follows it: decryption keeps
       the last one for finish, which strips its padding */
    if (c->buf_len == 16) {
      int err = cbc_block(c, c->buf, out + produced);
      if (err) return err;
      produced += 16;
      c->buf_len = 0;
    }
    size_t take = 16 - c->buf_len < n ? 16 - c->buf_len : n;
    memcpy(c->buf + c->buf_len, in, take);
    c->buf_len += take;
    in += take;
    n -= take;
  }
  if (c->buf_len == 16 && c->op == ESP_AES_ENCRYPT) {
    int err = cbc_block(c, c->buf, out + produced);
    if (err) return err;
    produced += 16;
    c->buf_len = 0;
  }
  return out_set(produced);
}

int espinel_tls_cipher_finish(cipher_t *c) {
  if (!out_reserve(16)) return TLS_ERR_NO_MEM;
  if (c->mode == MODE_GCM) {
    size_t olen = 0;
    int err = gcm_ready(c);
    if (!err) err = esp_aes_gcm_finish(&c->u.gcm, out, 16, &olen, c->tag, 16);
    if (err) return err;
    c->finished = true;
    return out_set(olen);
  }
  if (!c->key_set || !c->iv_set || c->finished) return TLS_ERR_STATE;
  c->finished = true;
  if (c->op == ESP_AES_ENCRYPT) {
    unsigned char pad = (unsigned char)(16 - c->buf_len);
    memset(c->buf + c->buf_len, pad, pad);
    int err = cbc_block(c, c->buf, out);
    return err ? err : out_set(16);
  }
  if (c->buf_len != 16) return TLS_ERR_PADDING;
  int err = cbc_block(c, c->buf, out);
  if (err) return err;
  unsigned char pad = out[15];
  if (pad == 0 || pad > 16) return TLS_ERR_PADDING;
  for (int i = 16 - pad; i < 16; i++)
    if (out[i] != pad) return TLS_ERR_PADDING;
  return out_set(16 - pad);
}

/* GCM's tag after finish; nothing for CBC */
int espinel_tls_cipher_tag(cipher_t *c) {
  if (c->mode != MODE_GCM) return out_set(0);
  if (!c->finished) return TLS_ERR_STATE;
  return out_copy(c->tag, 16);
}

/* 1 when the tag matches the one computed at finish, 0 when not */
int espinel_tls_cipher_check_tag(cipher_t *c, const unsigned char *tag, size_t n) {
  if (c->mode != MODE_GCM) return 1;
  if (!c->finished) return TLS_ERR_STATE;
  if (n < 4 || n > 16) return TLS_ERR_LENGTH;
  return mbedtls_ct_memcmp(c->tag, tag, n) == 0;
}

/* ---- public keys: RSA and EC ---- */

#define PK_RSA 1
#define PK_EC 2

int espinel_tls_pk_rsa(void) { return PK_RSA; }
int espinel_tls_pk_ec(void) { return PK_EC; }

/* what the last parse found */
static int pk_last_type, pk_last_private;
int espinel_tls_pk_type(void) { return pk_last_type; }
int espinel_tls_pk_private(void) { return pk_last_private; }

/* the key in pem (PEM or DER); private tells which kind it is */
static int pk_load(mbedtls_pk_context *pk, const char *pem, size_t n, bool *private) {
  /* Mbed TLS takes PEM with its terminating NUL counted */
  unsigned char *buf = malloc(n + 1);
  if (!buf) return TLS_ERR_NO_MEM;
  memcpy(buf, pem, n);
  buf[n] = '\0';
  size_t len = strstr((const char *)buf, "-----BEGIN") ? n + 1 : n;
  int err = mbedtls_pk_parse_public_key(pk, buf, len);
  *private = false;
  if (err) {
    mbedtls_pk_free(pk);
    mbedtls_pk_init(pk);
    err = mbedtls_pk_parse_key(pk, buf, len, NULL, 0);
    *private = err == 0;
  }
  mbedtls_platform_zeroize(buf, n + 1);
  free(buf);
  return err;
}

static int pk_type(mbedtls_pk_context *pk) {
  psa_key_type_t t = mbedtls_pk_get_key_type(pk);
  if (PSA_KEY_TYPE_IS_RSA(t)) return PK_RSA;
  if (PSA_KEY_TYPE_IS_ECC(t)) return PK_EC;
  return 0;
}

/* the key as PEM: the private key when it has one */
static int pk_pem(mbedtls_pk_context *pk, bool private) {
  if (!out_reserve(8192)) return TLS_ERR_NO_MEM;
  int err = private ? mbedtls_pk_write_key_pem(pk, out, out_cap)
                    : mbedtls_pk_write_pubkey_pem(pk, out, out_cap);
  return err ? err : out_set(strlen((const char *)out));
}

/* parses a key, answering its PEM (espinel_tls_pk_type and _private tell
   the rest); want is the type asked for, or 0 for either */
int espinel_tls_pk_parse(const char *pem, size_t n, int want) {
  mbedtls_pk_context pk;
  bool private;
  mbedtls_pk_init(&pk);
  int err = pk_load(&pk, pem, n, &private);
  if (!err && want && pk_type(&pk) != want) err = TLS_ERR_KEY_TYPE;
  if (!err) {
    pk_last_type = pk_type(&pk);
    pk_last_private = private;
    err = pk_pem(&pk, private);
  }
  mbedtls_pk_free(&pk);
  return err;
}

int espinel_tls_pk_public(const char *pem, size_t n) {
  mbedtls_pk_context pk;
  bool private;
  mbedtls_pk_init(&pk);
  int err = pk_load(&pk, pem, n, &private);
  if (!err) {
    pk_last_type = pk_type(&pk);
    pk_last_private = 0;
    err = pk_pem(&pk, false);
  }
  mbedtls_pk_free(&pk);
  return err;
}

/* a key PSA generates with attr, answered as PEM; the PSA key is gone after */
static int pk_generate(psa_key_attributes_t *attr, const uint8_t *exponent, size_t exponent_len,
                       int type) {
  mbedtls_svc_key_id_t id;
  psa_set_key_usage_flags(attr, PSA_KEY_USAGE_EXPORT);
  psa_status_t st;
  if (exponent_len) {
    psa_custom_key_parameters_t custom = { 0 };
    st = psa_generate_key_custom(attr, &custom, exponent, exponent_len, &id);
  } else {
    st = psa_generate_key(attr, &id);
  }
  psa_reset_key_attributes(attr);
  if (st != PSA_SUCCESS) return st;
  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  int err = mbedtls_pk_copy_from_psa(id, &pk);
  psa_destroy_key(id);
  if (!err) {
    pk_last_type = type;
    pk_last_private = 1;
    err = pk_pem(&pk, true);
  }
  mbedtls_pk_free(&pk);
  return err;
}

int espinel_tls_pk_generate_rsa(int bits, int exponent) {
  psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
  uint8_t e[4];
  size_t e_len = 0;
  if (exponent != 65537) {
    /* the public exponent, big-endian with no leading zeros */
    for (int shift = 24; shift >= 0; shift -= 8)
      if (e_len || (exponent >> shift) & 0xff) e[e_len++] = (uint8_t)(exponent >> shift);
    if (!e_len) return TLS_ERR_LENGTH;
  }
  psa_set_key_type(&attr, PSA_KEY_TYPE_RSA_KEY_PAIR);
  psa_set_key_bits(&attr, bits);
  return pk_generate(&attr, e, e_len, PK_RSA);
}

/* the curves Mbed TLS named, as PSA's family and size */
static const struct {
  const char *name;
  psa_ecc_family_t family;
  size_t bits;
} curves[] = {
  { "secp256r1", PSA_ECC_FAMILY_SECP_R1, 256 },
  { "secp384r1", PSA_ECC_FAMILY_SECP_R1, 384 },
  { "secp521r1", PSA_ECC_FAMILY_SECP_R1, 521 },
  { "secp256k1", PSA_ECC_FAMILY_SECP_K1, 256 },
  { "brainpoolP256r1", PSA_ECC_FAMILY_BRAINPOOL_P_R1, 256 },
  { "brainpoolP384r1", PSA_ECC_FAMILY_BRAINPOOL_P_R1, 384 },
  { "brainpoolP512r1", PSA_ECC_FAMILY_BRAINPOOL_P_R1, 512 },
};

int espinel_tls_pk_generate_ec(const char *curve) {
  for (size_t i = 0; i < sizeof curves / sizeof curves[0]; i++) {
    if (strcmp(curve, curves[i].name) != 0) continue;
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(curves[i].family));
    psa_set_key_bits(&attr, curves[i].bits);
    return pk_generate(&attr, NULL, 0, PK_EC);
  }
  return TLS_ERR_CURVE;
}

/* the signature of data's alg hash */
int espinel_tls_pk_sign(const char *pem, size_t n, int alg, const unsigned char *data, size_t dn) {
  unsigned char hash[32];
  size_t hash_len, sig_len = 0;
  mbedtls_pk_context pk;
  bool private;
  if (!out_reserve(MBEDTLS_PK_SIGNATURE_MAX_SIZE)) return TLS_ERR_NO_MEM;
  int err = hash_of(alg, data, dn, hash, &hash_len);
  if (err) return err;
  mbedtls_pk_init(&pk);
  err = pk_load(&pk, pem, n, &private);
  if (!err && !private) err = TLS_ERR_KEY_TYPE;
  if (!err) err = mbedtls_pk_sign(&pk, (mbedtls_md_type_t)alg, hash, hash_len, out,
                                  MBEDTLS_PK_SIGNATURE_MAX_SIZE, &sig_len);
  mbedtls_pk_free(&pk);
  return err ? err : out_set(sig_len);
}

/* 1 when sig signs data's alg hash, 0 when it does not */
int espinel_tls_pk_verify(const char *pem, size_t n, int alg, const unsigned char *sig, size_t sn,
                          const unsigned char *data, size_t dn) {
  unsigned char hash[32];
  size_t hash_len;
  mbedtls_pk_context pk;
  bool private;
  int err = hash_of(alg, data, dn, hash, &hash_len);
  if (err) return err;
  mbedtls_pk_init(&pk);
  err = pk_load(&pk, pem, n, &private);
  if (!err) err = mbedtls_pk_verify(&pk, (mbedtls_md_type_t)alg, hash, hash_len, sig, sn) == 0 ? 1 : 0;
  mbedtls_pk_free(&pk);
  return err;
}
