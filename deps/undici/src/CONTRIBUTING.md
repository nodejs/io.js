# Contributing to Undici

* [Guides](#guides)
  * [Update `llhttp`](#update-llhttp)
  * [Lint](#lint)
  * [Test](#test)
  * [Coverage](#coverage)
  * [Releases](#releases)
  * [Update `WPTs`](#update-wpts)
  * [Building for externally shared node builtins](#external-builds)
  *  [Benchmarks](#benchmarks)
  *  [Documentation](#documentation)
* [Developer's Certificate of Origin 1.1](#developers-certificate-of-origin)
  * [Moderation Policy](#moderation-policy)

<a id="guides"></a>
## Guides

This is a collection of guides on how to run and update `undici`, and how to run different parts of the project.

<a id="update-llhttp"></a>
### Update `llhttp`

The HTTP parser used by `undici` is a WebAssembly build of [`llhttp`](https://github.com/nodejs/llhttp).

While the project itself provides a way to compile targeting WebAssembly, at the moment we embed the sources
directly and compile the module in `undici`.

The `deps/llhttp/include` folder contains the C header files, while the `deps/llhttp/src` folder contains
the C source files needed to compile the module.

The `lib/llhttp` folder contains the `.js` transpiled assets required to implement a parser.

The following are the steps required to perform an update.

#### Clone the [llhttp](https://github.com/nodejs/llhttp) project

```bash
git clone git@github.com:nodejs/llhttp.git

cd llhttp
```

#### Checkout a `llhttp` release

```bash
git checkout <tag>
```

#### Install the `llhttp` dependencies

```bash
npm i
```

#### Run the wasm build script

> This requires [docker](https://www.docker.com/) installed on your machine.

```bash
npm run build-wasm
```

#### Copy the sources to `undici`

```bash
cp build/wasm/*.js <your-path-to-undici>/lib/llhttp/

cp build/wasm/*.js.map <your-path-to-undici>/lib/llhttp/

cp build/wasm/*.d.ts <your-path-to-undici>/lib/llhttp/

cp src/native/api.c src/native/http.c build/c/llhttp.c <your-path-to-undici>/deps/llhttp/src/

cp src/native/api.h build/llhttp.h <your-path-to-undici>/deps/llhttp/include/
```

#### Build the WebAssembly module in `undici`

> This requires [docker](https://www.docker.com/) installed on your machine.

```bash
cd <your-path-to-undici>

npm run build:wasm
```

#### Commit the contents of lib/llhttp

Create a commit which includes all of the updated files in lib/llhttp.

<a id="update-wpts"></a>
### Update `WPTs`

`undici` runs a subset of the [`web-platform-tests`](https://github.com/web-platform-tests/wpt).

### Requirements:
- [Node core utils](https://github.com/nodejs/node-core-utils) setup with credentials.

To update every test, run the following commands. Typically you would only need to update the tests in a specific directory.

```bash
git node wpt resources
git node wpt interfaces
git node wpt common
git node wpt fetch
git node wpt xhr
git node wpt websockets
git node wpt mimesniff
git node wpt storage
git node wpt service-workers
git node wpt eventsource
```

#### Run the tests

Run the tests to ensure that any new failures are marked as such.

You can mark tests as failing in their corresponding [status](./test/wpt/status) file.

```bash
npm run test:wpt
```

<a id="lint"></a>
### Lint

```bash
npm run lint
```

<a id="test"></a>
### Test

```bash
npm run test
```

<a id="coverage"></a>
### Coverage

```bash
npm run coverage
```

<a id="releases"></a>
### Issuing Releases

Release is automatic on commit to main which bumps the package.json version field.
Use the "Create release PR" github action to generate a release PR.

<a id="external-builds"></a>
### Building for externally shared node builtins

If you are packaging `undici` for a distro, this might help if you would like to use
an unbundled version instead of bundling one in `libnode.so`.

To enable this, pass `EXTERNAL_PATH=/path/to/global/node_modules/undici` to `build/wasm.js`.
Pass this path with `loader.js` appended to `--shared-builtin-undici/undici-path` in Node.js's `configure.py`.
If building on a non-Alpine Linux distribution, you may need to also set the `WASM_CC`, `WASM_CFLAGS`, `WASM_LDFLAGS` and `WASM_LDLIBS` environment variables before running `build/wasm.js`.
Similarly, you can set the `WASM_OPT` environment variable to utilize your own `wasm-opt` optimizer.

<a id="benchmarks"></a>
### Benchmarks

```bash
cd benchmarks && npm i && npm run bench
```

The benchmarks will be available at `http://localhost:3042`.

<a id="documentation"></a>
### Documentation

```bash
cd docs && npm i && npm run serve
```

The documentation will be available at `http://localhost:3000`.

<a id="developers-certificate-of-origin"></a>
## Developer's Certificate of Origin 1.1

By making a contribution to this project, I certify that:

* (a) The contribution was created in whole or in part by me and I
  have the right to submit it under the open source license
  indicated in the file; or

* (b) The contribution is based upon previous work that, to the best
  of my knowledge, is covered under an appropriate open source
  license and I have the right under that license to submit that
  work with modifications, whether created in whole or in part
  by me, under the same open source license (unless I am
  permitted to submit under a different license), as indicated
  in the file; or

* (c) The contribution was provided directly to me by some other
  person who certified (a), (b) or (c) and I have not modified
  it.

* (d) I understand and agree that this project and the contribution
  are public and that a record of the contribution (including all
  personal information I submit with it, including my sign-off) is
  maintained indefinitely and may be redistributed consistent with
  this project or the open source license(s) involved.

<a id="moderation-policy"></a>
### Moderation Policy

The [Node.js Moderation Policy] applies to this project.

[Node.js Moderation Policy]: https://github.com/nodejs/admin/blob/main/Moderation-Policy.md
