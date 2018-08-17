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
#include <fcntl.h>
#include <time.h>

/*! \mainpage

This is the API documentation of the
<a class="external" href="https://github.com/koka-lang/nodec">NodeC</a> project

See the <a class="external" href="./modules.html">Modules</a> section for an
overview of the API functionality.

\b Naming:
- `async_` : for asynchronous functions that might interleave
- `nodec_` : synchronous functions that might throw exeptions or use other effects.
- `nodecx_`: synchronous functions that return an explicit error result.
- `<tp>_t`  : for types
- `using_`  : for scoped combinators, to be used with double curly braces:
              i.e. `{using_alloc(tp,name){ ... <use name> ... }}`
- `lh_value`: an #lh_value is used to mimic polymorphism. There are a host of 
              conversion functions, like lh_ptr_value() (from value to pointer)
              or lh_int_value() (from value to an int).

.
*/

// Forward declarations 
typedef struct _channel_t channel_t;

/// \addtogroup nodec_various
/// \{

/// 1 KB.
#define NODEC_KB  (1024)
/// 1 MB.
#define NODEC_MB  (1024*NODEC_KB)
/// 1 GB
#define NODEC_GB  (1024*NODEC_MB)

/// Throw an error, either an `errno` or `uv_errno_t`.
/// \param err error code.
void nodec_throw(int err);

/// Throw an error, either an `errno` or `uv_errno_t`.
/// \param err error code.
/// \param msg custom message.
void nodec_throw_msg(int err, const char* msg);

/// Does a string start with a prefix?
/// \param s The string to analyze, can be NULL, in which case `false` is returned.
/// \param prefix The prefix to compare, can be NULL, in which case `false` is returned.
/// \returns True if the string starts with `prefix`.
bool nodec_starts_with(const char* s, const char* prefix);

/// Does a string start with a prefix?
/// Uses case-insensitive comparison.
/// \param s The string to analyze, can be NULL, in which case `false` is returned.
/// \param prefix The prefix to compare, can be NULL, in which case `false` is returned.
/// \returns True if the string starts with `prefix`.
bool nodec_starts_withi(const char* s, const char* prefix);

/// Does a string end with a postfix?
/// \param s The string to analyze, can be NULL, in which case `false` is returned.
/// \param postfix The postfix to compare, can be NULL, in which case `false` is returned.
/// \returns True if the string ends with `postfix`.
bool nodec_ends_with(const char* s, const char* postfix);

/// Does a string end with a postfix?
/// Uses case-insensitive comparison.
/// \param s The string to analyze, can be NULL, in which case `false` is returned.
/// \param postfix The postfix to compare, can be NULL, in which case `false` is returned.
/// \returns True if the string ends with `postfix`.
bool nodec_ends_withi(const char* s, const char* postfix);

/// Get the current time as a RFC1123 internet date.
/// Caches previous results to increase efficiency.
/// \returns The current time in <a href="https://tools.ietf.org/html/rfc1123#page-55">RFC1123</a> format, 
/// for example `Thu, 01 Jan 1972 00:00:00 GMT`.
const char* nodec_inet_date_now();

/// Format a time as a RFC1123 internet date.
/// Caches previous results to increase efficiency.
/// \param now The time to convert.
/// \returns The current time in <a href="https://tools.ietf.org/html/rfc1123#page-55">RFC1123</a> format, 
/// for example `Thu, 01 Jan 1972 00:00:00 GMT`.
const char* nodec_inet_date(time_t now);

/// Parse a RFC1123 internet date.
/// \param date  A RFC1123 formatted internet date, for example `Thu, 01 Jan 1972 00:00:00 GMT`. Can be NULL which is treated as an invalid date.
/// \param[out] t Set to the time according to `date`. If the `date` was not valid, the UNIX epoch is used.
/// \returns True if successful.
bool nodec_parse_inet_date(const char* date, time_t* t);

/// \}


/* ----------------------------------------------------------------------------
Buffers are `uv_buf_t` which contain a `base` pointer and the
available `len` bytes. These buffers are usually passed by value.
-----------------------------------------------------------------------------*/

/// \defgroup buffers Buffer's
/// Memory buffers.
/// Buffers are the `libuv` #uv_buf_t` structure which contain 
/// a pointer to the data and their length.
/// Buffer structures are small and always passed and returned _by value_.
///
/// The routines in this module ensure that all buffers actually are allocated
/// with `len + 1` where `base[len] == 0`, i.e. it is safe to print
/// the contents as a string as it will be zero terminated.
/// \{

/// Initialize a libuv buffer, which is a record with a data pointer and its length.
uv_buf_t nodec_buf(const void* base, size_t len);

/// Initialize a buffer from a string.
uv_buf_t nodec_buf_str(const char* s);

/// Initialize a buffer from a string.
/// This (re)allocates the string in the heap which is then owned by the buffer.
uv_buf_t nodec_buf_strdup(const char* s);

/// Create a null buffer, i.e. `nodec_buf(NULL,0)`.
uv_buf_t nodec_buf_null();

/// Create and allocate a buffer of a certain length.
uv_buf_t nodec_buf_alloc(size_t len);

/// Reallocate a buffer to a given new length.
/// See also nodec_buf_fit() and nodec_buf_ensure().
uv_buf_t nodec_buf_realloc(uv_buf_t buf, size_t len);

/// Ensure a buffer has at least `needed` size.
/// \param  buf The buffer to potentially reallocate. If `buf` is null, 
///             use 8kb for initial size, and after that use doubling of at most 
///             4mb increments.
/// \param needed the required size of the buffer.
/// \returns a new buffer that has at least `needed` length.
uv_buf_t nodec_buf_ensure(uv_buf_t buf, size_t needed);

/// Ensure a buffer has at least `needed` size.
/// Like nodec_buf_ensure() but with customizable `initial_size` and `max_increase`.
uv_buf_t nodec_buf_ensure_ex(uv_buf_t buf, size_t needed, size_t initial_size, size_t max_increase);

/// Fit a buffer to `needed` size. 
/// Might increase the buffer size, or
/// Reallocate to a smaller size if it was larger than 128 bytes with more than 
/// 20% wasted space.
/// \param buf   the buffer to fit.
/// \param needed the required space needed.
/// \returns the new potentially reallocated buffer with a length that fits `needed`.
uv_buf_t nodec_buf_fit(uv_buf_t buf, size_t needed);

/// Append the contents of `buf2` into `buf1`.
/// \param buf1 the destination buffer.
/// \param buf2 the source buffer.
/// \returns a potentially resized `buf1` that contains the contents of both.
uv_buf_t nodec_buf_append_into(uv_buf_t buf1, uv_buf_t buf2);


/// Is this a null buffer?.
/// \param buf the buffer.
/// \returns `true` if the buffer length is 0 or the `base` is `NULL`.
bool nodec_buf_is_null(uv_buf_t buf);

/// Free a buffer.
/// This is usually not used directly, but through using_buf().
void nodec_buf_free(uv_buf_t buf);

/// Free a buffer by reference.
/// This is usually not used directly, but through using_buf().
void nodec_bufref_free(uv_buf_t* buf);

/// Free a buffer by reference value.
/// This is usually not used directly, but through using_buf().
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
#define using_buf(bufref)  defer(nodec_bufref_freev,lh_value_any_ptr(bufref))

/// Use a buffer in a scope, freeing automatically only when an exception is thrown.
/// Pass the buffer by reference so it will free
/// the final memory that `bufref->base` points to (and not the initial value).
/// \param bufref  a reference to the buffer to free after use.
#define using_buf_on_abort_free(bufref)   on_abort(nodec_bufref_freev,lh_value_any_ptr(bufref))

/// Don't free a buffer by reference value.
/// Usually not used directly, but used by using_buf_owned()
void nodec_bufref_nofreev(lh_value pv);

/// Use a potentially _owned_ buffer in a scope.
/// This is used for the `async_read_bufx()` calls where the returned buffer
/// might be owned (and must be freed), or not (and must not be freed).
/// Pass the buffer by reference so it will free
/// the final memory that `bufref->base` points to (and not the initial value).
/// \param owned   Only free the buffer if `owned` is true.
/// \param bufref  a reference to the buffer to free after use.
#define using_buf_owned(owned,bufref)  defer((owned ? nodec_bufref_freev : nodec_bufref_nofreev),lh_value_any_ptr(bufref))

/// \}



/* ----------------------------------------------------------------------------
  Cancelation scope
-----------------------------------------------------------------------------*/

// private
implicit_declare(_cancel_scope)
lh_value _cancel_scope_alloc();

/// \defgroup nodec_async Asynchrony
/// Asynchronous utility functions.
/// \{

/// Execute under a cancelation scope.
/// \sa async_scoped_cancel()
#define using_cancel_scope()        using_implicit_defer(nodec_freev,_cancel_scope_alloc(),_cancel_scope)

/// Asynchronously cancel all outstanding requests under the same cancelation scope.
/// This is a powerful primitive that enables cancelation 
/// of operations and for example enables timeouts over any composition of
/// asynchronous operations. When async_scoped_cancel() is called, it cancels
/// all outstanding (interleaved) operations under the same innermost cancelation
/// scope (raising a cancel exception for all of them).
void async_scoped_cancel();

/// Is the current scope canceled?
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

/// \}


/* ----------------------------------------------------------------------------
  File system (fs)
-----------------------------------------------------------------------------*/

/// \defgroup nodec_fs File System
/// Asynchronous access to the file system.
/// \{

uv_errno_t  asyncx_stat(const char* path, uv_stat_t* stat);
uv_stat_t   async_stat(const char* path);
uv_stat_t   async_fstat(uv_file file);

/// Open a file.
/// \param path The file path
/// \param flags The file open flags, see <a href="https://linux.die.net/man/2/open">open2()</a>.
/// \param mode The file open mode, see <a href="https://linux.die.net/man/2/open">open2()</a>.
/// \returns The opened file handle on success.
uv_file     async_fopen(const char* path, int flags, int mode);

/// Open a file.
/// Returns an error code instead of throwing an exception.
/// \param path The file path
/// \param flags The file open flags, see <a href="https://linux.die.net/man/2/open">open2()</a>.
/// \param mode The file open mode, see <a href="https://linux.die.net/man/2/open">open2()</a>.
/// \param[out] file Set to the file handle on success
/// \returns A possible error code, or 0 on succes.
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

/// Type of scoped file functions.
typedef lh_value(nodec_file_fun)(uv_file file, const char* path, lh_value arg);

/// Safely open a file, use it, and close it again.
/// \param path The file path
/// \param flags The file open flags, see <a href="https://linux.die.net/man/2/open">open2()</a>.
/// \param mode The file open mode, see <a href="https://linux.die.net/man/2/open">open2()</a>.
/// \param action The file open action: called with the opened file handle.
/// \param arg Extra argument passed to `action`.
/// \returns The return value of `action`.
lh_value    using_async_fopen(const char* path, int flags, int mode, nodec_file_fun* action, lh_value arg);

/// Safely open a file, use it, and close it again.
/// Does not throw an exception on a file open error but returns an `uv_errno_t` instead.
/// \param path The file path
/// \param flags The file open flags, see <a href="https://linux.die.net/man/2/open">open2()</a>.
/// \param mode The file open mode, see <a href="https://linux.die.net/man/2/open">open2()</a>.
/// \param action The file open action: called with the opened file handle.
/// \param arg Extra argument passed to `action`.
/// \param[out] result The return value of `action`.
/// \returns A possible error code or 0 on success.
uv_errno_t  using_asyncx_fopen(const char* path, int flags, int mode, nodec_file_fun* action, lh_value arg, lh_value* result);

/// \}

/* ----------------------------------------------------------------------------
  Streams
-----------------------------------------------------------------------------*/
///\defgroup streams Streams
/// Streaming data.
/// The main interfaces are plain streams as #nodec_stream_t and
/// buffered streams #nodec_bstream_t.
///\{

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
/// This closes the stream gracefully for writes (nodec_stream_free()
/// does this too but closes the stream abruptly).
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
/// This is the most efficient form of reading since it can even read by providing
/// _unowned_ view buffers but it is also most complex as returned unowned buffers are
/// only valid until the next read call. See async_read_buf() for more robust 
/// reading that always returns callee owned buffers.
/// \param stream  stream to read from.
/// \param[out] buf_owned this is set to `true` if the returned buffer is _owned_, i.e. should be freed by the callee.
/// \returns a buffer with the data. This buffer is becomes
///    the callee's responsibility if `buf_owned` is true; otherwise the buffer should
///    _not_ be freed by the callee. See using_buf_owned() to do this automatically.
///    Moreover, if a buffer is returned with `*buf_owned` false, the buffer only
///    stays valid until the next read from that stream. 
///
/// \b Example
/// ```
/// nodec_stream_t stream = http_req_body();
/// {using_stream(stream){
///    uv_buf_t buf;
///    bool owned;
///    while( (buf = async_read_bufx(stream,&owned), !nodec_buf_is_null(buf)) ) {
///      {using_buf_owned(owned,buf){
///         printf("data read: %s\n", buf.base);
///      }}
///    }
/// }}
/// ```
uv_buf_t  async_read_bufx(nodec_stream_t* stream, bool* buf_owned);

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
/// \param bstream   the stream to read from.
/// \param read_max  maximum number of bytes to read. Use 0 (or `SIZE_MAX`) for unlimited.
/// \returns the data read in a callee owned buffer (see using_buf()).
uv_buf_t  async_read_buf_all(nodec_bstream_t* bstream, size_t read_max);

/// Read the entire stream as a string.
/// \param bstream   the stream to read from.
/// \param read_max  maximum number of bytes to read. Use 0 (or `SIZE_MAX`) for unlimited.
/// \returns the data read in a callee owned string (see using_str()).
char*     async_read_all(nodec_bstream_t* bstream, size_t read_max);

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

/// Push back a buffer onto a read stream. 
/// The data pushed back will be read on the next read call.
void nodec_pushback_buf(nodec_bstream_t* bstream, uv_buf_t buf);

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
/// \param alloc_init the initial allocation size for read buffers. Use 0 for default (8k). Doubles on further data until `alloc_max`.
/// \param alloc_max  the maximal allocation size for read buffers.
/// \returns a buffered stream.
nodec_bstream_t* nodec_bstream_alloc_read_ex(uv_stream_t* stream, size_t alloc_init, size_t alloc_max);


#ifndef NO_ZLIB

/// Create a stream over another stream that is gzip'd.
/// Reading unzips, while writing zips again.
/// Uses a default compression level of 6 and gzip format (instead of deflate).
/// \param stream the gzipped stream -- after allocation the returned stream
///               owns this stream and takes care of shutdown and free.
/// \returns a new buffered stream that automatically (de)compresses.
nodec_bstream_t* nodec_zstream_alloc(nodec_stream_t* stream);


/// Create a stream over another stream that is gzip'd.
/// Reading unzips automatically, while writing zips again.
/// \param stream the gzipped stream -- after allocation the returned stream
///               owns this stream and takes care of shutdown and free.
/// \param compress_level  the compression level to use between 1 and 9 (6 by default, which
///                         is a good balance between speed and compression ratio).
/// \param gzip   if `true` uses the gzip format for compression while reading both
///               deflate and gzip streams. If false, uses deflate for both compression
///               and decompression.
/// \returns a new buffered stream that automatically (de)compresses.
nodec_bstream_t* nodec_zstream_alloc_ex(nodec_stream_t* stream, int compress_level, bool gzip);

#endif

/// \}


/* ----------------------------------------------------------------------------
  IP4 and IP6 Addresses
-----------------------------------------------------------------------------*/

/// \defgroup nodec_ip IP addresses
/// Raw IP4 or IP6 addresses.
/// \{
void nodec_ip4_addr(const char* ip, int port, struct sockaddr_in* addr);
void nodec_ip6_addr(const char* ip, int port, struct sockaddr_in6* addr);

/// Define `name` as an ip4 address
#define define_ip4_addr(ip,port,name)  \
  struct sockaddr_in name##_ip4; nodec_ip4_addr(ip,port,&name##_ip4); \
  struct sockaddr* name = (struct sockaddr*)&name##_ip4;

/// Define `name` as an ip4 address
#define define_ip6_addr(ip,port,name)  \
  struct sockaddr_in name##_ip6; nodec_ip6_addr(ip,port,&name##_ip6); \
  struct sockaddr* name = (struct sockaddr*)&name##_ip6;

void nodec_sockname(const struct sockaddr* addr, char* buf, size_t bufsize);

void async_getnameinfo(const struct sockaddr* addr, int flags, char** node, char** service);
struct addrinfo* async_getaddrinfo(const char* node, const char* service, const struct addrinfo* hints);

void nodec_free_addrinfo(struct addrinfo* info);
void nodec_free_addrinfov(lh_value infov);
#define using_addrinfo(name)  defer(nodec_free_addrinfov,lh_value_ptr(name))
/// \}

/* ----------------------------------------------------------------------------
URL's
-----------------------------------------------------------------------------*/
/// \defgroup nodec_url URL
/// URL parsing.
/// \{

/// Structured URL
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

/// \}


/* ----------------------------------------------------------------------------
  Mime types
-----------------------------------------------------------------------------*/
/// \defgroup nodec_mime MIME Types.
/// MIME type functionality.
/// The default MIME information is generated from the
/// <a href="https://www.npmjs.com/package/mime-db">mime-db</a> project.
/// \{

/// Mime info from a filename.
/// \param fname The file name.
/// \param[out] compressible Set to `true` if this kind of file can be further compressed by gzip.
/// \param[out] charset      Set to the default character set for this MIME type. (do not free).
/// \returns The MIME type. (do not free).
const char* nodec_mime_info_from_fname(const char* fname, bool* compressible, const char** charset);

/// Mime name from a file name.
/// \param fname The file name.
/// \returns The MIME type. (do not free).
const char* nodec_mime_from_fname(const char* fname);

/// Information for a MIME type.
/// \param mime_type The MIME type.
/// \param[out] preferred_ext  The preferred extension.
/// \param[out] compressible Set to `true` if this kind of file can be further compressed by gzip.
/// \param[out] charset      Set to the default character set for this MIME type. (do not free).
void        nodec_info_from_mime(const char* mime_type, const char** preferred_ext, bool* compressible, const char** charset);

/// The preferred extension for a MIME type.
/// \param mime_type The MIME type.
/// \returns The preferrred extensions (do not free).
const char* nodec_ext_from_mime(const char* mime_type);

/// \}


/* ----------------------------------------------------------------------------
  TCP
-----------------------------------------------------------------------------*/
/// \defgroup tcp TCP Connections
/// Communication over the TCP protocol (reliable streaming).
/// \{

/// \defgroup tcpx Low level TCP Connections
/// Low level functions to access TCP.
/// \{

typedef struct _channel_t tcp_channel_t;
void            channel_freev(lh_value vchannel);
#define using_tcp_channel(ch)  defer(channel_freev,lh_value_ptr(ch))

uv_tcp_t*       nodec_tcp_alloc();
void            nodec_tcp_free(uv_tcp_t* tcp);
void            nodec_tcp_freev(lh_value tcp);

void            nodec_tcp_bind(uv_tcp_t* handle, const struct sockaddr* addr, unsigned int flags);
tcp_channel_t*  nodec_tcp_listen(uv_tcp_t* tcp, int backlog, bool channel_owns_tcp);
uv_stream_t*    async_tcp_channel_receive(tcp_channel_t* ch);

/// \}


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

/// \}



/* ----------------------------------------------------------------------------
  HTTP
-----------------------------------------------------------------------------*/
/// \defgroup http HTTP Connections
/// Communication over the HTTP 1.1 protocol.
/// \{


/// \addtogroup http_in_out 
/// \{

/// HTTP incoming data object
typedef struct _http_in_t http_in_t;

/// HTTP outgoing data object
typedef struct _http_out_t http_out_t;

/// \}


/// \defgroup http_setup HTTP Setup
/// Initiating HTTP servers or clients.
/// \{
typedef enum http_status http_status_t;
typedef enum http_method http_method_t;

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

/// \}

/* ----------------------------------------------------------------------------
  HTTP incoming connection
-----------------------------------------------------------------------------*/
/// \defgroup http_in_out HTTP Input and Output.
/// HTTP incoming and outgoing data.
/// These are the request- and response objects for the server,
/// and the response- and request objects for a client connection.
/// Servers bind these implicitly to http_req() and http_resp() for
/// the current request- and response objects so that the explicit
/// #http_in_t and #http_out_t objects don't need to be threaded around
/// explicitly.
/// \{

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
bool          http_in_header_contains(http_in_t* req, const char* name, const char* pattern);

// Read comma separated header fields; `*iter` should start as NULL.
// Returns NULL when done.
const char* http_header_next_field(const char* header, size_t* len, const char** iter);

// use this on connections to wait for a response
size_t async_http_in_read_headers(http_in_t* in);

/// Get a buffered stream to read the body.
/// \param in       the HTTP input.
/// \returns NULL if the stream has no body or was already completely read.
/// Use async_read_bufx() to read most efficiently without memory copies.
nodec_bstream_t* http_in_body(http_in_t* in);

/// Read the full body; the returned buf should be deallocated by the caller.
/// Uses `Content-Length` when possible to read into a pre-allocated buffer of the right size.
uv_buf_t async_http_in_read_body(http_in_t* in, size_t read_max);

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

// Send headers and get a possible body write stream

/// Use this as a `content_length` to use a _chunked_ transfer encoding
/// in http_out_send_status_body() and http_resp_send_status_body().
#define NODEC_CHUNKED ((size_t)(-1))

void             http_out_send_status(http_out_t* out, http_status_t status);
nodec_stream_t*  http_out_send_status_body(http_out_t* out, http_status_t status, size_t content_length, const char* content_type);
void             http_out_send_request(http_out_t* out, http_method_t method, const char* url);
nodec_stream_t*  http_out_send_request_body(http_out_t* out, http_method_t method, const char* url, size_t content_length, const char* content_type);

bool http_out_status_sent(http_out_t* out);
/// \}


/*-----------------------------------------------------------------
  HTTP server implicitly bound request and response
-----------------------------------------------------------------*/
/// \defgroup http_req_resp HTTP Server Requests and Responses.
/// Implicit bindings for HTTP incoming- and outgoing data for a server.
/// The HTTP(S) server implicitly binds the current request
/// and response objects (as dynamically bound implicit parameters).
/// They can be accessed through `http_req_xxx` for requests and
/// `http_resp_xxx` for responses.
/// \{

/// The identity of the current asynchronous strand serving the request.
int         http_strand_id();

/// Return the current HTTP request.
http_in_t*  http_req();

/// Return the current HTTP response.
http_out_t* http_resp();

/// Add a header to the current HTTP response.
/// \param name  the name of the header
/// \param value the value of the header
/// the name and value are copied into the header block and can be freed afterwards.
void            http_resp_add_header(const char* name, const char* value);


/// Return `true` if the HTTP response status was sent.
bool http_resp_status_sent();

/// Send the headers and the response status without a body.
/// \param status The HTTP response status.
void  http_resp_send_status(http_status_t status);

/// Send the headers and response status, and return a reponse body stream.
/// \param status  The HTTP response status
/// \param content_length  The length of the response body. Use #NODEC_CHUNKED for
///   a _chunked_ transfer encoding where the content length
///   is unknown ahead of time.
/// \param content_type    The mime content type (like `text/html`). The `charset`
///   can be specified too (like `text/html;charset=UTF-8`) but
///   is otherwise determined automatically from the mime type.
///   if the `content_type` is `NULL` no _Content-Type_ header
///   is added.
/// \returns A stream to write the body to; this is automatically chunked if
///   #NODEC_CHUNKED was passed as the `content_length`.
nodec_stream_t* http_resp_send_status_body(http_status_t status, size_t content_length, const char* content_type);

/// Send the headers and OK response without a body.
/// Just a shorthand for http_resp_send_status(HTTP_STATUS_OK).
void http_resp_send_ok();

/// Send headers, status, and full body as a buffer.
void http_resp_send_body_buf(http_status_t status, uv_buf_t buf, const char* content_type);

/// Send headers, status, and full body as a string.
void http_resp_send_body_str(http_status_t status, const char* body, const char* content_type);


/// Return the current request url.
const char*   http_req_url();

/// Return the parsed full request url, including the path, hash, query etc.
const nodec_url_t* http_req_parsed_url();


/// Return the current request path.
const char*   http_req_path();

/// Return the current request method.
http_method_t http_req_method();

/// The value of the _Content-Length_ header.
/// \returns 0 if the _Content-Length_ header was not present.
uint64_t      http_req_content_length();

/// Return the value of an HTTP request header.
/// \param name the header name.
/// \returns the header value (owned by the request object) or NULL
///  if the header was not present. If there were multiple headers
///  the values are joined with commas into one value. See http_header_next_field()
/// for iterating through them.
const char*   http_req_header(const char* name);

/// Iterate through all headers.
/// \param[out] value  the value of the current header, or NULL if done iterating.
/// \param[in,out] iter   the iteration state, should be initialized to 0.
/// \returns the name of the current header or NULL if done iterating.
const char*   http_req_header_next(const char** value, size_t* iter);

/// Return the current HTTP request body as a buffered stream.
/// Handles chunked bodies, and does automatic decompression if the 
/// `Content-Encoding` was `gzip`.
nodec_bstream_t* http_req_body();

/// Read the full HTTP request body.
/// The returned buffer should be deallocated by the caller.
/// \param read_max   maximum bytes to read (or 0 for unlimited reading)
/// \returns the buffer with the full request body.
/// Handles `chunked` request bodies, and does automatic decompression 
/// if the request body had a Content-Encoding of `gzip`.
uv_buf_t      async_req_read_body(size_t read_max);

/// Read the full body as a string. 
/// Only works if the body cannot contain 0 characters.
/// The returned string should be deallocated by the caller.
/// \param read_max   maximum bytes to read (or 0 for unlimited reading)
/// \returns the buffer with the full request body.
/// Handles `chunked` request bodies, and does automatic decompression 
/// if the request body had a Content-Encoding of `gzip`.
const char*   async_req_read_body_str(size_t read_max);

/// \}


/* ----------------------------------------------------------------------------
  HTTP Static web server
-----------------------------------------------------------------------------*/
/// \defgroup nodec_static HTTP Static Content Server.
/// Serve static content over HTTP 1.1.
/// \{

/// HTTP Static server configuration.
typedef struct _http_static_config_t {
  bool use_etag;
  const char*  cache_control;    // public, max-age=604800
  const char** implicit_exts;  // http_static_html_exts
  const char* implicit_index; // index.html
  bool   gzip_vary;           // true
  size_t gzip_min_size;       // 1kb  zip compress above this size
  size_t content_max_size;    // SIZE_MAX
  size_t read_buf_size;       // 64kb
} http_static_config_t;

extern const char* http_static_implicit_exts[];

#define http_static_default_config() { true, "public, max-age=604800", http_static_implicit_exts, "index", true, 1024, SIZE_MAX, 64*1024 }

/// Serve static files under a `root` directory. 
/// \param config The configuration. Can be NULL for the default configuration.
void http_serve_static(const char* root, const http_static_config_t* config);

/// \}

/// \}  HTTP Connections

/* ----------------------------------------------------------------------------
  TTY
-----------------------------------------------------------------------------*/

implicit_declare(tty)

lh_value _nodec_tty_allocv();
void     _nodec_tty_freev(lh_value ttyv);
void     async_tty_shutdown();

/// \defgroup nodec_tty TTY 
/// Terminal input and output.
/// \{

/// Read from the console.
/// \returns The input read. Should be freed by the callee (see using_free()).
char* async_tty_readline();

/// Write to the console.
/// \param s The string to write to the console. Can contain ANSI escape sequences.
void  async_tty_write(const char* s);

/// Enable the console in a scope.
/// Inside the given scope, one can use async_tty_readline() and async_tty_write().
///
/// \b Example:
/// ```
/// {using_tty() {
///   async_tty_write("\033[41;37m");         // ANSI escape to set red color
///   async_tty_write("what is your name? ");
///   const char* s = async_tty_readline();
///   {using_free(s) {
///     printf("I got: %s\n", s);
///   }}
/// }}
/// ```
#define using_tty()  \
    using_implicit_defer_exit(async_tty_shutdown(),_nodec_tty_freev,_nodec_tty_allocv(),tty)

/// \}


/* ----------------------------------------------------------------------------
  Main entry point
  -----------------------------------------------------------------------------*/

/// \defgroup nodec_various Various
/// Various utility functions.
/// \{

/// Type of the asynchronous main function.
typedef void (nodec_main_fun_t)();

/// Run the passed in function as the main asynchronous function.
/// This starts the libuv event loop and sets up the asynchronous and
/// exception effect handlers.
/// \param entry The main asynchronous entry point.
/// \returns A possible error code or 0 on success.
uv_errno_t  async_main( nodec_main_fun_t* entry );

/// \} 

/* ----------------------------------------------------------------------------
  Safe allocation
-----------------------------------------------------------------------------*/

/// \defgroup nodec_alloc Memory Allocation.
/// Routines for safe memory allocation based on `using`.
/// These raise an exception on failure and always `free` even 
/// if exceptions are raised.
/// The code :
/// ```
/// {using_alloc(mystruct_t, name) {
///   ...
/// }}
/// ```
/// will safely allocate a `mystruct_t*` to `name` which can be used inside `...`
/// and will be deallocated safely if an exception is thrown or when exiting
/// the block scope.
/// \{

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

/// Allocate memory for a type.
/// \param tp The type to allocate memory for.
/// \b Example
/// ```
/// my_struct_t* p = nodec_alloc(my_struct_t);
/// {using_free(p){
///    ...
/// }}
/// ```
#define nodec_alloc(tp)           ((tp*)(nodec_malloc(sizeof(tp))))
#define nodec_alloc_n(n,tp)       ((tp*)(nodec_malloc((n)*sizeof(tp))))
#define nodec_zero_alloc_n(n,tp)  ((tp*)(nodec_calloc(n,sizeof(tp))))
#define nodec_zero_alloc(tp)      nodec_zero_alloc_n(1,tp)
#define nodec_realloc_n(p,n,tp)   ((tp*)(nodec_realloc(p,(n)*sizeof(tp))))

/// Use a pointer in a scope and free afterwards.
/// Always frees the pointer, even if an exception is thrown.
/// \param name The name of the `void*`.
#define using_free(name)               defer(nodec_freev,lh_value_ptr(name))

/// Allocate and use a pointer in a scope and free afterwards.
/// Always frees the pointer, even if an exception is thrown.
/// \param tp   The type of value to allocate.
/// \param name The name of the `tp*`.
/// Equivalent to:
/// ```
/// tp* name = nodec_alloc(tp);
/// {using_free(name){
///   ...
/// }}
/// ```
#define using_alloc(tp,name)           tp* name = nodec_alloc(tp); using_free(name)

#define using_alloc_n(n,tp,name)       tp* name = nodec_alloc_n(n,tp); using_free(name)
#define using_zero_alloc_n(n,tp,name)  tp* name = nodec_zero_alloc_n(n,tp); using_free(name)
#define using_zero_alloc(tp,name)      using_zero_alloc_n(1,tp,name)

#define nodec_zero(tp,ptr)        memset(ptr,0,sizeof(tp));

/// \} 

#endif // __nodec_h
