# Maintaining Dependencies

Node.js depends on additional components beyond the Node.js code
itself. These dependencies provide both native and JavaScript code
and are built together with the code under the `src` and `lib`
directories to create the Node.js binaries.

All dependencies are located within the `deps` directory.
This a list of all the dependencies:

* [acorn][]
* [ada][]
* [amaro][]
* [base64][]
* [brotli][]
* [c-ares][]
* [cjs-module-lexer][]
* [corepack][]
* [googletest][]
* [histogram][]
* [icu-small][]
* [libuv][]
* [llhttp][]
* [minimatch][]
* [nghttp2][]
* [nghttp3][]
* [ngtcp2][]
* [npm][]
* [openssl][]
* [postject][]
* [simdjson][]
* [simdutf][]
* [sqlite][]
* [undici][]
* [uvwasi][]
* [V8][]
* [zlib][]

Any code which meets one or more of these conditions should
be managed as a dependency:

* originates in an upstream project and is maintained
  in that upstream project.
* is not built from the `preferred form of the work for
  making modifications to it` (see
  [GNU GPL v2, section 3.](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
  when `make node` is run. A good example is
  WASM code generated from C (the preferred form).
  Typically generation is only supported on a subset of platforms, needs
  additional tools, and is pre-built outside of the `make node`
  step and then committed as a WASM binary in the directory
  for the dependency under the `deps` directory.

By default all dependencies are bundled into the Node.js
binary, however, `configure` options should be available to
use an externalized version at runtime when:

* the dependency provides native code and is available as
  a shared library in one or more of the common Node.js
  distributions.
* the dependency provides JavaScript and is not built
  from the `preferred form of the work for making modifications
  to it` when `make node` is run.

Many distributions use externalized dependencies for one or
more of these reasons:

1. They have a requirement to build everything that they ship
   from the `preferred form of the work for making
   modifications to it`. This means that they need to
   replace any pre-built components (for example WASM
   binaries) with an equivalent that they have built.
2. They manage the dependency separately as it is used by
   more applications than just Node.js. Linking against
   a shared library allows them to manage updates and
   CVE fixes against the library instead of having to
   patch all of the individual applications.
3. They have a system wide configuration for the
   dependency that all applications should respect.

## Supporting externalized dependencies with native code

Support for externalized dependencies with native code for which a
shared library is available can added by:

* adding options to `configure.py`. These are added to the
  shared\_optgroup and include an options to:
  * enable use of a shared library
  * set the name of the shared library
  * set the path to the directory with the includes for the
    shared library
  * set the path to where to find the shared library at
    runtime
* add a call to configure\_library() to `configure.py` for the
  library at the end of list of existing configure\_library() calls.
  If there are additional libraries that are required it is
  possible to list more than one with the `pkgname` option.
* in `node.gypi` guard the build for the dependency
  with `node_shared_depname` so that it is only built if
  the dependency is being bundled into Node.js itself. For example:

```text
    [ 'node_shared_brotli=="false"', {
      'dependencies': [ 'deps/brotli/brotli.gyp:brotli' ],
    }],
```

## Supporting externalizable dependencies with JavaScript code

Support for an externalizable dependency with JavaScript code
can be added by:

* adding an entry to the `sharable_builtins` map in
  `configure.py`. The path should correspond to the file
  within the deps directory that is normally bundled into
  Node.js. For example `deps/cjs-module-lexer/lexer.js`.
  This will add a new option for building with that dependency
  externalized. After adding the entry you can see
  the new option by running `./configure --help`.

* adding a call to `AddExternalizedBuiltin` to the constructor
  for BuiltinLoader in `src/node_builtins.cc` for the
  dependency using the `NODE_SHARED_BUILTLIN` #define generated for
  the dependency. After running `./configure` with the new
  option you can find the #define in `config.gypi`. You can cut and
  paste one of the existing entries and then update to match the
  import name for the dependency and the #define generated.

## Supporting non-externalized dependencies with JavaScript code

If the dependency consists of JavaScript in the
`preferred form of the work for making modifications to it`, it
can be added as a non-externalizable dependency. In this case
simply add the path to the JavaScript file in the `deps_files`
list in the `node.gyp` file.

## Updating dependencies

Most dependencies are automatically updated by
[dependency-update-action][] that runs weekly.
However, it is possible to manually update a dependency by running
the corresponding script in `tools/update-deps`.
[OpenSSL](https://github.com/openssl/openssl) has its own update action:
[update-openssl-action][].
[npm-cli-bot](https://github.com/npm/cli/blob/latest/.github/workflows/create-node-pr.yml)
takes care of npm update, it is maintained by the npm team.

PRs for manual dependency updates should only be accepted if
the update cannot be generated by the automated tooling,
the reason is clearly documented and either the PR is
reviewed in detail or it is from an existing collaborator.

In general updates to dependencies should only be accepted
if they have already landed in the upstream. The TSC may
grant an exception on a case-by-case basis. This avoids
the project having to float patches for a long time and
ensures that tooling can generate updates automatically.

## Dependency list

### acorn

The [acorn](https://github.com/acornjs/acorn) dependency is a JavaScript parser.
[acorn-walk](https://github.com/acornjs/acorn/tree/master/acorn-walk) is
an abstract syntax tree walker for the ESTree format.

### ada

The [ada](https://github.com/ada-url/ada) dependency is a
fast and spec-compliant URL parser written in C++.

### amaro

The [amaro](https://www.npmjs.com/package/amaro) dependency is a wrapper around the
WebAssembly version of the SWC JavaScript/TypeScript parser.

### brotli

The [brotli](https://github.com/google/brotli) dependency is
used for the homonym generic-purpose lossless compression algorithm.

### c-ares

The [c-ares](https://github.com/c-ares/c-ares) is a C library
for asynchronous DNS requests.

### cjs-module-lexer

The [cjs-module-lexer](https://github.com/nodejs/node/tree/HEAD/deps/cjs-module-lexer)
dependency is used within the Node.js ESM implementation to detect the
named exports of a CommonJS module.
See [maintaining-cjs-module-lexer][] for more information.

### corepack

The [corepack](https://github.com/nodejs/corepack) dependency is a
zero-runtime-dependency Node.js script that acts as a bridge between
Node.js projects and the package managers they are intended to
be used with during development.
In practical terms, Corepack will let you use Yarn and pnpm without having to
install them - just like what currently happens with npm, which is shipped
by Node.js by default.

### googletest

The [googletest](https://github.com/google/googletest) dependency is Google’s
C++ testing and mocking framework.

### histogram

The [histogram](https://github.com/HdrHistogram/HdrHistogram_c) dependency is
a C port of High Dynamic Range (HDR) Histogram.

### ic

The [icu](http://site.icu-project.org) is widely used set of C/C++
and Java libraries providing Unicode and Globalization
support for software applications.
See [maintaining-icu][] for more information.

### libuv

The [libuv](https://github.com/libuv/libuv) dependency is a
multi-platform support library with a focus on asynchronous I/O.
It was primarily developed for use by Node.js.

### llhttp

The [llhttp](https://github.com/nodejs/llhttp) dependency is
the http parser used by Node.js.
See [maintaining-http][] for more information.

### minimatch

The [minimatch](https://github.com/isaacs/minimatch) dependency is a
minimal matching utility.

### nghttp2

The [nghttp2](https://github.com/nghttp2/nghttp2) dependency is a C library
implementing HTTP/2 protocol.
See [maintaining-http][] for more information.

### nghttp3

The [nghttp3](https://github.com/ngtcp2/nghttp3) dependency is HTTP/3 library
written in C. See ngtcp2 for more information.

### ngtcp2

The ngtcp2 and nghttp3 dependencies provide the core functionality for
QUIC and HTTP/3.

The sources are pulled from:

* ngtcp2: <https://github.com/ngtcp2/ngtcp2>
* nghttp3: <https://github.com/ngtcp2/nghttp3>

In both the `ngtcp2` and `nghttp3` git repos, the active development occurs
in the default branch (currently named `main` in each). Tagged versions do not
always point to the default branch.

We only use a subset of the sources for each.

The `nghttp3` library depends on `ngtcp2`. Both should always be updated
together. From `ngtcp2` we only want the contents of the `lib` and `crypto`
directories; from `nghttp3` we only want the contents of the `lib` directory.

### npm

The [npm](https://github.com/npm/cli) dependency is
the package manager for JavaScript.

New pull requests should be opened when a "next" version of npm has
been released. Once the "next" version has been promoted to "latest"
the PR should be updated as necessary.

The specific Node.js release streams the new version will be able to land into
are at the discretion of the release and LTS teams.

This process only covers full updates to new versions of npm. Cherry-picked
changes can be reviewed and landed via the normal consensus seeking process.

### openssl

The [openssl](https://github.com/quictls/openssl) dependency is a
fork of OpenSSL to enable QUIC.
[OpenSSL](https://www.openssl.org/) is toolkit for general-purpose
cryptography and secure communication.

Node.js currently uses the quictls/openssl fork, which closely tracks
the main openssl/openssl releases with the addition of APIs to support
the QUIC protocol.
See [maintaining-openssl][] for more information.

### postject

The [postject](https://github.com/nodejs/postject) dependency is used for the
[Single Executable strategic initiative](https://github.com/nodejs/single-executable).

### simdjson

The [simdjson](https://github.com/simdjson/simdjson) dependency is
a C++ library for fast JSON parsing.

### simdutf

The [simdutf](https://github.com/simdutf/simdutf) dependency is
a C++ library for fast UTF-8 decoding and encoding.

### sqlite

The [sqlite](https://github.com/sqlite/sqlite) dependency is
an embedded SQL database engine written in C.

### undici

The [undici](https://github.com/nodejs/undici) dependency is an HTTP/1.1 client,
written from scratch for Node.js..
See [maintaining-http][] for more information.

### uvwasi

The [uvwasi](https://github.com/nodejs/uvwasi) dependency implements
the WASI system call API, so that WebAssembly runtimes can easily
implement WASI calls.
Under the hood, uvwasi leverages libuv where possible for maximum portability.
See [maintaining-web-assembly][] for more information.

### V8

[V8](https://chromium.googlesource.com/v8/v8.git/) is Google's open source
high-performance JavaScript and WebAssembly engine, written in C++.
See [maintaining-V8][] for more information.

### zlib

The [zlib](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/third_party/zlib)
dependency lossless data-compression library,
it comes from the Chromium team's zlib fork which incorporated
performance improvements not currently available in standard zlib.

[acorn]: #acorn
[ada]: #ada
[amaro]: #amaro
[base64]: #base64
[brotli]: #brotli
[c-ares]: #c-ares
[cjs-module-lexer]: #cjs-module-lexer
[corepack]: #corepack
[dependency-update-action]: ../../../.github/workflows/tools.yml
[googletest]: #googletest
[histogram]: #histogram
[icu-small]: #icu-small
[libuv]: #libuv
[llhttp]: #llhttp
[maintaining-V8]: ./maintaining-V8.md
[maintaining-cjs-module-lexer]: ./maintaining-cjs-module-lexer.md
[maintaining-http]: ./maintaining-http.md
[maintaining-icu]: ./maintaining-icu.md
[maintaining-openssl]: ./maintaining-openssl.md
[maintaining-web-assembly]: ./maintaining-web-assembly.md
[minimatch]: #minimatch
[nghttp2]: #nghttp2
[nghttp3]: #nghttp3
[ngtcp2]: #ngtcp2
[npm]: #npm
[openssl]: #openssl
[postject]: #postject
[simdjson]: #simdjson
[simdutf]: #simdutf
[sqlite]: #sqlite
[undici]: #undici
[update-openssl-action]: ../../../.github/workflows/update-openssl.yml
[uvwasi]: #uvwasi
[v8]: #v8
[zlib]: #zlib
