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
  char s[256];
  snprintf(s, 256, "%s: tls code: %i", msg, err); s[255] = 0;
  nodec_throw_msg(UV_EINVAL, s);
}

typedef struct _nodec_tls_stream_t {
  nodec_bstream_t     bstream;
  mbedtls_ssl_context ssl;
  nodec_bstream_t*    source;
  bool                handshaked;
  size_t              read_chunk_size;
} nodec_tls_stream_t;

static void nodec_tls_stream_handshake(nodec_tls_stream_t* ts) {
  if (ts->handshaked) return;
  int res = mbedtls_ssl_handshake(&ts->ssl);
  if (res != 0) nodec_throw_tls(res, "unable to perform SSL handshake");
  ts->handshaked = true;
}

static void nodec_tls_stream_init_read(nodec_tls_stream_t* ts) {
  nodec_tls_stream_handshake(ts);
}

static void nodec_tls_stream_init_write(nodec_tls_stream_t* ts) {
  nodec_tls_stream_handshake(ts);
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
  return (res < (size_t)buf.len);
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
  nodec_tls_stream_init_write(ts);  
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
  // mbedtls_ssl_session_reset(&ts->ssl);
  async_shutdown(ts->source);
}

static void nodec_tls_stream_free(nodec_stream_t* stream) {
  nodec_tls_stream_t* ts = (nodec_tls_stream_t*)stream;
  mbedtls_ssl_free(&ts->ssl);
  nodec_stream_free(ts->source);
  nodec_free(ts);
}

static int tls_stream_net_send(void* sv, const unsigned char* buf, size_t len) {
  nodec_bstream_t* s = (nodec_bstream_t*)sv;
  uv_errno_t err = asyncx_write_buf(as_stream(s), nodec_buf(buf, len));
  if (err != 0) return (err < 0 ? err : UV_EINVAL);
  return len;
}


static int tls_stream_net_recv(void* sv, unsigned char* buf, size_t len) {
  nodec_bstream_t* s = (nodec_bstream_t*)sv;
  size_t nread = 0;
  uv_errno_t err = asyncx_read_into(s, nodec_buf(buf, len), &nread);
  if (err != 0) return (err < 0 ? err : UV_EINVAL);
  return nread;
}

typedef struct _nodec_ssl_config_t nodec_ssl_config_t;

struct _nodec_ssl_config_t {
  mbedtls_ssl_config mbedtls_config;
};


nodec_tls_stream_t* nodec_mbedtls_stream_alloc(nodec_bstream_t* stream, const nodec_ssl_config_t* config ) {
  int res = 0;
  nodec_tls_stream_t* ts = nodecx_zero_alloc(nodec_tls_stream_t);
  if (ts == NULL) { res = UV_ENOMEM; goto err; }
  mbedtls_ssl_init(&ts->ssl);
  if (mbedtls_ssl_setup(&ts->ssl, &config->mbedtls_config) != 0) { res = UV_ENOMEM;  goto err; }
  mbedtls_ssl_set_bio(&ts->ssl, stream, tls_stream_net_send, tls_stream_net_recv, NULL);
  nodec_bstream_init(&ts->bstream,
    &async_tls_stream_read_chunk, &nodec_chunks_pushback_buf,
    &async_tls_stream_read_bufx, &async_tls_stream_write_bufs,
    &async_tls_stream_shutdown, &nodec_tls_stream_free);
  ts->source = stream;
  return ts;
err:
  if (ts != NULL) nodec_free(ts);
  if (stream != NULL) nodec_stream_free(stream);
  nodec_check(res);
  return NULL;
}


void nodec_ssl_config_free(nodec_ssl_config_t* config) {
  if (config == NULL) return;
  mbedtls_ssl_config_free(&config->mbedtls_config);
  nodec_free(config);
}

void nodec_ssl_config_freev(lh_value configv) {
  nodec_ssl_config_t* config = (nodec_ssl_config_t*)lh_ptr_value(configv);
  nodec_ssl_config_free(config);
}

nodec_ssl_config_t* nodec_tls_config_server(uv_buf_t cert, uv_buf_t key, const char* password ) {
  nodec_ssl_config_t* config = nodec_alloc(nodec_ssl_config_t);
  {on_abort(nodec_ssl_config_freev, lh_value_any_ptr(config)) {
    mbedtls_ssl_config_init(&config->mbedtls_config);
    const int endpt = MBEDTLS_SSL_IS_SERVER;
    const int tport = MBEDTLS_SSL_TRANSPORT_STREAM;
    const int present = MBEDTLS_SSL_PRESET_DEFAULT;
    int res = mbedtls_ssl_config_defaults(&config->mbedtls_config, endpt, tport, present);
    if (res != 0) nodec_throw_tls(res, "cannot configure TLS" );

    // set our own certificate
    // TODO: free intermediates on error; check if free_config frees the chain and key
    mbedtls_x509_crt* chain = nodec_alloc(mbedtls_x509_crt);
    mbedtls_x509_crt_init(chain);
    res = mbedtls_x509_crt_parse(chain, (const unsigned char*)cert.base, (size_t)cert.len);
    if (res != 0) nodec_throw_tls(res, "cannot parse x509 certificate");
    mbedtls_pk_context* pk = nodec_alloc(mbedtls_pk_context);
    mbedtls_pk_init(pk);
    res = mbedtls_pk_parse_key(pk, (const unsigned char*)key.base, (size_t)key.len, (const unsigned char*)password, strlen(password));
    if (res != 0) nodec_throw_tls(res, "invalid password");
    res = mbedtls_ssl_conf_own_cert(&config->mbedtls_config, chain, pk);
    if (res != 0) nodec_throw_tls(res, "cannot set server certificate");
  }}
  return config;
}




#endif // NO_MBEDTLS