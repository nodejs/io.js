# llhttp
[![CI](https://github.com/nodejs/llhttp/workflows/CI/badge.svg)](https://github.com/nodejs/llhttp/actions?query=workflow%3ACI)

Port of [http_parser][0] to [llparse][1].

## Why?

Let's face it, [http_parser][0] is practically unmaintainable. Even
introduction of a single new method results in a significant code churn.

This project aims to:

* Make it maintainable
* Verifiable
* Improving benchmarks where possible

More details in [Fedor Indutny's talk at JSConf EU 2019](https://youtu.be/x3k_5Mi66sY)

## How?

Over time, different approaches for improving [http_parser][0]'s code base
were tried. However, all of them failed due to resulting significant performance
degradation.

This project is a port of [http_parser][0] to TypeScript. [llparse][1] is used
to generate the output C source file, which could be compiled and
linked with the embedder's program (like [Node.js][7]).

## Performance

So far llhttp outperforms http_parser:

|                 | input size |  bandwidth   |  reqs/sec  |   time  |
|:----------------|-----------:|-------------:|-----------:|--------:|
| **llhttp**      | 8192.00 mb | 1777.24 mb/s | 3583799.39 req/sec | 4.61 s |
| **http_parser** | 8192.00 mb | 694.66 mb/s | 1406180.33 req/sec | 11.79 s |

llhttp is faster by approximately **156%**.

## Maintenance

llhttp project has about 1400 lines of TypeScript code describing the parser
itself and around 450 lines of C code and headers providing the helper methods.
The whole [http_parser][0] is implemented in approximately 2500 lines of C, and
436 lines of headers.

All optimizations and multi-character matching in llhttp are generated
automatically, and thus doesn't add any extra maintenance cost. On the contrary,
most of http_parser's code is hand-optimized and unrolled. Instead describing
"how" it should parse the HTTP requests/responses, a maintainer should
implement the new features in [http_parser][0] cautiously, considering
possible performance degradation and manually optimizing the new code.

## Verification

The state machine graph is encoded explicitly in llhttp. The [llparse][1]
automatically checks the graph for absence of loops and correct reporting of the
input ranges (spans) like header names and values. In the future, additional
checks could be performed to get even stricter verification of the llhttp.

## Usage

```C
#include "stdio.h"
#include "llhttp.h"
#include "string.h"

int handle_on_message_complete(llhttp_t* parser) {
	fprintf(stdout, "Message completed!\n");
	return 0;
}

int main() {
	llhttp_t parser;
	llhttp_settings_t settings;

	/*Initialize user callbacks and settings */
	llhttp_settings_init(&settings);

	/*Set user callback */
	settings.on_message_complete = handle_on_message_complete;

	/*Initialize the parser in HTTP_BOTH mode, meaning that it will select between
	*HTTP_REQUEST and HTTP_RESPONSE parsing automatically while reading the first
	*input.
	*/
	llhttp_init(&parser, HTTP_BOTH, &settings);

	/*Parse request! */
	const char* request = "GET / HTTP/1.1\r\n\r\n";
	int request_len = strlen(request);

	enum llhttp_errno err = llhttp_execute(&parser, request, request_len);
	if (err == HPE_OK) {
		fprintf(stdout, "Successfully parsed!\n");
	} else {
		fprintf(stderr, "Parse error: %s %s\n", llhttp_errno_name(err), parser.reason);
	}
}
```
For more information on API usage, please refer to [src/native/api.h](https://github.com/nodejs/llhttp/blob/main/src/native/api.h).

## API

### llhttp_settings_t

The settings object contains a list of callbacks that the parser will invoke.

The following callbacks can return `0` (proceed normally), `-1` (error) or `HPE_PAUSED` (pause the parser):

* `on_message_begin`: Invoked when a new request/response starts.
* `on_message_complete`: Invoked when a request/response has been completedly parsed.
* `on_url_complete`: Invoked after the URL has been parsed.
* `on_method_complete`: Invoked after the HTTP method has been parsed.
* `on_version_complete`: Invoked after the HTTP version has been parsed.
* `on_status_complete`: Invoked after the status code has been parsed.
* `on_header_field_complete`: Invoked after a header name has been parsed.
* `on_header_value_complete`: Invoked after a header value has been parsed.
* `on_chunk_header`: Invoked after a new chunk is started. The current chunk length is stored in `parser->content_length`.
* `on_chunk_extension_name_complete`: Invoked after a chunk extension name is started.
* `on_chunk_extension_value_complete`: Invoked after a chunk extension value is started.
* `on_chunk_complete`: Invoked after a new chunk is received. 
* `on_reset`: Invoked after `on_message_complete` and before `on_message_begin` when a new message 
   is received on the same parser. This is not invoked for the first message of the parser.

The following callbacks can return `0` (proceed normally), `-1` (error) or `HPE_USER` (error from the callback): 

* `on_url`: Invoked when another character of the URL is received. 
* `on_status`: Invoked when another character of the status is received.
* `on_method`: Invoked when another character of the method is received. 
   When parser is created with `HTTP_BOTH` and the input is a response, this also invoked for the sequence `HTTP/`
   of the first message.
* `on_version`: Invoked when another character of the version is received.
* `on_header_field`: Invoked when another character of a header name is received.
* `on_header_value`: Invoked when another character of a header value is received.
* `on_chunk_extension_name`: Invoked when another character of a chunk extension name is received.
* `on_chunk_extension_value`: Invoked when another character of a extension value is received.

The callback `on_headers_complete`, invoked when headers are completed, can return:

* `0`: Proceed normally.
* `1`: Assume that request/response has no body, and proceed to parsing the next message.
* `2`: Assume absence of body (as above) and make `llhttp_execute()` return `HPE_PAUSED_UPGRADE`.
* `-1`: Error
* `HPE_PAUSED`: Pause the parser.

### `void llhttp_init(llhttp_t* parser, llhttp_type_t type, const llhttp_settings_t* settings)`

Initialize the parser with specific type and user settings.

### `uint8_t llhttp_get_type(llhttp_t* parser)`

Returns the type of the parser.

### `uint8_t llhttp_get_http_major(llhttp_t* parser)`

Returns the major version of the HTTP protocol of the current request/response.

### `uint8_t llhttp_get_http_minor(llhttp_t* parser)`

Returns the minor version of the HTTP protocol of the current request/response.

### `uint8_t llhttp_get_method(llhttp_t* parser)`

Returns the method of the current request.

### `int llhttp_get_status_code(llhttp_t* parser)`

Returns the method of the current response.

### `uint8_t llhttp_get_upgrade(llhttp_t* parser)`

Returns `1` if request includes the `Connection: upgrade` header.

### `void llhttp_reset(llhttp_t* parser)`

Reset an already initialized parser back to the start state, preserving the 
existing parser type, callback settings, user data, and lenient flags.

### `void llhttp_settings_init(llhttp_settings_t* settings)`

Initialize the settings object.

### `llhttp_errno_t llhttp_execute(llhttp_t* parser, const char* data, size_t len)`

Parse full or partial request/response, invoking user callbacks along the way.

If any of `llhttp_data_cb` returns errno not equal to `HPE_OK` - the parsing interrupts, 
and such errno is returned from `llhttp_execute()`. If `HPE_PAUSED` was used as a errno, 
the execution can be resumed with `llhttp_resume()` call.

In a special case of CONNECT/Upgrade request/response `HPE_PAUSED_UPGRADE` is returned 
after fully parsing the request/response. If the user wishes to continue parsing, 
they need to invoke `llhttp_resume_after_upgrade()`.

**if this function ever returns a non-pause type error, it will continue to return 
the same error upon each successive call up until `llhttp_init()` is called.**

### `llhttp_errno_t llhttp_finish(llhttp_t* parser)`

This method should be called when the other side has no further bytes to
send (e.g. shutdown of readable side of the TCP connection.)

Requests without `Content-Length` and other messages might require treating
all incoming bytes as the part of the body, up to the last byte of the
connection. 

This method will invoke `on_message_complete()` callback if the
request was terminated safely. Otherwise a error code would be returned.


### `int llhttp_message_needs_eof(const llhttp_t* parser)`

Returns `1` if the incoming message is parsed until the last byte, and has to be completed by calling `llhttp_finish()` on EOF.

### `int llhttp_should_keep_alive(const llhttp_t* parser)`

Returns `1` if there might be any other messages following the last that was
successfully parsed.

### `void llhttp_pause(llhttp_t* parser)`

Make further calls of `llhttp_execute()` return `HPE_PAUSED` and set
appropriate error reason.

**Do not call this from user callbacks! User callbacks must return
`HPE_PAUSED` if pausing is required.**

### `void llhttp_resume(llhttp_t* parser)`

Might be called to resume the execution after the pause in user's callback.

See `llhttp_execute()` above for details.

**Call this only if `llhttp_execute()` returns `HPE_PAUSED`.**

### `void llhttp_resume_after_upgrade(llhttp_t* parser)`

Might be called to resume the execution after the pause in user's callback.
See `llhttp_execute()` above for details.

**Call this only if `llhttp_execute()` returns `HPE_PAUSED_UPGRADE`**

### `llhttp_errno_t llhttp_get_errno(const llhttp_t* parser)`

Returns the latest error.

### `const char* llhttp_get_error_reason(const llhttp_t* parser)`

Returns the verbal explanation of the latest returned error.

**User callback should set error reason when returning the error. See
`llhttp_set_error_reason()` for details.**

### `void llhttp_set_error_reason(llhttp_t* parser, const char* reason)`

Assign verbal description to the returned error. Must be called in user
callbacks right before returning the errno.

**`HPE_USER` error code might be useful in user callbacks.**

### `const char* llhttp_get_error_pos(const llhttp_t* parser)`

Returns the pointer to the last parsed byte before the returned error. The
pointer is relative to the `data` argument of `llhttp_execute()`.

**This method might be useful for counting the number of parsed bytes.**

### `const char* llhttp_errno_name(llhttp_errno_t err)`

Returns textual name of error code.

### `const char* llhttp_method_name(llhttp_method_t method)`

Returns textual name of HTTP method.

### `const char* llhttp_status_name(llhttp_status_t status)`

Returns textual name of HTTP status.

### `void llhttp_set_lenient_headers(llhttp_t* parser, int enabled)`

Enables/disables lenient header value parsing (disabled by default).
Lenient parsing disables header value token checks, extending llhttp's
protocol support to highly non-compliant clients/server. 

No `HPE_INVALID_HEADER_TOKEN` will be raised for incorrect header values when
lenient parsing is "on".

**Enabling this flag can pose a security issue since you will be exposed to request smuggling attacks. USE WITH CAUTION!**

### `void llhttp_set_lenient_chunked_length(llhttp_t* parser, int enabled)`

Enables/disables lenient handling of conflicting `Transfer-Encoding` and
`Content-Length` headers (disabled by default).

Normally `llhttp` would error when `Transfer-Encoding` is present in
conjunction with `Content-Length`. 

This error is important to prevent HTTP request smuggling, but may be less desirable
for small number of cases involving legacy servers.

**Enabling this flag can pose a security issue since you will be exposed to request smuggling attacks. USE WITH CAUTION!**

### `void llhttp_set_lenient_keep_alive(llhttp_t* parser, int enabled)`

Enables/disables lenient handling of `Connection: close` and HTTP/1.0
requests responses.

Normally `llhttp` would error the HTTP request/response 
after the request/response with `Connection: close` and `Content-Length`. 

This is important to prevent cache poisoning attacks,
but might interact badly with outdated and insecure clients. 

With this flag the extra request/response will be parsed normally.

**Enabling this flag can pose a security issue since you will be exposed to poisoning attacks. USE WITH CAUTION!**

### `void llhttp_set_lenient_transfer_encoding(llhttp_t* parser, int enabled)`

Enables/disables lenient handling of `Transfer-Encoding` header.

Normally `llhttp` would error when a `Transfer-Encoding` has `chunked` value
and another value after it (either in a single header or in multiple
headers whose value are internally joined using `, `).

This is mandated by the spec to reliably determine request body size and thus
avoid request smuggling.

With this flag the extra value will be parsed normally.

**Enabling this flag can pose a security issue since you will be exposed to request smuggling attacks. USE WITH CAUTION!**

### `void llhttp_set_lenient_version(llhttp_t* parser, int enabled)`

Enables/disables lenient handling of HTTP version.

Normally `llhttp` would error when the HTTP version in the request or status line
is not `0.9`, `1.0`, `1.1` or `2.0`.
With this flag the extra value will be parsed normally.

**Enabling this flag can pose a security issue since you will allow unsupported HTTP versions. USE WITH CAUTION!**

### `void llhttp_set_lenient_data_after_close(llhttp_t* parser, int enabled)`

Enables/disables lenient handling of additional data received after a message ends
and keep-alive is disabled.

Normally `llhttp` would error when additional unexpected data is received if the message
contains the `Connection` header with `close` value.
With this flag the extra data will discarded without throwing an error.

**Enabling this flag can pose a security issue since you will be exposed to poisoning attacks. USE WITH CAUTION!**

### `void llhttp_set_lenient_optional_lf_after_cr(llhttp_t* parser, int enabled)`

Enables/disables lenient handling of incomplete CRLF sequences.

Normally `llhttp` would error when a CR is not followed by LF when terminating the
request line, the status line, the headers or a chunk header.
With this flag only a CR is required to terminate such sections.

**Enabling this flag can pose a security issue since you will be exposed to request smuggling attacks. USE WITH CAUTION!**

### `void llhttp_set_lenient_optional_cr_before_lf(llhttp_t* parser, int enabled)`

Enables/disables lenient handling of line separators.

Normally `llhttp` would error when a LF is not preceded by CR when terminating the
request line, the status line, the headers, a chunk header or a chunk data.
With this flag only a LF is required to terminate such sections.

**Enabling this flag can pose a security issue since you will be exposed to request smuggling attacks. USE WITH CAUTION!**

### `void llhttp_set_lenient_optional_crlf_after_chunk(llhttp_t* parser, int enabled)`

Enables/disables lenient handling of chunks not separated via CRLF.

Normally `llhttp` would error when after a chunk data a CRLF is missing before
starting a new chunk.
With this flag the new chunk can start immediately after the previous one.

**Enabling this flag can pose a security issue since you will be exposed to request smuggling attacks. USE WITH CAUTION!**

### `void llhttp_set_lenient_spaces_after_chunk_size(llhttp_t* parser, int enabled)`

Enables/disables lenient handling of spaces after chunk size.

Normally `llhttp` would error when after a chunk size is followed by one or more spaces are present instead of a CRLF or `;`.
With this flag this check is disabled.

**Enabling this flag can pose a security issue since you will be exposed to request smuggling attacks. USE WITH CAUTION!**

## Build Instructions

Make sure you have [Node.js](https://nodejs.org/), npm and npx installed. Then under project directory run:

```sh
npm ci
make
```

---

### Bindings to other languages

* Lua: [MunifTanjim/llhttp.lua][11]
* Python: [pallas/pyllhttp][8]
* Ruby: [metabahn/llhttp][9]
* Rust: [JackLiar/rust-llhttp][10]

### Using with CMake

If you want to use this library in a CMake project as a shared library, you can use the snippet below.

```
FetchContent_Declare(llhttp
  URL "https://github.com/nodejs/llhttp/archive/refs/tags/release/v8.1.0.tar.gz")

FetchContent_MakeAvailable(llhttp)

# Link with the llhttp_shared target
target_link_libraries(${EXAMPLE_PROJECT_NAME} ${PROJECT_LIBRARIES} llhttp_shared ${PROJECT_NAME})
```

If you want to use this library in a CMake project as a static library, you can set some cache variables first.

```
FetchContent_Declare(llhttp
  URL "https://github.com/nodejs/llhttp/archive/refs/tags/release/v8.1.0.tar.gz")

set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
set(BUILD_STATIC_LIBS ON CACHE INTERNAL "")
FetchContent_MakeAvailable(llhttp)

# Link with the llhttp_static target
target_link_libraries(${EXAMPLE_PROJECT_NAME} ${PROJECT_LIBRARIES} llhttp_static ${PROJECT_NAME})
```

_Note that using the git repo directly (e.g., via a git repo url and tag) will not work with FetchContent_Declare because [CMakeLists.txt](./CMakeLists.txt) requires string replacements (e.g., `_RELEASE_`) before it will build._

## Building on Windows

### Installation

* `choco install git`
* `choco install node`
* `choco install llvm` (or install the `C++ Clang tools for Windows` optional package from the Visual Studio 2019 installer)
* `choco install make` (or if you have MinGW, it comes bundled)

1. Ensure that `Clang` and `make` are in your system path.
2. Using Git Bash, clone the repo to your preferred location.
3. Cd into the cloned directory and run `npm ci`
5. Run `make`
6. Your `repo/build` directory should now have `libllhttp.a` and `libllhttp.so` static and dynamic libraries.
7. When building your executable, you can link to these libraries. Make sure to set the build folder as an include path when building so you can reference the declarations in `repo/build/llhttp.h`.

### A simple example on linking with the library:

Assuming you have an executable `main.cpp` in your current working directory, you would run: `clang++ -Os -g3 -Wall -Wextra -Wno-unused-parameter -I/path/to/llhttp/build main.cpp /path/to/llhttp/build/libllhttp.a -o main.exe`.

If you are getting `unresolved external symbol` linker errors you are likely attempting to build `llhttp.c` without linking it with object files from `api.c` and `http.c`.

#### LICENSE

This software is licensed under the MIT License.

Copyright Fedor Indutny, 2018.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to permit
persons to whom the Software is furnished to do so, subject to the
following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
USE OR OTHER DEALINGS IN THE SOFTWARE.

[0]: https://github.com/nodejs/http-parser
[1]: https://github.com/nodejs/llparse
[2]: https://en.wikipedia.org/wiki/Register_allocation#Spilling
[3]: https://en.wikipedia.org/wiki/Tail_call
[4]: https://llvm.org/docs/LangRef.html
[5]: https://llvm.org/docs/LangRef.html#call-instruction
[6]: https://clang.llvm.org/
[7]: https://github.com/nodejs/node
[8]: https://github.com/pallas/pyllhttp
[9]: https://github.com/metabahn/llhttp
[10]: https://github.com/JackLiar/rust-llhttp
[11]: https://github.com/MunifTanjim/llhttp.lua
