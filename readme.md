<!--madoko
Title         : NodeC
Author        : Daan Leijen
Logo          : True
code {
  background-color: #EEE;
}
[TITLE]
-->

# Overview

Warning: this library is still under active development and experimental.
It is not yet ready for general use. Current development is mostly for Windows x64.

`NodeC` is a _lean and mean_ version of [NodeJS] -- it aims to provide
similar functionality as NodeJS but using C. The main goal is improved
efficiency and resource usage (in particular more predictable resource
usage) but highly robust and asynchronous. 

Previously, this would be very cumbersome as _async/await_ style programming 
is very difficult in C. NodeC uses the `libhandler` library to provide algebraic 
effect handlers directly in C making it much easier to write highly asynchronous
and robust code directly in C. 

For a primer on algebraic effects, see the relevant section in the [koka book].

Enjoy!\
-- Daan.

[tr]: https://www.microsoft.com/en-us/research/publication/implementing-algebraic-effects-c
[koka book]: https://bit.do/kokabook


# Why Algebraic Effect handlers

For example, consider the following asynchronous function for closing a file
in plain [libuv]:
```C
int uv_fs_close(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb);
```
We need to thread around a `loop` parameter everywhere, and create a fresh request 
object `req`. Then we can pass in the actualy file to close, `file`, but then need
a top-level continuation function `cb`. If there are any locals you need in that
callback you need to explicitly allocate them and store them into the request object
to pass them to the callback. Finally, an `int` is returned in case an error happens.

In NodeC, using algebraic effects provided by `libhandler`, we can wrap that 
function and have the following function signature instead:
```C
void async_fs_close( uv_file file );
```
Yes! much better. We only pass the essential `file` parameter and that is it. 
The call is still asynchronous but will continue at that exact point in the
program when the file is closed (just like async/await). Furthermore, we provide
exceptions and thus there is no need to check any return value.


# A `Hello World` Mini Server

Here is a mini hello world server:
```C
const char* hello_body =
"<!DOCTYPE html>"
"<html>\n"
"<head>\n"
"  <meta charset=\"utf-8\">\n"
"</head>\n"
"<body>\n"
"  <h1>Hello NodeC World!</h1>\n"
"</body>\n"
"</html>\n";


static void hello_serve(int strand_id, http_in_t* in, http_out_t* out, lh_value arg) {
  // debug
  printf("strand %i request, url: %s, content length: %llu\n", strand_id, http_in_url(in), http_in_content_length(in));
  
  // do something
  printf("waiting %i secs...\n", 1 + strand_id); 
  async_wait(1000 + strand_id*1000);
  
  // send response
  http_out_add_header(out,"Content-Type","text/html; charset=utf-8");
  http_out_send_status_headers(out,HTTP_STATUS_OK,true);
  http_out_send_body(out,hello_body);
}

static void hello() {
  define_ip4_addr("127.0.0.1", 8080,addr);
  async_http_server_at( addr, 0, 3 /* max concurrency */, 0, &hello_serve, lh_value_null );
}

int main() {
  async_main(hello);
  return 0;
}
```


# Building

## Sub projects

NodeC uses the [libhandler], [libuv], and [http-parser] projects.
You need to check them out in a peer directory of `NodeC`, i.e.
```
home\dev\
  libhandler
  http-parser
  libuv
  nodec
```


### LibUV

For the `test\libuv` example, you need to get the [libuv] project as:

* Check out `libuv`
  `> git clone https://github.com/libuv/libuv.git`

* And build a static library. On windows use:
  `> cd libuv`
  `> ./vcbuild debug x64 static`

* You may need to install Python too.


### http-parser

The [http-parser] project is checked out as:

* `> git clone https://github.com/nodejs/http-parser.git`

There is no need to build it, it is included automatically by the NodeC project.


### libhandler


The [libhandler] project is checked out as:

* `> https://github.com/koka-lang/libhandler.git`

There is no need to build it, it is included automatically by the NodeC project.


## Windows

Use the Microsoft Visual Studio solution at `ide\msvc\nodec.sln`.


[libuv]: https://github.com/libuv/libuv
[http-parser]: https://github.com/nodejs/http-parser
[libhandler]: https://github.com/koka-lang/libhandler