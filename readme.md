<!--madoko
Title         : NodeC
Author        : Daan Leijen
Logo          : True
code {
  background-color: #EEE;
}
[TITLE]
-->



<img align="left" width="100" height="100" src="doc/logo-blue-100.png"/>

# NodeC

  \
Warning: this library is still under heavy active development and experimental.
It is not yet ready for general use. Current development is mostly for Windows x64 
using Visual Studio 2017, but the library has also been tested on Ubuntu Linux (AMDx64) 
and a Raspberry Pi (ARMv7).

NodeC is a _lean and mean_ version of [Node.js] which aims to provide
similar functionality as Node.js but using C directly. The main goal is improved
efficiency and resource usage (in particular more predictable resource
usage) but robust and asynchronous. 

Previously, this would be very cumbersome as _async/await_ style programming 
is very difficult in C. NodeC uses the [libhandler] library to provide algebraic 
effect handlers directly in C, making it _much_ easier to write such code. 

Preliminary NodeC [API documentation][apidoc] is available.
For a primer on algebraic effects, see the relevant section in the [koka book].

Enjoy!\
-- Daan.

Note: Clone the repository using `--recursive` as:
```
git clone --recursive https://github.com/koka-lang/nodec.git
```


[tr]: https://www.microsoft.com/en-us/research/publication/implementing-algebraic-effects-c
[koka book]: https://bit.do/kokabook
[apidoc]: https://koka-lang.github.io/nodec/api


# A "Hello World" Server

Here is a mini hello world server:
```C
#include <stdio.h>
#include <nodec.h>

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
  http_resp_send_body_str(HTTP_STATUS_OK,hello_body,"text/html");
}

static void hello() {
  async_http_server_at( "127.0.0.1:8080", NULL, &hello_serve );
}

int main() {
  async_main(hello);
  return 0;
}
```
Note how this uses async/await style programming (as with the `async_wait` function) to allow other
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
The `loop` parameter identifies the outer 
event loop and must be explicitly passed around everywhere. We also need to pass a fresh request 
object `req` and can then pass in the actualy file to close, `file`. After that we need
a top-level continuation function `cb` -- if there are any locals needed in that
callback, you need to explicitly allocate them and store them into the request object
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
the use implicit parameters there is no need to explicitly pass around a `loop` parameter.


# Building

## Sub projects

NodeC uses the [libhandler], [libuv], [zlib], [http-parser], and [mbedTLS] projects.
All of these are registered as submodules under the `deps` directory and are automatically
checked out at the right version when giving the `--recursive` flag to `clone`:
```
git clone --recursive https://github.com/koka-lang/nodec.git
```

(if you have cloned initially without `--recursive` you can use the `git submodule update`
command to fetch the submodules).


## Building on Windows

You will need:

1. [Microsoft Visual Studio 2017][vs2017].
2. [Python 2.7][python] (for building [libuv]).

When you have installed those, you can run `msvc-build.bat` to build
the NodeC library and all its dependencies:
```
> .\msvc-build.bat  [debug/release] [x86/x64]
```
By default it will build the x64 debug version. 
(Unfortunately, the way `libuv` is built means that you can choose `x86` or `x64` only
once for a checkout).

Depending on your Visual Studio version you might get a build error that the build
tools have the wrong version (like `1.40`). In that case you need to open the offending 
solution (usually `deps/zlib/contrib/vstudio/vc14/zlibvc.sln`) and upgrade the solution in
Visual Studio and save it to fix this.

After it builds successfully, you can use the Microsoft Visual Studio solution at 
`ide\msvc\nodec.sln` to play with examples in the `nodec-examples` project.


## Building on Unix's

This has been tested on Ubuntu Linux (amd64), and on a Raspberry PI (Raspbian, ARMv7).

1. Ensure you have `automake` and `libtool`  installed to build the [libuv] dependency.
   Usually, you can install this as:
   ```
   > sudo apt-get install automake libtool
   ```

2. Run `make-deps.sh` to configure and make all dependent 
   projects:
   ```
   > ./make-deps.sh
   ```

3. Run `configure` and `make` to build the `nodec` 
   library:
   ```
   > ./configure
   > make depend
   > make
   ```
   This will build the `libnodecx.a` library in the `out/nodecx/debug/lib` directory
   which includes all the dependent projects into one static library.
   Build a release version using:
   ```
   > make VARIANT=release
   ```

4. Make and run the example program:
   ```
   > make examples
   ```


[libuv]: https://github.com/libuv/libuv
[http-parser]: https://github.com/nodejs/http-parser
[libhandler]: https://github.com/koka-lang/libhandler
[zlib]: https://github.com/madler/zlib
[Node.js]: https://nodejs.org
[vs2017]: https://visualstudio.microsoft.com/vs/community/
[python]: https://www.python.org/downloads/release/python-2715/
[mbedTLS]: https://github.com/ARMmbed/mbedtls
