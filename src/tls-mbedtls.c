/* ----------------------------------------------------------------------------
Copyright (c) 2018, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the Apache License, Version 2.0. A copy of the License can be
found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "nodec.h"
#include "nodec-internal.h"
#include "nodec-primitive.h"
#include <assert.h>

#ifndef NO_MBEDTLS

#include <mbedtls/include/mbedtls/ssl.h>
#include <mbedtls/include/mbedtls/net.h>
#include <mbedtls/include/mbedtls/error.h>
#include <mbedtls/include/mbedtls/entropy.h>
#include <mbedtls/include/mbedtls/ctr_drbg.h>

typedef struct _write_buf_args_t {
  nodec_stream_t* s;  
  uv_buf_t        buf;
} write_buf_args_t;

static lh_value _asyncx_write_buf(lh_value argsv) {
  write_buf_args_t* args = (write_buf_args_t*)lh_ptr_value(argsv);
  async_write_buf(args->s, args->buf);
  return lh_value_null;
}

uv_errno_t asyncx_write_buf(nodec_stream_t* s, uv_buf_t buf) {
  lh_exception* exn = NULL;
  write_buf_args_t args = { s, buf };
  lh_try(&exn, &_asyncx_write_buf, lh_value_any_ptr(&args));
  return (exn != NULL ? exn->code : 0);
}


typedef struct _read_buf_args_t {
  nodec_bstream_t* s;
  uv_buf_t         buf;
} read_buf_args_t;

static lh_value _asyncx_read_into(lh_value argsv) {
  read_buf_args_t* args = (read_buf_args_t*)lh_ptr_value(argsv);
  return lh_value_long((long)(async_read_into(args->s, args->buf)));
}

uv_errno_t asyncx_read_into(nodec_bstream_t* s, uv_buf_t buf, size_t* nread) {
  if (nread != NULL) *nread = 0;
  lh_exception* exn = NULL;
  read_buf_args_t args = { s, buf };
  lh_value nreadv = lh_try(&exn, &_asyncx_read_into, lh_value_any_ptr(&args));
  if (exn != NULL) return exn->code;
  if (nread != NULL) *nread = (size_t)lh_long_value(nreadv);
  return 0;
}


/* ----------------------------------------------------------------------------
  TLS streams
-----------------------------------------------------------------------------*/

static void nodec_throw_tls(int err, const char* msg) {
  char tls_err[256];
  mbedtls_strerror(err, tls_err, 256); tls_err[255] = 0;
  char s[256];
  snprintf(s, 256, "%s: %s (%i)", msg, tls_err, err); s[255] = 0;
  nodec_throw_msg(UV_EINVAL, s);
}

static void debug_callback(void* ctx, int level, const char* file, int line, const char* str) {
  FILE* stream = (FILE*)ctx;
  fprintf(stream, "%s:%04d: %s", file, line, str);
  fflush(stream);
}

struct _nodec_tls_stream_t {
  nodec_bstream_t     bstream;
  mbedtls_ssl_context ssl;
  nodec_bstream_t*    source;
  bool                handshaked;
  size_t              read_chunk_size;
};

void nodec_tls_stream_handshake(nodec_tls_stream_t* ts) {
  if (ts->handshaked) return;
  int res = mbedtls_ssl_handshake(&ts->ssl);
  if (res != 0) nodec_throw_tls(res, "unable to perform SSL handshake");
  ts->handshaked = true;
}

static bool async_tls_stream_read_chunks(nodec_tls_stream_t* ts)
{
  uv_buf_t buf = nodec_buf_alloc(ts->read_chunk_size);
  int res = mbedtls_ssl_read(&ts->ssl, (unsigned char*)buf.base, (size_t)buf.len);
  if (res <= 0) {
    // free the buffer and potentially throw
    nodec_buf_free(buf);
    if (res == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || res == MBEDTLS_ERR_NET_CONN_RESET) {
      // ok, just closed
      res = 0;  // make it like eof
    }
    else if (res < 0) {
      nodec_throw_tls(res, "unable to read from SSL stream");
    }
  }
  else {
    // push into the read chunks
    buf = nodec_buf_fit(buf, res); // TODO: should we do this?
    nodec_chunks_push(&ts->bstream, buf);
  }
  return (res == 0 || res < (size_t)buf.len);
}

static uv_buf_t async_tls_stream_read_bufx(nodec_stream_t* stream, bool* owned) {
  nodec_tls_stream_t* ts = (nodec_tls_stream_t*)stream;
  if (owned != NULL) *owned = true;
  if (nodec_chunks_available(&ts->bstream) == 0) {
    async_tls_stream_read_chunks(ts);
  }
  // hopefully by now we have available chunks
  return nodec_chunks_read_buf(&ts->bstream);
}

static bool async_tls_stream_read_chunk(nodec_bstream_t* s, nodec_chunk_read_t read_mode, size_t read_to_eof_max) {
  nodec_tls_stream_t* ts = (nodec_tls_stream_t*)s;
  if (read_mode == CREAD_NORMAL && nodec_chunks_available(&ts->bstream) > 0) {
    return false;
  }
  else {
    bool eof = false;
    if (read_mode != CREAD_TO_EOF) read_to_eof_max = 0;
    do {
      eof = async_tls_stream_read_chunks(ts);
    } while (!eof && nodec_chunks_available(&ts->bstream) < read_to_eof_max);
    return eof;
  }
}

static void async_tls_stream_write_buf(nodec_tls_stream_t* ts, uv_buf_t src) {
  size_t nwritten = 0;
  while (nwritten < (size_t)src.len) {
    int res = mbedtls_ssl_write(&ts->ssl, (const unsigned char*)src.base + nwritten, (size_t)src.len - nwritten);
    if (res == 0) {
      // todo: or throw broken pipe?
      break;
    }
    else if (res < 0) {
      nodec_throw_tls(res, "unable to write to SSL stream");
      break; // paranoia
    }
    else {
      nwritten += res;
    }
  }  
}

static void async_tls_stream_write_bufs(nodec_stream_t* s, uv_buf_t bufs[], size_t buf_count) {
  nodec_tls_stream_t* ts = (nodec_tls_stream_t*)s;
  for (size_t i = 0; i < buf_count; i++) {
    uv_buf_t src = bufs[i];
    if (nodec_buf_is_null(src)) continue;
    async_tls_stream_write_buf(ts, src);
  }
}

static void async_tls_stream_shutdown(nodec_stream_t* stream) {
  nodec_tls_stream_t* ts = (nodec_tls_stream_t*)stream;
  mbedtls_ssl_close_notify(&ts->ssl);
  //mbedtls_ssl_session_reset(&ts->ssl);
  async_shutdown(as_stream(ts->source));
}

static void nodec_tls_stream_free(nodec_stream_t* stream) {
  nodec_tls_stream_t* ts = (nodec_tls_stream_t*)stream;
  mbedtls_ssl_free(&ts->ssl);
  //nodec_stream_free(as_stream(ts->source));
  nodec_free(ts);
}

static int tls_stream_net_send(void* sv, const unsigned char* buf, size_t len) {
  nodec_bstream_t* s = (nodec_bstream_t*)sv;
  uv_errno_t err = asyncx_write_buf(as_stream(s), nodec_buf(buf, len));
  if (err != 0) return (err < 0 ? err : UV_EINVAL);
  return (int)len;
}


static int tls_stream_net_recv(void* sv, unsigned char* buf, size_t len) {
  nodec_bstream_t* s = (nodec_bstream_t*)sv;
  size_t nread = 0;
  uv_errno_t err = asyncx_read_into(s, nodec_buf(buf, len), &nread);
  if (err != 0) return (err < 0 ? err : UV_EINVAL);
    return (int)nread;
}

typedef struct _nodec_ssl_config_t nodec_ssl_config_t;

struct _nodec_ssl_config_t {
  mbedtls_ssl_config        mbedtls_config;  
  // the rest of the fields are just there to free automatically 
  // with the configuration
  mbedtls_ctr_drbg_context* drbg;
  mbedtls_entropy_context*  entropy;
  mbedtls_x509_crt*         chain;
  mbedtls_pk_context*       private_key;
};


nodec_bstream_t* nodec_tls_stream_alloc(nodec_bstream_t* stream, const nodec_ssl_config_t* config ) {
  int res = 0;
  nodec_tls_stream_t* ts = nodecx_zero_alloc(nodec_tls_stream_t);
  if (ts == NULL) { res = UV_ENOMEM; goto err; }
  nodec_bstream_init(&ts->bstream,
    &async_tls_stream_read_chunk, &nodec_chunks_pushback_buf,
    &async_tls_stream_read_bufx, &async_tls_stream_write_bufs,
    &async_tls_stream_shutdown, &nodec_tls_stream_free);
  ts->source = stream;
  ts->read_chunk_size = 14 * NODEC_KB;
  mbedtls_ssl_init(&ts->ssl);
  if (mbedtls_ssl_setup(&ts->ssl, &config->mbedtls_config) != 0) { res = UV_ENOMEM;  goto err; }
  mbedtls_ssl_set_bio(&ts->ssl, stream, tls_stream_net_send, tls_stream_net_recv, NULL);
  return &ts->bstream;
err:
  if (ts != NULL) nodec_free(ts);
  if (stream != NULL) nodec_stream_free(as_stream(stream));
  nodec_check(res);
  return NULL;
}


void nodec_ssl_config_free(nodec_ssl_config_t* config) {
  if (config == NULL) return;
  if (config->chain != NULL) {
    mbedtls_x509_crt_free(config->chain);
    nodec_free(config->chain);
  }
  if (config->private_key != NULL) {
    mbedtls_pk_free(config->private_key);
    nodec_free(config->private_key);
  }
  if (config->drbg != NULL) {
    mbedtls_ctr_drbg_free(config->drbg);
    nodec_free(config->drbg);
  }
  if (config->entropy != NULL) {
    (config->entropy);
    nodec_free(config->entropy);
  }
  mbedtls_ssl_config_free(&config->mbedtls_config);
  nodec_free(config);
}

void nodec_ssl_config_freev(lh_value configv) {
  nodec_ssl_config_t* config = (nodec_ssl_config_t*)lh_ptr_value(configv);
  nodec_ssl_config_free(config);
}

nodec_ssl_config_t* nodec_ssl_config_server(uv_buf_t cert, uv_buf_t key, const char* password ) {
  nodec_ssl_config_t* config = nodec_zero_alloc(nodec_ssl_config_t);
  {on_abort(nodec_ssl_config_freev, lh_value_any_ptr(config)) {
    mbedtls_ssl_config_init(&config->mbedtls_config);
    int res = mbedtls_ssl_config_defaults(&config->mbedtls_config, 
      MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (res != 0) nodec_throw_tls(res, "cannot configure TLS" );
    mbedtls_ssl_conf_dbg(&config->mbedtls_config, &debug_callback, stderr);
    mbedtls_debug_set_threshold(1);

    // init random numbers
    config->drbg = nodec_alloc(mbedtls_ctr_drbg_context);
    mbedtls_ctr_drbg_init(config->drbg);
    config->entropy = nodec_alloc(mbedtls_entropy_context);
    mbedtls_entropy_init(config->entropy);
    res = mbedtls_ctr_drbg_seed(config->drbg, &mbedtls_entropy_func, config->entropy, "", 0);
    if (res != 0) nodec_throw_tls(res, "cannot initialize random number generator");
    mbedtls_ssl_conf_rng(&config->mbedtls_config, mbedtls_ctr_drbg_random, config->drbg);

    // set our own certificate
    config->chain = nodec_alloc(mbedtls_x509_crt);
    mbedtls_x509_crt_init(config->chain);
    res = mbedtls_x509_crt_parse(config->chain, (const unsigned char*)cert.base, (size_t)cert.len + 1); // include zero byte at end
    if (res != 0) nodec_throw_tls(res, "cannot parse x509 certificate");
    config->private_key = nodec_alloc(mbedtls_pk_context);
    mbedtls_pk_init(config->private_key);
    res = mbedtls_pk_parse_key(config->private_key, (const unsigned char*)key.base, (size_t)key.len + 1, (const unsigned char*)password, strlen(password));
    if (res != 0) nodec_throw_tls(res, "invalid password");
    res = mbedtls_ssl_conf_own_cert(&config->mbedtls_config, config->chain, config->private_key);
    if (res != 0) nodec_throw_tls(res, "cannot set server certificate");

    // TODO: enable session tickets? rfc5070
  }}
  return config;
}

nodec_ssl_config_t* nodec_ssl_config_server_from(const char* cert_path, const char* key_path, const char* password) {
  nodec_ssl_config_t* config = NULL;
  uv_buf_t cert = async_fs_read_buf_from(cert_path);
  {using_buf(&cert) {
    uv_buf_t key = async_fs_read_buf_from(key_path);
    {using_buf(&key) {
      config = nodec_ssl_config_server(cert, key, password);
    }}
  }}
  return config;       
}


#endif // NO_MBEDTLS