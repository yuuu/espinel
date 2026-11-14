/* ESP-IDF's Mbed TLS behind plain functions, for mbedtls.rb.

   Spinel's ffi pointers are not collected, and nothing finalizes them, so
   no context here lives in memory of its own:

   - The streaming objects (Digest, HMAC, CMAC, Cipher) keep their context
     in an IO::Buffer the Ruby object owns, which the GC frees with it. That
     takes contexts that own no heap memory: with ESP-IDF's hardware SHA
     and AES (CONFIG_MBEDTLS_HARDWARE_SHA, _AES, on by default) the SHA-1,
     SHA-256, AES and GCM contexts are plain structs (GCM keeps the IV and
     AAD pointers only for the call that hands them over). HMAC and CMAC
     are built on those rather than on mbedtls_md / mbedtls_cipher, which
     allocate.
   - A key (PKey) is kept by Ruby as its PEM. Each call parses it, does its
     work and frees the context before returning.

   A call that produces bytes leaves them in one buffer, which
   espinel_tls_out() hands to Ruby (a :binstr return), and answers their
   count, or a negative error: Mbed TLS's own, or one of TLS_ERR_* below
   (espinel_tls_error names either). */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "esp_random.h"
#include "mbedtls/aes.h"
#include "mbedtls/constant_time.h"
#include "mbedtls/ecp.h"
#include "mbedtls/error.h"
#include "mbedtls/gcm.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/rsa.h"
#include "mbedtls/sha1.h"
#include "mbedtls/sha256.h"

/* the byte count of a :binstr return (Spinel's sp_alloc.h) */
extern int sp_ffi_bin_len;

#define TLS_ERR_STATE (-0x10001)    /* called out of order */
#define TLS_ERR_LENGTH (-0x10002)   /* a key, IV or tag of the wrong size */
#define TLS_ERR_PADDING (-0x10003)  /* CBC: bad padding or a partial block */
#define TLS_ERR_KEY_TYPE (-0x10004) /* not the kind of key asked for */
#define TLS_ERR_CURVE (-0x10005)    /* an unknown curve name */
#define TLS_ERR_NO_MEM (-0x10006)

const char *espinel_tls_error(int err) {
  static char buf[128];
  switch (err) {
  case TLS_ERR_STATE: return "called out of order";
  case TLS_ERR_LENGTH: return "invalid length";
  case TLS_ERR_PADDING: return "bad decrypt";
  case TLS_ERR_KEY_TYPE: return "key type mismatch";
  case TLS_ERR_CURVE: return "invalid curve name";
  case TLS_ERR_NO_MEM: return "out of memory";
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

static int rng(void *p, unsigned char *buf, size_t n) {
  (void)p;
  esp_fill_random(buf, n);
  return 0;
}

/* ---- digests: SHA-1, SHA-256 ---- */

int espinel_tls_md_sha1(void) { return MBEDTLS_MD_SHA1; }
int espinel_tls_md_sha256(void) { return MBEDTLS_MD_SHA256; }

typedef struct {
  int alg;
  bool finished;
  union {
    mbedtls_sha1_context sha1;
    mbedtls_sha256_context sha256;
  } u;
} digest_t;

int espinel_tls_digest_size(void) { return sizeof(digest_t); }

int espinel_tls_digest_start(digest_t *d, int alg) {
  memset(d, 0, sizeof *d);
  d->alg = alg;
  if (alg == MBEDTLS_MD_SHA1) {
    mbedtls_sha1_init(&d->u.sha1);
    return mbedtls_sha1_starts(&d->u.sha1);
  }
  if (alg == MBEDTLS_MD_SHA256) {
    mbedtls_sha256_init(&d->u.sha256);
    return mbedtls_sha256_starts(&d->u.sha256, 0);
  }
  return MBEDTLS_ERR_MD_FEATURE_UNAVAILABLE;
}

int espinel_tls_digest_update(digest_t *d, const unsigned char *in, size_t n) {
  if (d->finished) return TLS_ERR_STATE;
  return d->alg == MBEDTLS_MD_SHA1 ? mbedtls_sha1_update(&d->u.sha1, in, n)
                                   : mbedtls_sha256_update(&d->u.sha256, in, n);
}

int espinel_tls_digest_finish(digest_t *d) {
  if (d->finished) return TLS_ERR_STATE;
  if (!out_reserve(32)) return TLS_ERR_NO_MEM;
  d->finished = true;
  if (d->alg == MBEDTLS_MD_SHA1) {
    int err = mbedtls_sha1_finish(&d->u.sha1, out);
    return err ? err : out_set(20);
  }
  int err = mbedtls_sha256_finish(&d->u.sha256, out);
  return err ? err : out_set(32);
}

/* the hash of data, for a signature */
static int hash_of(int alg, const unsigned char *in, size_t n, unsigned char *hash, size_t *len) {
  if (alg == MBEDTLS_MD_SHA1) { *len = 20; return mbedtls_sha1(in, n, hash); }
  if (alg == MBEDTLS_MD_SHA256) { *len = 32; return mbedtls_sha256(in, n, hash, 0); }
  return MBEDTLS_ERR_MD_FEATURE_UNAVAILABLE;
}

/* ---- HMAC-SHA256 (RFC 2104) ---- */

typedef struct {
  mbedtls_sha256_context inner;
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
  mbedtls_sha256_init(&h->inner);
  int err = mbedtls_sha256_starts(&h->inner, 0);
  if (!err) err = mbedtls_sha256_update(&h->inner, ipad, 64);
  mbedtls_platform_zeroize(ipad, sizeof ipad);
  return err;
}

int espinel_tls_hmac_start(hmac_t *h, const unsigned char *key, size_t n) {
  memset(h, 0, sizeof *h);
  if (n > 64) {
    int err = mbedtls_sha256(key, n, h->k0, 0);
    if (err) return err;
  } else {
    memcpy(h->k0, key, n);
  }
  return espinel_tls_hmac_reset(h);
}

int espinel_tls_hmac_update(hmac_t *h, const unsigned char *in, size_t n) {
  if (h->finished) return TLS_ERR_STATE;
  return mbedtls_sha256_update(&h->inner, in, n);
}

int espinel_tls_hmac_finish(hmac_t *h) {
  unsigned char ih[32], opad[64];
  mbedtls_sha256_context outer;
  if (h->finished) return TLS_ERR_STATE;
  if (!out_reserve(32)) return TLS_ERR_NO_MEM;
  h->finished = true;
  int err = mbedtls_sha256_finish(&h->inner, ih);
  xor_pad(opad, h->k0, 0x5c);
  mbedtls_sha256_init(&outer);
  if (!err) err = mbedtls_sha256_starts(&outer, 0);
  if (!err) err = mbedtls_sha256_update(&outer, opad, 64);
  if (!err) err = mbedtls_sha256_update(&outer, ih, 32);
  if (!err) err = mbedtls_sha256_finish(&outer, out);
  mbedtls_sha256_free(&outer);
  mbedtls_platform_zeroize(opad, sizeof opad);
  mbedtls_platform_zeroize(ih, sizeof ih);
  return err ? err : out_set(32);
}

/* ---- AES-CMAC (RFC 4493) ---- */

typedef struct {
  mbedtls_aes_context aes;
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
  mbedtls_aes_init(&c->aes);
  int err = mbedtls_aes_setkey_enc(&c->aes, key, 128);
  if (!err) err = mbedtls_aes_crypt_ecb(&c->aes, MBEDTLS_AES_ENCRYPT, l, l);
  if (err) return err;
  dbl(c->k1, l);
  dbl(c->k2, c->k1);
  mbedtls_platform_zeroize(l, sizeof l);
  return espinel_tls_cmac_reset(c);
}

static int cmac_chain(cmac_t *c, const unsigned char *block) {
  for (int i = 0; i < 16; i++) c->x[i] ^= block[i];
  return mbedtls_aes_crypt_ecb(&c->aes, MBEDTLS_AES_ENCRYPT, c->x, c->x);
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

/* The MAC; the state starts over, as Mbed TLS's cmac_finish leaves it. */
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

int espinel_tls_mode_cbc(void) { return MBEDTLS_MODE_CBC; }
int espinel_tls_mode_gcm(void) { return MBEDTLS_MODE_GCM; }

typedef struct {
  int mode;
  int op;            /* MBEDTLS_ENCRYPT, _DECRYPT, or -1 before either */
  size_t key_len, iv_len;
  bool key_set, iv_set, started, finished;
  unsigned char iv[16];
  unsigned char buf[16];  /* CBC: input short of a block, or the block held back */
  size_t buf_len;
  unsigned char tag[16];  /* GCM: after finish */
  union {
    mbedtls_aes_context aes;
    mbedtls_gcm_context gcm;
  } u;
} cipher_t;

int espinel_tls_cipher_size(void) { return sizeof(cipher_t); }

int espinel_tls_cipher_start(cipher_t *c, int mode, int key_len) {
  memset(c, 0, sizeof *c);
  c->mode = mode;
  c->op = -1;
  c->key_len = key_len;
  if (mode == MBEDTLS_MODE_CBC) {
    c->iv_len = 16;
    mbedtls_aes_init(&c->u.aes);
  } else {
    c->iv_len = 12;
    mbedtls_gcm_init(&c->u.gcm);
  }
  return 0;
}

int espinel_tls_cipher_set_op(cipher_t *c, int encrypt) {
  if (c->key_set) return TLS_ERR_STATE;
  c->op = encrypt ? MBEDTLS_ENCRYPT : MBEDTLS_DECRYPT;
  return 0;
}

int espinel_tls_cipher_set_key(cipher_t *c, const unsigned char *key, size_t n) {
  if (c->op < 0 || c->key_set) return TLS_ERR_STATE;
  if (n != c->key_len) return TLS_ERR_LENGTH;
  int err;
  if (c->mode == MBEDTLS_MODE_CBC)
    err = c->op == MBEDTLS_ENCRYPT ? mbedtls_aes_setkey_enc(&c->u.aes, key, n * 8)
                                   : mbedtls_aes_setkey_dec(&c->u.aes, key, n * 8);
  else
    err = mbedtls_gcm_setkey(&c->u.gcm, MBEDTLS_CIPHER_ID_AES, key, n * 8);
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
  int err = mbedtls_gcm_starts(&c->u.gcm, c->op, c->iv, c->iv_len);
  if (!err) c->started = true;
  return err;
}

int espinel_tls_cipher_update_ad(cipher_t *c, const unsigned char *ad, size_t n) {
  if (c->mode != MBEDTLS_MODE_GCM) return TLS_ERR_STATE;
  int err = gcm_ready(c);
  return err ? err : mbedtls_gcm_update_ad(&c->u.gcm, ad, n);
}

static int cbc_block(cipher_t *c, const unsigned char *in, unsigned char *dst) {
  return mbedtls_aes_crypt_cbc(&c->u.aes, c->op, 16, c->iv, in, dst);
}

int espinel_tls_cipher_update(cipher_t *c, const unsigned char *in, size_t n) {
  if (!out_reserve(n + 16)) return TLS_ERR_NO_MEM;
  if (c->mode == MBEDTLS_MODE_GCM) {
    size_t olen = 0;
    int err = gcm_ready(c);
    if (!err) err = mbedtls_gcm_update(&c->u.gcm, in, n, out, n + 16, &olen);
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
  if (c->buf_len == 16 && c->op == MBEDTLS_ENCRYPT) {
    int err = cbc_block(c, c->buf, out + produced);
    if (err) return err;
    produced += 16;
    c->buf_len = 0;
  }
  return out_set(produced);
}

int espinel_tls_cipher_finish(cipher_t *c) {
  if (!out_reserve(16)) return TLS_ERR_NO_MEM;
  if (c->mode == MBEDTLS_MODE_GCM) {
    size_t olen = 0;
    int err = gcm_ready(c);
    if (!err) err = mbedtls_gcm_finish(&c->u.gcm, out, 16, &olen, c->tag, 16);
    if (err) return err;
    c->finished = true;
    return out_set(olen);
  }
  if (!c->key_set || !c->iv_set || c->finished) return TLS_ERR_STATE;
  c->finished = true;
  if (c->op == MBEDTLS_ENCRYPT) {
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
  if (c->mode != MBEDTLS_MODE_GCM) return out_set(0);
  if (!c->finished) return TLS_ERR_STATE;
  return out_copy(c->tag, 16);
}

/* 1 when the tag matches the one computed at finish, 0 when not */
int espinel_tls_cipher_check_tag(cipher_t *c, const unsigned char *tag, size_t n) {
  if (c->mode != MBEDTLS_MODE_GCM) return 1;
  if (!c->finished) return TLS_ERR_STATE;
  if (n < 4 || n > 16) return TLS_ERR_LENGTH;
  return mbedtls_ct_memcmp(c->tag, tag, n) == 0;
}

/* ---- public keys: RSA and EC ---- */

int espinel_tls_pk_rsa(void) { return MBEDTLS_PK_RSA; }
int espinel_tls_pk_ec(void) { return MBEDTLS_PK_ECKEY; }

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
    err = mbedtls_pk_parse_key(pk, buf, len, NULL, 0, rng, NULL);
    *private = err == 0;
  }
  mbedtls_platform_zeroize(buf, n + 1);
  free(buf);
  return err;
}

static int pk_type(mbedtls_pk_context *pk) {
  mbedtls_pk_type_t t = mbedtls_pk_get_type(pk);
  return t == MBEDTLS_PK_ECDSA ? MBEDTLS_PK_ECKEY : (int)t;
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

int espinel_tls_pk_generate_rsa(int bits, int exponent) {
  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  int err = mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
  if (!err) err = mbedtls_rsa_gen_key(mbedtls_pk_rsa(pk), rng, NULL, bits, exponent);
  if (!err) {
    pk_last_type = MBEDTLS_PK_RSA;
    pk_last_private = 1;
    err = pk_pem(&pk, true);
  }
  mbedtls_pk_free(&pk);
  return err;
}

int espinel_tls_pk_generate_ec(const char *curve) {
  const mbedtls_ecp_curve_info *info = mbedtls_ecp_curve_info_from_name(curve);
  if (!info) return TLS_ERR_CURVE;
  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  int err = mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
  if (!err) err = mbedtls_ecp_gen_key(info->grp_id, mbedtls_pk_ec(pk), rng, NULL);
  if (!err) {
    pk_last_type = MBEDTLS_PK_ECKEY;
    pk_last_private = 1;
    err = pk_pem(&pk, true);
  }
  mbedtls_pk_free(&pk);
  return err;
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
                                  MBEDTLS_PK_SIGNATURE_MAX_SIZE, &sig_len, rng, NULL);
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
