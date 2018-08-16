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


/// \addtogroup http_connect
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

/*! \mainpage

This is the API documentation of the 
<a class="external" href="https://github.com/koka-lang/nodec">NodeC</a> project

See the <a class="external" href="./modules.html">Modules</a> section for an 
overview of the API functionality.

*/