/* ----------------------------------------------------------------------------
  Copyright (c) 2018, Microsoft Research, Daan Leijen
  This is free software; you can redistribute it and/or modify it under the
  terms of the Apache License, Version 2.0. A copy of the License can be
  found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/

#pragma once
#error "don't include this file; it is for documentation only"

/// \addtogroup streams
/// \{

/// A `libuv` stream.
/// Try not to use these directly but wrap it into a buffered stream,
/// See nodec_bstream_alloc() and nodec_bstream_alloc_read().
typedef struct _uv_stream_t uv_stream_t;

/// \}

/// \addtogroup buffers
/// \{

/// A `libuv` buffer.
/// These are small and always passed by value.
typedef struct _uv_buf_t {
  /// The length of the buffer data.
  size_t len;
  /// Pointer to the buffer data.
  void*  base;
} uv_buf_t;

/// \}

/// \addtogroup nodec_fs
/// \{

/// A platform independent file handle.
typedef struct _uv_file* uv_file;

/// File open modes, see <a href="https://linux.die.net/man/2/open">open2()</a>.
/// The open mode must always include one of `UV_FS_O_RDONLY`, `UV_FS_O_WRONLY`,
/// or `UV_FS_O_RDWR`. 
typedef enum _nodec_open_flags_t {	
  UV_FS_O_APPEND,				///< Open in append mode.
  UV_FS_O_CREAT,        ///< Create a new file if it does not exist yet; `mode` specifies the permissions in this case.
  UV_FS_O_EXCL,         ///< Fail if the file already exists.
  UV_FS_O_RANDOM,       ///< Access is intended to be random.
  UV_FS_O_RDONLY,       ///< Open for reading only.
  UV_FS_O_RDWR,         ///< Open for reading and writing.
  UV_FS_O_SEQUENTIAL,   ///< Access is intended to be sequential.
  UV_FS_O_SHORT_LIVED,  ///< The file is intended to be short lived.
  UV_FS_O_TEMPORARY,    ///< The file is intended as a temporary file and should not be flushed. 
  UV_FS_O_TRUNC,        ///< If the file exists and  opened for writing, it is set to a zero length.
  UV_FS_O_WRONLY,       ///< Open for writing only.
  UV_FS_O_DIRECT,       ///< I/O is done directly from buffers that must be aligned.
  UV_FS_O_DIRECTORY,    ///< If the path is not a directory, fail to open
  UV_FS_O_DSYNC,        ///< Writing is blocking and only completes if flushed to disk.
  UV_FS_O_EXLOCK,       ///< Obtain an exclusive lock
  UV_FS_O_NOATIME,      ///< Do not update the access time of the file.
  UV_FS_O_NOCTTY,       ///< If the path identifies a terminal device, opening the path will not cause that terminal to become the controlling terminal for the process (if the process does not already have one). (not supported on Windows)
  UV_FS_O_NOFOLLOW,     ///< If the path is a symbolic link, fail to open. (not supported on Windows)
  UV_FS_O_NONBLOCK,			///< Open in non-blocking mode (not supported on Windows).
  UV_FS_O_SYMLINK,      ///< If the path is not a symbolic link, fail to open. (not supported on Windows) 
  UV_FS_O_SYNC          ///< The file is opened for synchronous I/O. Write operations will complete once all data and all metadata are flushed to disk.
} nodec_open_flags_t;

/// Permission mode for (user) reading.
#define R_OK 4
/// Permission mode for (user) writing.
#define W_OK 2
/// Permission mode for (user) execute.
#define X_OK 1

/// Directory entry types.
typedef enum {
  UV_DIRENT_UNKNOWN,  ///< Unknown type.
  UV_DIRENT_FILE,     ///< A regular file.
  UV_DIRENT_DIR,      ///< A regular (sub)directory
  UV_DIRENT_LINK,     ///< A symbolic link.
  UV_DIRENT_FIFO,     ///< A FIFO queue.
  UV_DIRENT_SOCKET,   ///< A socket.
  UV_DIRENT_CHAR,     ///< A character device.
  UV_DIRENT_BLOCK     ///< A block device.
} uv_dirent_type_t;

/// Directory entries
typedef struct uv_dirent_s {
  const char* name;       ///< Name of the entry.
  uv_dirent_type_t type;  ///< Type of the entry.
} uv_dirent_t;

/// \}


/// \addtogroup http_setup
/// \{

/// The HTTP method enumeration. 
/// The first 5 (8) are commonly used while the others are used in various
/// protocols, like WebDAV, Subversion etc. 
typedef enum http_method {
  HTTP_DELETE, ///< DELETE method
  HTTP_GET, ///< GET method
  HTTP_HEAD, ///< HEAD method
  HTTP_POST, ///< POST method
  HTTP_PUT, ///< PUT method
  /* pathological */
  HTTP_CONNECT, ///< CONNECT method
  HTTP_OPTIONS, ///< OPTIONS method
  HTTP_TRACE, ///< TRACE method
  /* WebDAV */
  HTTP_COPY, ///< COPY method
  HTTP_LOCK, ///< LOCK method
  HTTP_MKCOL, ///< MKCOL method
  HTTP_MOVE, ///< MOVE method
  HTTP_PROPFIND, ///< PROPFIND method
  HTTP_PROPPATCH, ///< PROPPATCH method
  HTTP_SEARCH, ///< SEARCH method
  HTTP_UNLOCK, ///< UNLOCK method
  HTTP_BIND, ///< BIND method
  HTTP_REBIND, ///< REBIND method
  HTTP_UNBIND, ///< UNBIND method
  HTTP_ACL, ///< ACL method
  /* subversion */
  HTTP_REPORT, ///< REPORT method
  HTTP_MKACTIVITY, ///< MKACTIVITY method
  HTTP_CHECKOUT, ///< CHECKOUT method
  HTTP_MERGE, ///< MERGE method
  /* upnp */
  HTTP_MSEARCH, ///< MSEARCH method
  HTTP_NOTIFY, ///< NOTIFY method
  HTTP_SUBSCRIBE, ///< SUBSCRIBE method
  HTTP_UNSUBSCRIBE, ///< UNSUBSCRIBE method
  /* RFC-5789 */
  HTTP_PATCH, ///< PATCH method
  HTTP_PURGE, ///< PURGE method
  /* CalDAV */
  HTTP_MKCALENDAR, ///< MKCALENDAR method
  /* RFC-2068, section 19.6.1.2 */
  HTTP_LINK, ///< LINK method
  HTTP_UNLINK, ///< UNLINK method
  /* icecast */
  HTTP_SOURCE
} http_method_t;

/// HTTP server response status codes.
typedef enum http_status {
  HTTP_STATUS_CONTINUE,  ///< 100, Continue)                        
  HTTP_STATUS_SWITCHING_PROTOCOLS,  ///< 101, Switching Protocols)             
  HTTP_STATUS_PROCESSING,  ///< 102, Processing)                      
  HTTP_STATUS_OK,  ///< 200, OK)                              
  HTTP_STATUS_CREATED,  ///< 201, Created)                         
  HTTP_STATUS_ACCEPTED,  ///< 202, Accepted)                        
  HTTP_STATUS_NON_AUTHORITATIVE_INFORMATION,  ///< 203, Non-Authoritative Information)   
  HTTP_STATUS_NO_CONTENT,  ///< 204, No Content)                      
  HTTP_STATUS_RESET_CONTENT,  ///< 205, Reset Content)                   
  HTTP_STATUS_PARTIAL_CONTENT,  ///< 206, Partial Content)                 
  HTTP_STATUS_MULTI_STATUS,  ///< 207, Multi-Status)                    
  HTTP_STATUS_ALREADY_REPORTED,  ///< 208, Already Reported)                
  HTTP_STATUS_IM_USED,  ///< 226, IM Used)                         
  HTTP_STATUS_MULTIPLE_CHOICES,  ///< 300, Multiple Choices)                
  HTTP_STATUS_MOVED_PERMANENTLY,  ///< 301, Moved Permanently)               
  HTTP_STATUS_FOUND,  ///< 302, Found)                           
  HTTP_STATUS_SEE_OTHER,  ///< 303, See Other)                       
  HTTP_STATUS_NOT_MODIFIED,  ///< 304, Not Modified)                    
  HTTP_STATUS_USE_PROXY,  ///< 305, Use Proxy)                       
  HTTP_STATUS_TEMPORARY_REDIRECT,  ///< 307, Temporary Redirect)              
  HTTP_STATUS_PERMANENT_REDIRECT,  ///< 308, Permanent Redirect)              
  HTTP_STATUS_BAD_REQUEST,  ///< 400, Bad Request)                     
  HTTP_STATUS_UNAUTHORIZED,  ///< 401, Unauthorized)                    
  HTTP_STATUS_PAYMENT_REQUIRED,  ///< 402, Payment Required)                
  HTTP_STATUS_FORBIDDEN,  ///< 403, Forbidden)                       
  HTTP_STATUS_NOT_FOUND,  ///< 404, Not Found)                       
  HTTP_STATUS_METHOD_NOT_ALLOWED,  ///< 405, Method Not Allowed)              
  HTTP_STATUS_NOT_ACCEPTABLE,  ///< 406, Not Acceptable)                  
  HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED,  ///< 407, Proxy Authentication Required)   
  HTTP_STATUS_REQUEST_TIMEOUT,  ///< 408, Request Timeout)                 
  HTTP_STATUS_CONFLICT,  ///< 409, Conflict)                        
  HTTP_STATUS_GONE,  ///< 410, Gone)                            
  HTTP_STATUS_LENGTH_REQUIRED,  ///< 411, Length Required)                 
  HTTP_STATUS_PRECONDITION_FAILED,  ///< 412, Precondition Failed)             
  HTTP_STATUS_PAYLOAD_TOO_LARGE,  ///< 413, Payload Too Large)               
  HTTP_STATUS_URI_TOO_LONG,  ///< 414, URI Too Long)                    
  HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE,  ///< 415, Unsupported Media Type)          
  HTTP_STATUS_RANGE_NOT_SATISFIABLE,  ///< 416, Range Not Satisfiable)           
  HTTP_STATUS_EXPECTATION_FAILED,  ///< 417, Expectation Failed)              
  HTTP_STATUS_MISDIRECTED_REQUEST,  ///< 421, Misdirected Request)             
  HTTP_STATUS_UNPROCESSABLE_ENTITY,  ///< 422, Unprocessable Entity)            
  HTTP_STATUS_LOCKED,  ///< 423, Locked)                          
  HTTP_STATUS_FAILED_DEPENDENCY,  ///< 424, Failed Dependency)               
  HTTP_STATUS_UPGRADE_REQUIRED,  ///< 426, Upgrade Required)                
  HTTP_STATUS_PRECONDITION_REQUIRED,  ///< 428, Precondition Required)           
  HTTP_STATUS_TOO_MANY_REQUESTS,  ///< 429, Too Many Requests)               
  HTTP_STATUS_REQUEST_HEADER_FIELDS_TOO_LARGE,  ///< 431, Request Header Fields Too Large) 
  HTTP_STATUS_UNAVAILABLE_FOR_LEGAL_REASONS,  ///< 451, Unavailable For Legal Reasons)   
  HTTP_STATUS_INTERNAL_SERVER_ERROR,  ///< 500, Internal Server Error)           
  HTTP_STATUS_NOT_IMPLEMENTED,  ///< 501, Not Implemented)                 
  HTTP_STATUS_BAD_GATEWAY,  ///< 502, Bad Gateway)                     
  HTTP_STATUS_SERVICE_UNAVAILABLE,  ///< 503, Service Unavailable)             
  HTTP_STATUS_GATEWAY_TIMEOUT,  ///< 504, Gateway Timeout)                 
  HTTP_STATUS_HTTP_VERSION_NOT_SUPPORTED,  ///< 505, HTTP Version Not Supported)      
  HTTP_STATUS_VARIANT_ALSO_NEGOTIATES,  ///< 506, Variant Also Negotiates)         
  HTTP_STATUS_INSUFFICIENT_STORAGE,  ///< 507, Insufficient Storage)            
  HTTP_STATUS_LOOP_DETECTED,  ///< 508, Loop Detected)                   
  HTTP_STATUS_NOT_EXTENDED,  ///< 510, Not Extended)                    
  HTTP_STATUS_NETWORK_AUTHENTICATION_REQUIRED  ///< 511, Network Authentication Required) 
} http_status_t;

/// \}

