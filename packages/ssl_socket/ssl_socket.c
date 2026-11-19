/* TLS client sessions over a connected socket, on ESP-IDF's Mbed TLS, for
   ssl_socket.rb.

   A session is an Mbed TLS context and its configuration, certificates and
   key, kept in a slot of a small table: Ruby holds the slot number, and
   close frees the slot (the session buffers are about 20 KB, so a socket
   that is never closed keeps them, as it keeps its descriptor). With no CA
   given, the peer is verified against ESP-IDF's certificate bundle
   (CONFIG_MBEDTLS_CERTIFICATE_BUNDLE), which lives in flash.

   The records go through the socket's descriptor with send and recv; a
   descriptor in non-blocking mode (Spinel's sockets can be) is waited on
   with select. Calls answer a count, or a negative error: Mbed TLS's own,
   or SSL_ERR_TIMEOUT when timeout_ms (-1 for none) runs out. */
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

/* the byte count of a :binstr return (Spinel's sp_alloc.h) */
extern int sp_ffi_bin_len;

#define SSL_ERR_TIMEOUT (-0x10101)
#define SSL_ERR_HANDLE (-0x10102)
#define SSL_ERR_NO_MEM (-0x10103)
#define SSL_SESSIONS 4

typedef struct {
  int fd;
  int64_t deadline;  /* of the call under way; 0 for none */
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config conf;
  mbedtls_x509_crt ca, own;
  mbedtls_pk_context key;
} session_t;

static session_t *sessions[SSL_SESSIONS];
static char message[256];

/* why the last call failed */
const char *espinel_ssl_message(void) { return message; }

static int fail(int err, const char *what) {
  char text[128];
  if (err == SSL_ERR_TIMEOUT) snprintf(text, sizeof text, "timed out");
  else if (err == SSL_ERR_HANDLE) snprintf(text, sizeof text, "closed");
  else if (err == SSL_ERR_NO_MEM) snprintf(text, sizeof text, "out of memory");
  else mbedtls_strerror(err, text, sizeof text);
  snprintf(message, sizeof message, "%s: %s", what, text);
  return err;
}

int espinel_ssl_timed_out(int err) { return err == SSL_ERR_TIMEOUT; }

static session_t *session(int h) {
  return h >= 0 && h < SSL_SESSIONS ? sessions[h] : NULL;
}

/* ---- the descriptor ---- */

/* until fd is ready (write or read) or the call's deadline */
static int wait_fd(session_t *s, bool write) {
  fd_set set;
  struct timeval tv, *tvp = NULL;
  FD_ZERO(&set);
  FD_SET(s->fd, &set);
  if (s->deadline) {
    int64_t left = s->deadline - esp_timer_get_time();
    if (left <= 0) return SSL_ERR_TIMEOUT;
    tv.tv_sec = left / 1000000;
    tv.tv_usec = left % 1000000;
    tvp = &tv;
  }
  int rc = select(s->fd + 1, write ? NULL : &set, write ? &set : NULL, NULL, tvp);
  if (rc == 0) return SSL_ERR_TIMEOUT;
  return rc < 0 ? MBEDTLS_ERR_NET_POLL_FAILED : 0;
}

static int bio_error(void) {
  if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 1;
  if (errno == EPIPE || errno == ECONNRESET) return MBEDTLS_ERR_NET_CONN_RESET;
  return 0;
}

/* A blocking descriptor would sit in send or recv past the deadline, so
   with one the descriptor is waited on first. */
static int bio_send(void *p, const unsigned char *buf, size_t n) {
  session_t *s = p;
  for (;;) {
    int rc;
    if (s->deadline && (rc = wait_fd(s, true)) != 0) return rc;
    rc = send(s->fd, buf, n, 0);
    if (rc >= 0) return rc;
    int e = bio_error();
    if (e < 0) return e;
    if (e == 0) return MBEDTLS_ERR_NET_SEND_FAILED;
    if (!s->deadline && (rc = wait_fd(s, true)) != 0) return rc;
  }
}

static int bio_recv(void *p, unsigned char *buf, size_t n) {
  session_t *s = p;
  for (;;) {
    int rc;
    if (s->deadline && (rc = wait_fd(s, false)) != 0) return rc;
    rc = recv(s->fd, buf, n, 0);
    if (rc >= 0) return rc;
    int e = bio_error();
    if (e < 0) return e;
    if (e == 0) return MBEDTLS_ERR_NET_RECV_FAILED;
    if (!s->deadline && (rc = wait_fd(s, false)) != 0) return rc;
  }
}

static void set_deadline(session_t *s, int timeout_ms) {
  s->deadline = timeout_ms < 0 ? 0 : esp_timer_get_time() + (int64_t)timeout_ms * 1000;
}

/* ---- sessions ---- */

static void session_free(session_t *s) {
  mbedtls_ssl_free(&s->ssl);
  mbedtls_ssl_config_free(&s->conf);
  mbedtls_x509_crt_free(&s->ca);
  mbedtls_x509_crt_free(&s->own);
  mbedtls_pk_free(&s->key);
  free(s);
}

/* PEM as Mbed TLS takes it: with its terminating NUL counted */
static int parse_crt(mbedtls_x509_crt *crt, const char *pem, size_t n) {
  unsigned char *buf = malloc(n + 1);
  if (!buf) return SSL_ERR_NO_MEM;
  memcpy(buf, pem, n);
  buf[n] = '\0';
  int rc = mbedtls_x509_crt_parse(crt, buf, strstr((char *)buf, "-----BEGIN") ? n + 1 : n);
  free(buf);
  return rc < 0 ? rc : 0;
}

static int parse_key(mbedtls_pk_context *key, const char *pem, size_t n) {
  unsigned char *buf = malloc(n + 1);
  if (!buf) return SSL_ERR_NO_MEM;
  memcpy(buf, pem, n);
  buf[n] = '\0';
  int rc = mbedtls_pk_parse_key(key, buf, strstr((char *)buf, "-----BEGIN") ? n + 1 : n, NULL, 0);
  mbedtls_platform_zeroize(buf, n + 1);
  free(buf);
  return rc;
}

/* why the peer's certificate was refused, after a failed handshake */
static void verify_message(session_t *s) {
  uint32_t flags = mbedtls_ssl_get_verify_result(&s->ssl);
  if (flags == 0 || flags == (uint32_t)-1) return;
  size_t len = strlen(message);
  char info[160];
  mbedtls_x509_crt_verify_info(info, sizeof info, "", flags);
  for (char *p = info; *p; p++)
    if (*p == '\n') *p = *(p + 1) ? ';' : '\0';
  snprintf(message + len, sizeof message - len, " (%s)", info);
}

/* Handshakes over the connected socket fd. host names the server for SNI
   and the certificate check (NULL for neither); ca (PEM, empty for the
   bundle), cert and key (empty for none) are the context's. Answers the
   session's number. */
int espinel_ssl_connect(int fd, const char *host, int verify, const char *ca, size_t ca_n,
                        const char *cert, size_t cert_n, const char *key, size_t key_n,
                        int timeout_ms) {
  int h;
  for (h = 0; h < SSL_SESSIONS && sessions[h]; h++) {}
  if (h == SSL_SESSIONS) {
    snprintf(message, sizeof message, "SSL: no free session (%d are open)", SSL_SESSIONS);
    return SSL_ERR_NO_MEM;
  }
  /* the bundle logs every verdict; the exception carries the refusals */
  esp_log_level_set("esp-x509-crt-bundle", ESP_LOG_NONE);
  session_t *s = calloc(1, sizeof *s);
  if (!s) return fail(SSL_ERR_NO_MEM, "SSL");
  s->fd = fd;
  mbedtls_ssl_init(&s->ssl);
  mbedtls_ssl_config_init(&s->conf);
  mbedtls_x509_crt_init(&s->ca);
  mbedtls_x509_crt_init(&s->own);
  mbedtls_pk_init(&s->key);

  const char *what = "SSL configuration";
  int rc = mbedtls_ssl_config_defaults(&s->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                       MBEDTLS_SSL_PRESET_DEFAULT);
  if (!rc) {
    mbedtls_ssl_conf_authmode(&s->conf, verify ? MBEDTLS_SSL_VERIFY_REQUIRED : MBEDTLS_SSL_VERIFY_NONE);
    if (ca_n > 0) {
      what = "CA certificate";
      if (!(rc = parse_crt(&s->ca, ca, ca_n))) mbedtls_ssl_conf_ca_chain(&s->conf, &s->ca, NULL);
    } else if (verify) {
      what = "certificate bundle";
      rc = esp_crt_bundle_attach(&s->conf);
    }
  }
  if (!rc && cert_n > 0) {
    what = "certificate";
    rc = parse_crt(&s->own, cert, cert_n);
    if (!rc) { what = "key"; rc = parse_key(&s->key, key, key_n); }
    if (!rc) rc = mbedtls_ssl_conf_own_cert(&s->conf, &s->own, &s->key);
  }
  if (!rc) { what = "SSL setup"; rc = mbedtls_ssl_setup(&s->ssl, &s->conf); }
  if (!rc && host) { what = "SSL hostname"; rc = mbedtls_ssl_set_hostname(&s->ssl, host); }
  if (!rc) {
    what = "SSL handshake";
    mbedtls_ssl_set_bio(&s->ssl, s, bio_send, bio_recv, NULL);
    set_deadline(s, timeout_ms);
    while ((rc = mbedtls_ssl_handshake(&s->ssl)) == MBEDTLS_ERR_SSL_WANT_READ ||
           rc == MBEDTLS_ERR_SSL_WANT_WRITE) {}
  }
  if (rc) {
    fail(rc, what);
    /* the bundle's callback refuses a chain without saying why */
    if (rc == MBEDTLS_ERR_X509_FATAL_ERROR && verify && ca_n == 0)
      snprintf(message, sizeof message,
               "SSL handshake: the server's certificate is not signed by a CA in ESP-IDF's bundle");
    else
      verify_message(s);
    session_free(s);
    return rc;
  }
  sessions[h] = s;
  return h;
}

int espinel_ssl_write(int h, const unsigned char *data, size_t n, int timeout_ms) {
  session_t *s = session(h);
  if (!s) return fail(SSL_ERR_HANDLE, "SSL write");
  set_deadline(s, timeout_ms);
  size_t done = 0;
  while (done < n) {
    int rc = mbedtls_ssl_write(&s->ssl, data + done, n - done);
    if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
    if (rc < 0) return fail(rc, "SSL write");
    done += rc;
  }
  return (int)done;
}

/* ---- reading ---- */

static unsigned char *in;
static size_t in_cap, in_len;

const char *espinel_ssl_out(void) {
  sp_ffi_bin_len = (int)in_len;
  return in ? (const char *)in : "";
}

/* Up to max bytes, waiting for the first; 0 at the end (the peer's
   close_notify, or the connection closed) */
int espinel_ssl_read(int h, size_t max, int timeout_ms) {
  session_t *s = session(h);
  if (!s) return fail(SSL_ERR_HANDLE, "SSL read");
  if (max == 0) { in_len = 0; return 0; }
  if (max > in_cap) {
    unsigned char *p = realloc(in, max);
    if (!p) return fail(SSL_ERR_NO_MEM, "SSL read");
    in = p;
    in_cap = max;
  }
  set_deadline(s, timeout_ms);
  for (;;) {
    int rc = mbedtls_ssl_read(&s->ssl, in, max);
    if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
    if (rc == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;
#endif
    if (rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || rc == MBEDTLS_ERR_NET_CONN_RESET) rc = 0;
    if (rc < 0) return fail(rc, "SSL read");
    in_len = rc;
    return rc;
  }
}

/* decrypted bytes waiting, readable without the socket */
int espinel_ssl_pending(int h) {
  session_t *s = session(h);
  return s ? (int)mbedtls_ssl_get_bytes_avail(&s->ssl) : 0;
}

/* Mbed TLS's verification flags for the peer's certificate (0: verified) */
int espinel_ssl_verify_result(int h) {
  session_t *s = session(h);
  return s ? (int)mbedtls_ssl_get_verify_result(&s->ssl) : -1;
}

const char *espinel_ssl_version(int h) {
  session_t *s = session(h);
  return s ? mbedtls_ssl_get_version(&s->ssl) : "";
}

const char *espinel_ssl_cipher(int h) {
  session_t *s = session(h);
  return s ? mbedtls_ssl_get_ciphersuite(&s->ssl) : "";
}

/* Sends close_notify (without waiting long for it to go) and frees the
   session; the socket is the caller's to close. */
int espinel_ssl_close(int h) {
  session_t *s = session(h);
  if (!s) return 0;
  set_deadline(s, 1000);
  mbedtls_ssl_close_notify(&s->ssl);
  sessions[h] = NULL;
  session_free(s);
  return 0;
}
