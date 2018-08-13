/* ----------------------------------------------------------------------------
  Copyright (c) 2018, Microsoft Research, Daan Leijen
  This is free software; you can redistribute it and/or modify it under the
  terms of the Apache License, Version 2.0. A copy of the License can be
  found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/

#pragma once
#ifndef __nodec_h 
#define __nodec_h

#include <libhandler.h>
#include <uv.h>
#include <http_parser.h>


/* ----------------------------------------------------------------------------
  Notes:
  - Private functions are prepended with an underscore. Dont use them directly.
  - Pre- and post-fixes:
    "async_" : for asynchronous functions that might interleave
    "nodec_" : synchronous functions that might throw exeptions or use other effects.
    "nodecx_": synchronous functions that return an explicit error result.
    "..._t"  : for types
    "using_"  : for scoped combinators, to be used with double curly braces:
               i.e. "{using_alloc(tp,name){ ... <use name> ... }}"

  - an `lh_value` is used to mimic polymorphism. There are a host of 
    conversion functions, like `lh_ptr_value` (from value to pointer)
    or `lh_value_int` (from int to a value).
-----------------------------------------------------------------------------*/


// Forward declarations 
typedef struct _channel_t channel_t;

/// Throw an error, either an `errno` or `uv_errno_t`.
/// \param err error code.
void nodec_throw(int err);

/// Throw an error, either an `errno` or `uv_errno_t`.
/// \param err error code.
/// \param msg custom message.
void nodec_throw_msg(int err, const char* msg);

/* ----------------------------------------------------------------------------
Buffers are `uv_buf_t` which contain a `base` pointer and the
available `len` bytes. These buffers are usually passed by value.
-----------------------------------------------------------------------------*/

/// \defgroup buffers Buffer's
/// Convenience routines around the \a libuv `uv_buf_t` structure:
/// ```
/// struct uv_buf_t {
///   size_t len;
///   void*  base;
/// }
/// ```
///
/// @{

/// Initialize a libuv buffer, which is a record with a data pointer and its length.
uv_buf_t nodec_buf(const void* base, size_t len);
uv_buf_t nodec_buf_str(const char* s);
uv_buf_t nodec_buf_strdup(const char* s);

/// Create a null buffer, i.e. `nodec_buf(NULL,0)`.
uv_buf_t nodec_buf_null();

// Create and allocate a buffer
uv_buf_t nodec_buf_alloc(size_t len);
uv_buf_t nodec_buf_realloc(uv_buf_t buf, size_t len);

// Ensure a buffer has at least `needed` size.
// If `buf` is null, use 8kb for initial size, and after that use doubling of at most 4mb increments.
uv_buf_t nodec_buf_ensure(uv_buf_t buf, size_t needed);
uv_buf_t nodec_buf_ensure_ex(uv_buf_t buf, size_t needed, size_t initial_size, size_t max_increase);
// Fit a buffer to `needed` size. Might increase the buffer size, or
// Reallocate to a smaller size if it was larger than 128 bytes with more than 20% wasted.
uv_buf_t nodec_buf_fit(uv_buf_t buf, size_t needed);

uv_buf_t nodec_buf_append_into(uv_buf_t buf1, uv_buf_t buf2);


/// Is this a null buffer?.
/// \param buf the buffer.
/// \returns `true` if the buffer length is 0 or the `base` is `NULL`.
bool nodec_buf_is_null(uv_buf_t buf);

void nodec_buf_free(uv_buf_t buf);
void nodec_bufref_free(uv_buf_t* buf);
void nodec_bufref_freev(lh_value bufref);

/// Use a buffer in a scope, freeing automatically when exiting.
/// Pass the buffer by reference so it will free
/// the final memory that `bufref->base` points to (and not the initial value).
/// \param bufref  a reference to the buffer to free after use.
/// \b Example
/// ```
/// uv_buf_t buf = async_read_buf(stream);
/// {using_buf(&buf){
///    async_write_buf(out,buf);
/// }}
/// ```
#define using_buf(bufref)                 defer(nodec_bufref_freev,lh_value_any_ptr(bufref))

/// Use a buffer in a scope, freeing automatically only when an exception is thrown.
/// Pass the buffer by reference so it will free
/// the final memory that `bufref->base` points to (and not the initial value).
/// \param bufref  a reference to the buffer to free after use.
#define using_on_abort_free_buf(bufref)   on_abort(nodec_bufref_freev,lh_value_any_ptr(bufref))

/// @}

bool nodec_starts_with(const char* s, const char* prefix);
bool nodec_starts_withi(const char* s, const char* prefix);
bool nodec_ends_with(const char* s, const char* prefix);
bool nodec_ends_withi(const char* s, const char* prefix);


/* ----------------------------------------------------------------------------
  Cancelation scope
-----------------------------------------------------------------------------*/

// private
implicit_declare(_cancel_scope)
lh_value _cancel_scope_alloc();

// execute under a cancelation scope
#define using_cancel_scope()        using_implicit_defer(nodec_freev,_cancel_scope_alloc(),_cancel_scope)

// Asynchronously cancel all outstanding requests under the same
// cancelation scope.
void async_scoped_cancel();

// Is the current scope canceled?
bool async_scoped_is_canceled();

/* ----------------------------------------------------------------------------
  Asynchronous combinators
-----------------------------------------------------------------------------*/

// Interleave `n` actions with arguments `arg_results`. 
// The result of each action is stored again in `arg_results`; when
// an exception is raised, it is rethrown from `interleave` once all
// its actions have finished. Interleave introduces a cancelation 
// scope.
void interleave(size_t n, lh_actionfun* actions[], lh_value arg_results[]);

// General timeout routine over an `action`. 
lh_value async_timeout(lh_actionfun* action, lh_value arg, uint64_t timeout, bool* timedout);

// Return when the either action is done, canceling the other.
// Return `true` if the first action was finished first.
typedef void (nodec_actionfun_t)();
bool async_firstof(nodec_actionfun_t* action1, nodec_actionfun_t* action2);

// Return the value of the first returning action, canceling the other.
lh_value async_firstof_ex(lh_actionfun* action1, lh_value arg1, lh_actionfun* action2, lh_value arg2, bool* first, bool ignore_exn);

// Asynchronously wait for `timeout` milli-seconds.
void async_wait(uint64_t timeout);

// Yield asynchronously to other strands.
void async_yield();


// Get the current time as a RFC1123 internet date.
// Caches previous results to increase efficiency.
const char* nodec_inet_date_now();
const char* nodec_inet_date(time_t now);
bool nodec_parse_inet_date(const char* date, time_t* t);

/* ----------------------------------------------------------------------------
  File system (fs)
-----------------------------------------------------------------------------*/

uv_errno_t  asyncx_stat(const char* path, uv_stat_t* stat);
uv_stat_t   async_stat(const char* path);
uv_stat_t   async_fstat(uv_file file);

uv_file     async_fopen(const char* path, int flags, int mode);
uv_errno_t  asyncx_fopen(const char* path, int flags, int mode, uv_file* file );
void        async_fclose(uv_file file);

size_t      async_fread_into(uv_file file, uv_buf_t buf, int64_t file_offset);
uv_buf_t    async_fread_buf(uv_file file, size_t max, int64_t file_offset);
uv_buf_t    async_fread_buf_all(uv_file file, size_t max);

typedef uv_fs_t nodec_scandir_t;
void        nodec_scandir_free(nodec_scandir_t* scanreq);
void        nodec_scandir_freev(lh_value scanreqv);
#define using_scandir(req)  defer(nodec_scandir_freev,lh_value_ptr(req))

nodec_scandir_t* async_scandir(const char* path);
bool async_scandir_next(nodec_scandir_t* scanreq, uv_dirent_t* dirent);

// ----------------------------------
// File system convenience functions

char*       async_fread_from(const char* path);
uv_buf_t    async_fread_buf_from(const char* path);

typedef lh_value(nodec_file_fun)(uv_file file, const char* path, lh_value arg);
lh_value    using_async_fopen(const char* path, int flags, int mode, nodec_file_fun* action, lh_value arg);
uv_errno_t  using_asyncx_fopen(const char* path, int flags, int mode, nodec_file_fun* action, lh_value arg, lh_value* result);



/* ----------------------------------------------------------------------------
  Streams
-----------------------------------------------------------------------------*/
///\defgroup stream Streams
///@{

/// Basic unbuffered streams.
typedef struct _nodec_stream_t  nodec_stream_t;

// Primitives

/// Free a basic stream.
/// Usually this is not used directly but instead using_stream() is used.
void      nodec_stream_free(nodec_stream_t* stream);

/// Free a basic stream as an `lh_value`.
/// Usually this is not used directly but instead using_stream() is used.
void      nodec_stream_freev(lh_value streamv);

/// Shutdown a stream. 
///
/// This closes the stream gracefully for writes.
/// Usually this is not used directly but instead using_stream() is used.
void      async_shutdown(nodec_stream_t* stream);

/// Use a stream in a scope, freeing the stream when exiting.
///
/// This safely uses the stream in a scope and frees the stream afterwards
/// (even if an exception is thrown).
/// Uses async_shutdown(stream) too if no exception was thrown.
/// \param s   the #nodec_stream_t to free after use.
///
/// \b Example
/// ```
/// nodec_stream_t s = ...
/// {using_stream(s){
///    async_write(s, "hello");
/// }}
/// ```
#define using_stream(s) \
    defer_exit(async_shutdown(s),&nodec_stream_freev,lh_value_ptr(s))

/// Asynchronously read a buffer from a stream.
/// \param stream  stream to read from.
/// \returns uv_buf_t  a buffer with the data. This buffer is becomes
///    the callee's responsibility (see using_buf()) and is passed without
///    copying from the lower level read routines.
uv_buf_t  async_read_buf(nodec_stream_t* stream);

/// Asynchronously read a string from a stream.
char*     async_read(nodec_stream_t* stream);

/// Write an array of buffers to a stream.
/// \param stream  the stream to write to.
/// \param bufs    the array of buffers to write.
/// \param count the number of buffers in the array.
void      async_write_bufs(nodec_stream_t* stream, uv_buf_t bufs[], size_t count);

/// Write a buffer to a stream.
void      async_write_buf(nodec_stream_t* stream, uv_buf_t buf);

/// Write a string to a stream.
/// \param stream stream to write to.
/// \param s      the string to write.
void      async_write(nodec_stream_t* stream, const char* s);

/// The type of buffered streams. 
/// Derives from a basic #nodec_stream_t and can be cast to it using as_stream().
/// Buffered streams have added functionality, like being able to read
/// a stream up to a certain pattern is encountered, or reading the full
/// stream as a single buffer.
typedef struct _nodec_bstream_t nodec_bstream_t;

/// Safely cast a buffered stream to a basic stream.
nodec_stream_t*  as_stream(nodec_bstream_t* stream);

/// Use a buffered stream in a scope, freeing the stream when exiting.
///
/// This safely uses the stream in a scope and frees the stream afterwards
/// (even if an exception is thrown).
/// Uses async_shutdown(stream) too if no exception was thrown.
/// \param s   the #nodec_bstream_t to free after use.
///
/// \b Example
/// ```
/// nodec_bstream_t s = nodec_tcp_connect("http://www.google.com");
/// {using_bstream(s){
///    uv_buf_t buf = async_read_buf_all(s);
///    {using_buf(&buf){
///       printf("received: %s\n", buf.base);
///    }}
/// }}
/// ```
#define using_bstream(s)  using_stream(as_stream(s))  

/// Read the entire stream as a single buffer.
uv_buf_t  async_read_buf_all(nodec_bstream_t* bstream);

/// Read the entire stream as a string.
char*     async_read_all(nodec_bstream_t* bstream);

/// Read a stream into a pre-allocated buffer.
/// \param bstream  the stream to read from.
/// \param buf      the buffer to read into; reads up to either the end of the stream
///                 or up to the `buf.len`.
/// \returns the number of bytes read.
size_t    async_read_into(nodec_bstream_t* bstream, uv_buf_t buf);

/// Read a stream until some pattern is encountered.
/// The returned buffer will contain `pat` unless the end-of-stream was 
/// encountered or `read_max` bytes were read. The return buffer may contain 
/// more data following the
/// the pattern `pat`. This is more efficient than async_read_buf_upto which may involve
/// some memory copying to split buffers.
/// \param bstream the stream to read from.
/// \param[out] toread  the number of bytes just including the pattern `pat`. 0 on failure.
/// \param[in]  pat     the pattern to scan for.
/// \param[in] pat_len   the length of the patter. For efficiency reasons, this can be at
///                      most 8 (bytes).
/// \param[in] read_max stop reading after `read_max` bytes have been seen. Depending on 
///          the internal buffering, the returned buffer might still contain more bytes.
/// \returns buffer with the read bytes where `buf.len >= *toread`. Returns
///          a null buffer (see nodec_buf_is_null()) if an end-of-stream is encountered.
uv_buf_t  async_read_buf_including(nodec_bstream_t* bstream, size_t* toread, const void* pat, size_t pat_len, size_t read_max);

/// Read a stream until some pattern is encountered.
/// The returned buffer will contain `pat` at its end unless the end-of-stream was 
/// encountered or `read_max` bytes were read. 
/// \param bstream the stream to read from.
/// \param[in]  pat     the pattern to scan for.
/// \param[in] pat_len   the length of the patter. For efficiency reasons, this can be at
///                      most 8 (bytes).
/// \param[in] read_max stop reading after `read_max` bytes have been seen. Depending on 
///          the internal buffering, the returned buffer might still contain more bytes.
/// \returns buffer with the read bytes. Returns
///          a null buffer (see nodec_buf_is_null()) if an end-of-stream is encountered.
uv_buf_t  async_read_buf_upto(nodec_bstream_t* bstream, const void* pat, size_t pat_len, size_t read_max);

/// Read the first line of a buffered stream.
uv_buf_t  async_read_buf_line(nodec_bstream_t* bstream);

/// Read the first line as a string from a buffered stream.
char*     async_read_line(nodec_bstream_t* bstream);

/// Create a buffered stream from a plain stream
nodec_bstream_t* nodec_bstream_alloc_on(nodec_stream_t* source);


/// Low level: Create a buffered stream from an internal `uv_stream_t`.
///
/// \param stream   the underlying `uv_stream_t`. Freed when the #nodec_bstream_t is freed.
/// \returns a buffered stream.
nodec_bstream_t* nodec_bstream_alloc(uv_stream_t* stream);

/// Low level: Create a buffered stream from an internal `uv_stream_t` for reading.
///
/// \param stream   the underlying `uv_stream_t`. Freed when the #nodec_bstream_t is freed.
/// \returns a buffered stream.
nodec_bstream_t* nodec_bstream_alloc_read(uv_stream_t* stream);

/// Low level: Create a buffered stream from an internal `uv_stream_t` for reading.
///
/// \param stream   the underlying `uv_stream_t`. Freed when the #nodec_bstream_t is freed.
/// \param read_max  return end-of-stream after `read_max` bytes are read. Use 0 for no maximum.
/// \param alloc_init the initial allocation size for read buffers. Use 0 for default (8k). Doubles on further data until `alloc_max`.
/// \param alloc_max  the maximal allocation size for read buffers.
/// \returns a buffered stream.
nodec_bstream_t* nodec_bstream_alloc_read_ex(uv_stream_t* stream, size_t read_max, size_t alloc_init, size_t alloc_max);


#ifndef NO_ZLIB

nodec_bstream_t* nodec_zstream_alloc(nodec_stream_t* stream);
nodec_bstream_t* nodec_zstream_alloc_ex(nodec_stream_t* stream, int compress_level, bool gzip);

#endif

///@}


/*
void        nodec_handle_free(uv_handle_t* handle);
void        nodec_stream_free(uv_stream_t* stream);
void        nodec_stream_freev(lh_value streamv);
void        async_shutdown(uv_stream_t* stream);
 
#define using_stream(s) \
    defer_exit(async_shutdown(s),&nodec_stream_freev,lh_value_ptr(s))

// Configure a stream for reading; Normally not necessary to call explicitly
// unless special configuration needs exist.
void        nodec_read_start(uv_stream_t* stream, size_t read_max, size_t alloc_init, size_t alloc_max);
void        nodec_read_stop(uv_stream_t* stream);
void        nodec_read_restart(uv_stream_t* stream);
// Set the maximal amount of bytes that can be read from a stream (4Gb default)
void        nodec_set_read_max(uv_stream_t* stream, size_t read_max);

// Reading from a stream. `async_read_buf` is most efficient.
uv_buf_t    async_read_buf(uv_stream_t* stream);
uv_buf_t    async_read_buf_available(uv_stream_t* stream);
uv_buf_t    async_read_buf_line(uv_stream_t* stream);
uv_buf_t    async_read_buf_all(uv_stream_t* stream);
size_t      async_read_into_all(uv_stream_t* stream, uv_buf_t buf, bool* at_eof);
// Read a buffer from a stream up to and including the first occurrence of `pat`; the pattern
// can be at most 8 bytes long. Returns the number of bytes read in `*idx` (if not NULL).
uv_buf_t    async_read_buf_including(uv_stream_t* stream, size_t* idx, const void* pat, size_t pat_len);

char*       async_read(uv_stream_t* stream);
char*       async_read_all(uv_stream_t* stream);
char*       async_read_line(uv_stream_t* stream);

// Writing to a stream. `async_write_bufs` is most primitive.
void        async_write(uv_stream_t* stream, const char* s);
void        async_write_bufs(uv_stream_t* stream, uv_buf_t bufs[], size_t buf_count);
void        async_write_strs(uv_stream_t* stream, const char* strings[], size_t string_count );
void        async_write_buf(uv_stream_t* stream, uv_buf_t buf);

*/

/* ----------------------------------------------------------------------------
  IP4 and IP6 Addresses
-----------------------------------------------------------------------------*/

void nodec_ip4_addr(const char* ip, int port, struct sockaddr_in* addr);
void nodec_ip6_addr(const char* ip, int port, struct sockaddr_in6* addr);

// Define `name` as an ip4 or ip6 address
#define define_ip4_addr(ip,port,name)  \
  struct sockaddr_in name##_ip4; nodec_ip4_addr(ip,port,&name##_ip4); \
  struct sockaddr* name = (struct sockaddr*)&name##_ip4;

#define define_ip6_addr(ip,port,name)  \
  struct sockaddr_in name##_ip6; nodec_ip6_addr(ip,port,&name##_ip6); \
  struct sockaddr* name = (struct sockaddr*)&name##_ip6;

void nodec_sockname(const struct sockaddr* addr, char* buf, size_t bufsize);

void async_getnameinfo(const struct sockaddr* addr, int flags, char** node, char** service);
struct addrinfo* async_getaddrinfo(const char* node, const char* service, const struct addrinfo* hints);

void nodec_free_addrinfo(struct addrinfo* info);
void nodec_free_addrinfov(lh_value infov);
#define using_addrinfo(name)  defer(nodec_free_addrinfov,lh_value_ptr(name))


/* ----------------------------------------------------------------------------
  TCP
-----------------------------------------------------------------------------*/

/// \defgroup tcpx Low level TCP connections
/// @{

typedef struct _channel_t tcp_channel_t;
void            channel_freev(lh_value vchannel);
#define using_tcp_channel(ch)  defer(channel_freev,lh_value_ptr(ch))

uv_tcp_t*       nodec_tcp_alloc();
void            nodec_tcp_free(uv_tcp_t* tcp);
void            nodec_tcp_freev(lh_value tcp);

void            nodec_tcp_bind(uv_tcp_t* handle, const struct sockaddr* addr, unsigned int flags);
tcp_channel_t*  nodec_tcp_listen(uv_tcp_t* tcp, int backlog, bool channel_owns_tcp);
uv_stream_t*    async_tcp_channel_receive(tcp_channel_t* ch);

/// @}

/// \defgroup tcp TCP connections
/// @{

/// Establish a TCP connection.
/// \param addr   Connection address. 
/// \param host   Host name, only used for error messages and can be `NULL`.
nodec_bstream_t* async_tcp_connect_at(const struct sockaddr* addr, const char* host /* can be NULL, used for errors */);

/// Establish a TCP connection.
/// \param host     Host address. 
/// \param service  Can be a port (`"8080"`) or service (`"https"`). Uses `"http"` when `NULL`.
nodec_bstream_t* async_tcp_connect_at_host(const char* host, const char* service /*NULL="http"*/);

/// Establish a TCP connection.
/// \param host     Host address as a url, like `"http://www.bing.com"`.
nodec_bstream_t* async_tcp_connect(const char* host);

// TCP server

/// TCP Server configuration options.
typedef struct _tcp_server_config_t {
  int       backlog;           ///< maximal pending request queue length (default 8).
  int       max_interleaving;  ///< maximal number concurrent requests (default 512).
  uint64_t  timeout;           ///< ms between connection requests allowed; 0 = infinite
  uint64_t  timeout_total;     ///< total allowed connection time in ms; 0 = infinite
} tcp_server_config_t;

/// Default TCP server configuration.
#define tcp_server_config()    { 0, 0, 0, 0 }

/// The server callback when listening on a TCP connection.
/// \param id       The identity of the current asynchronous strand.
/// \param client   The connecting client data stream.
/// \param arg      The `lh_value` passed from async_tcp_server_at().
typedef void    (nodec_tcp_servefun)(int id, nodec_bstream_t* client, lh_value arg);

/// Create a TCP server.
///
/// \param addr       The socket address to serve.
/// \param config     The TCP server configuration, can be `NULL` in which case tcp_server_config() is used.
/// \param servefun   The callback called when a client connects.
/// \param on_exn     Optional function that is called when an exception happens in `servefun`.
/// \param arg        Optional argument to pass on to `servefun`, can be `lh_value_null`.
void async_tcp_server_at(const struct sockaddr* addr, tcp_server_config_t* config, nodec_tcp_servefun* servefun, lh_actionfun* on_exn, lh_value arg);

/// @}



/* ----------------------------------------------------------------------------
  HTTP
-----------------------------------------------------------------------------*/
typedef enum http_status http_status_t;
typedef enum http_method http_method_t;

typedef struct _http_in_t http_in_t;
typedef struct _http_out_t http_out_t;

void throw_http_err(http_status_t status);
void throw_http_err_str(http_status_t status, const char* msg);
void throw_http_err_strdup(http_status_t status, const char* msg);
const char* nodec_http_status_str(http_status_t code);
const char* nodec_http_method_str(http_method_t method);


// HTTP(S) server

typedef void (nodec_http_servefun)();

void async_http_server_at(const char* host, tcp_server_config_t* config, nodec_http_servefun* servefun);


// HTTP(S) connection

typedef lh_value (http_connect_fun)(http_in_t* in, http_out_t* out, lh_value arg);

lh_value async_http_connect(const char* url, http_connect_fun* connectfun, lh_value arg);


/* ----------------------------------------------------------------------------
  HTTP incoming connection
-----------------------------------------------------------------------------*/

void   http_in_clear(http_in_t* in);
void   http_in_clearv(lh_value inv);

// Initialize a incoming connection at a server or client by reading the headers block
size_t async_http_in_init(http_in_t* in, uv_stream_t* stream, bool is_request );

// Query the incoming connection
const char*   http_in_url(http_in_t* in);            // server
http_method_t http_in_method(http_in_t* in);         // server
http_status_t   http_in_status(http_in_t* in);       // client
uint64_t      http_in_content_length(http_in_t* in); 
const char*   http_in_header(http_in_t* in, const char* name);
const char*   http_in_header_next(http_in_t* in, const char** value, size_t* iter);

// Read comma separated header fields; `*iter` should start as NULL.
// Returns NULL when done.
const char* http_header_next_field(const char* header, size_t* len, const char** iter);

// use this on connections to wait for a response
size_t async_http_in_read_headers(http_in_t* in);

// Read HTTP body in parts; returned buffer is only valid until the next read
// Returns a null buffer when the end is reached.
uv_buf_t      async_http_in_read_body_buf(http_in_t* in);

// Read the full body; the returned buf should be deallocated by the caller.
// Pass `initial_size` 0 to automatically use the content-length or initially
// received buffer size.
uv_buf_t      async_http_in_read_body(http_in_t* in, size_t initial_size);


/*-----------------------------------------------------------------
  HTTP outgoing
-----------------------------------------------------------------*/

void http_out_init(http_out_t* out, nodec_stream_t* stream);
void http_out_init_server(http_out_t* out, nodec_stream_t* stream, const char* server_name);
void http_out_init_client(http_out_t* out, nodec_stream_t* stream, const char* host_name);

void http_out_clear(http_out_t* out);
void http_out_clearv(lh_value respv);

#define using_http_out(stream,out)  http_out_t _resp; http_out_t* out = &_resp; http_out_init(out,stream); defer(http_out_clearv,lh_value_any_ptr(out))

// Add headers
void http_out_add_header(http_out_t* out, const char* field, const char* value);

// Send the headers
void http_out_send_headers(http_out_t* out, const char* prefix, const char* postfix);
void http_out_send_status_headers(http_out_t* out, http_status_t status, bool end);
void http_out_send_request_headers(http_out_t* out, http_method_t method, const char* url, bool end);

// Send full body at once
void http_out_send_body_bufs(http_out_t* out, uv_buf_t bufs[], size_t count, const char* content_type);
void http_out_send_body_buf(http_out_t* out, uv_buf_t buf, const char* content_type);
void http_out_send_body(http_out_t* out, const char* s, const char* content_type);

// Send chunked up body
void http_out_send_chunked_start(http_out_t* out, const char* content_type);
void http_out_send_chunk_bufs(http_out_t* out, uv_buf_t bufs[], size_t count);
void http_out_send_chunk_buf(http_out_t* out, uv_buf_t buf);
void http_out_send_chunk(http_out_t* out, const char* s);
void http_out_send_chunked_end(http_out_t* out);


/*-----------------------------------------------------------------
  HTTP server implicitly bound request and response
-----------------------------------------------------------------*/

int         http_strand_id();
http_in_t*  http_req();
http_out_t* http_resp();

void http_resp_add_header(const char* field, const char* value);
void http_resp_send(http_status_t status, const char* body /* can be NULL */, const char* content_type);
void http_resp_send_ok();
void http_resp_send_bufs(http_status_t status, uv_buf_t bufs[], size_t count, const char* content_type);
void http_resp_send_buf(http_status_t status, uv_buf_t buf, const char* content_type);

const char*   http_req_url();
const char*   http_req_path();
http_method_t http_req_method();
uint64_t      http_req_content_length();
const char*   http_req_header(const char* name);
const char*   http_req_header_next(const char** value, size_t* iter);

// Read HTTP body in parts; returned buffer is only valid until the next read
// Returns a null buffer when the end is reached.
uv_buf_t      async_req_read_body_buf();

// Read the full body; the returned buf should be deallocated by the caller.
// Pass `initial_size` 0 to automatically use the content-length or initially
// received buffer size.
uv_buf_t      async_req_read_body_all(size_t initial_size);

// Read the full body as a string. Only works if the body cannot contain 0 characters.
const char*   async_req_read_body();



/* ----------------------------------------------------------------------------
   URL's
-----------------------------------------------------------------------------*/
typedef struct _nodec_url_t nodec_url_t;

void nodec_url_free(nodec_url_t* url);
void nodec_url_freev(lh_value urlv);

nodec_url_t*  nodecx_parse_url(const char* url);
nodec_url_t*  nodecx_parse_host(const char* host);
nodec_url_t*  nodec_parse_url(const char* url);
nodec_url_t*  nodec_parse_host(const char* host);

#define using_url(url)        defer(nodec_url_freev,lh_value_ptr(url))
#define usingx_url(url)       defer(nodec_url_freev,lh_value_ptr(url))

const char* nodec_url_schema(const nodec_url_t* url);
const char* nodec_url_host(const nodec_url_t* url);
const char* nodec_url_path(const nodec_url_t* url);
const char* nodec_url_query(const nodec_url_t* url);
const char* nodec_url_fragment(const nodec_url_t* url);
const char* nodec_url_userinfo(const nodec_url_t* url);
const char* nodec_url_port_str(const nodec_url_t* url);
uint16_t    nodec_url_port(const nodec_url_t* url);
bool        nodec_url_is_ip6(const nodec_url_t* url);


/* ----------------------------------------------------------------------------
  Mime types
-----------------------------------------------------------------------------*/

const char* nodec_mime_info_from_fname(const char* fname, bool* compressible, const char** charset);
const char* nodec_mime_from_fname(const char* fname);
void        nodec_info_from_mime(const char* mime_type, const char** preferred_ext, bool* compressible, const char** charset);
const char* nodec_ext_from_mime(const char* mime_type);

/* ----------------------------------------------------------------------------
  HTTP Static web server
-----------------------------------------------------------------------------*/

typedef struct _http_static_config_t {
  bool use_etag;
  bool use_implicit_index_html;
  bool use_implicit_html_ext;
  const char* cache_control;
  size_t max_content_size;
  size_t min_chunk_size;
} http_static_config_t;

#define http_static_default_config() { true, true, true, "public, max-age=604800", 0, 64*1024 }

// Serve static files under a `root` directory. `config` can be NULL for the default configuration.
void http_serve_static(const char* root, const http_static_config_t* config);

/* ----------------------------------------------------------------------------
  TTY
-----------------------------------------------------------------------------*/

lh_value _nodec_tty_allocv();
void     _nodec_tty_freev(lh_value ttyv);

implicit_declare(tty)

#define using_tty()  \
    using_implicit_defer_exit(async_tty_shutdown(),_nodec_tty_freev,_nodec_tty_allocv(),tty)

char* async_tty_readline();
void  async_tty_write(const char* s);
void  async_tty_shutdown();


/* ----------------------------------------------------------------------------
  Main entry point
-----------------------------------------------------------------------------*/

typedef void (nodec_main_fun_t)();

uv_errno_t  async_main( nodec_main_fun_t* entry );




/* ----------------------------------------------------------------------------
  Safe allocation
  These raise an exception on failure
  The code:
    {using_alloc(mystruct,name){
      ...
    }}
  will safely allocate a `mystruct*` to `name` which can be used inside `...`
  and will be deallocated safely if an exception is thrown or when exiting
  the block scope.
-----------------------------------------------------------------------------*/

#if defined(_MSC_VER) && defined(_DEBUG)
// Enable debugging logs on msvc 
# undef _malloca // suppress warning
# define _CRTDBG_MAP_ALLOC
# include <crtdbg.h>
# define nodecx_malloc  malloc
# define nodecx_calloc  calloc
# define nodecx_realloc realloc
# define nodecx_free    free
# define nodec_malloc(sz)     (check_nonnull(malloc(sz)))
# define nodec_calloc(n,sz)   (check_nonnull(calloc(n,sz)))
# define nodec_realloc(p,sz)  (check_nonnull(realloc(p,sz)))
# define nodec_free           _nodec_free
#else
# define nodecx_malloc  _nodecx_malloc
# define nodecx_calloc  _nodecx_calloc
# define nodecx_realloc _nodecx_realloc
# define nodecx_free    free
# define nodec_malloc  _nodec_malloc
# define nodec_calloc  _nodec_calloc
# define nodec_realloc _nodec_realloc
# define nodec_free    _nodec_free
#endif

void  nodec_register_malloc(lh_mallocfun* _malloc, lh_callocfun* _calloc, lh_reallocfun* _realloc, lh_freefun* _free);
void  nodec_check_memory();
void* check_nonnull(void* p);   // throws on a non-null pointer

void* _nodecx_malloc(size_t size);
void* _nodecx_calloc(size_t count, size_t size);
void* _nodecx_realloc(void* p, size_t newsize);
void* _nodec_malloc(size_t size);
void* _nodec_calloc(size_t count, size_t size);
void* _nodec_realloc(void* p, size_t newsize);
void  _nodec_free(const void* p);

void  nodec_freev(lh_value p);
char* nodec_strdup(const char* s);
char* nodec_strndup(const char* s, size_t max);
const void* nodec_memmem(const void* src, size_t src_len, const void* pat, size_t pat_len);

#define nodecx_alloc(tp)          ((tp*)(nodecx_malloc(sizeof(tp))))
#define nodecx_zero_alloc(tp)     ((tp*)(nodecx_calloc(1,sizeof(tp))))

#define nodec_alloc(tp)           ((tp*)(nodec_malloc(sizeof(tp))))
#define nodec_alloc_n(n,tp)       ((tp*)(nodec_malloc((n)*sizeof(tp))))
#define nodec_zero_alloc_n(n,tp)  ((tp*)(nodec_calloc(n,sizeof(tp))))
#define nodec_zero_alloc(tp)      nodec_zero_alloc_n(1,tp)
#define nodec_realloc_n(p,n,tp)   ((tp*)(nodec_realloc(p,(n)*sizeof(tp))))

#define using_free(name)               defer(nodec_freev,lh_value_ptr(name))
#define using_alloc(tp,name)           tp* name = nodec_alloc(tp); using_free(name)
#define using_alloc_n(n,tp,name)       tp* name = nodec_alloc_n(n,tp); using_free(name)
#define using_zero_alloc_n(n,tp,name)  tp* name = nodec_zero_alloc_n(n,tp); using_free(name)
#define using_zero_alloc(tp,name)      using_zero_alloc_n(1,tp,name)

#define nodec_zero(tp,ptr)        memset(ptr,0,sizeof(tp));


#endif // __nodec_h
