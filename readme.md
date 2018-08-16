<!--madoko
Title         : NodeC
Author        : Daan Leijen
Logo          : True
code {
  background-color: #EEE;
}
[TITLE]
-->

# ![NodeC](doc/logo-blue-100.png?raw=true) NodeC

Warning: this library is still under active development and experimental.
It is not yet ready for general use. Current development is mostly for Windows x64.

`NodeC` is a _lean and mean_ version of [NodeJS] which aims to provide
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


static void hello_serve() {
  // debug
  printf("strand %i request, url: %s\n", http_req_strand_id(), http_req_url());
  
  // do something
  printf("waiting 2 secs...\n"); 
  async_wait(2000);
  
  // send response (adds content-type and content-length headers)
  http_resp_send_body_str(out,hello_body,"test/html");
}

static void hello() {
  async_http_server_at( "127.0.0.1:8080", NULL, &hello_serve );
}

int main() {
  async_main(hello);
  return 0;
}
```
Note how this uses async/await style programming as with `async_wait` that allows other
requests to be interleaved while waiting or doing other work. Similarly, we use 
implicit parameters (or dynamic binding) to bind the current request and response objects
implicitly without needing to thread it around everywhere, and can thus write 
`http_req_url()` to get the URL of the current request. Finally, exceptions are used to
automatically propagate errors and ensure robust resource finalization. All of these
control-flow features are build upon the algebraic effect handlers provided by [libhandler].

# Why Algebraic Effect handlers

For example, consider the following asynchronous function for closing a file
in plain [libuv]:
```C
int uv_fs_close(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb);
```
We need to pass around a `loop` parameter everywhere, and create a fresh request 
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
exceptions and thus there is no need to check any return value, and through
the use implicit parameters there is no need to pass around a `loop` parameter anymore.




# Building

## Sub projects

NodeC uses the [libhandler], [libuv], [zlib], and [http-parser] projects.
You need to check them out in a peer directory of `NodeC`, i.e.
```
home\dev\
  libhandler
  http-parser
  libuv
  nodec
  zlib
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

* `> git clone https://github.com/koka-lang/libhandler.git`

There is no need to build it, it is included automatically by the NodeC project.


### zlib

The [zlib] project is checked out as:

* `> git clone https://github.com/madler/zlib.git`

* And build it as a static library. On Windows open up `contrib\vstudio\vc14\zlibvc.sln`
  to build the `Debug` and `ReleaseWithoutAsm` versions for `x64`, as `zlibstat.lib`.

## Windows

Use the Microsoft Visual Studio solution at `ide\msvc\nodec.sln`.


[libuv]: https://github.com/libuv/libuv
[http-parser]: https://github.com/nodejs/http-parser
[libhandler]: https://github.com/koka-lang/libhandler
[zlib]: https://github.com/madler/zlib