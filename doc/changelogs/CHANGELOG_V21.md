# Node.js 21 ChangeLog

<!--lint disable maximum-line-length no-literal-urls prohibited-strings-->

<table>
<tr>
<th>Current</th>
</tr>
<tr>
<td>
<a href="#21.7.3">21.7.3</a><br/>
<a href="#21.7.2">21.7.2</a><br/>
<a href="#21.7.1">21.7.1</a><br/>
<a href="#21.7.0">21.7.0</a><br/>
<a href="#21.6.2">21.6.2</a><br/>
<a href="#21.6.1">21.6.1</a><br/>
<a href="#21.6.0">21.6.0</a><br/>
<a href="#21.5.0">21.5.0</a><br/>
<a href="#21.4.0">21.4.0</a><br/>
<a href="#21.3.0">21.3.0</a><br/>
<a href="#21.2.0">21.2.0</a><br/>
<a href="#21.1.0">21.1.0</a><br/>
<a href="#21.0.0">21.0.0</a><br/>
</td>
</tr>
</table>

* Other Versions
  * [22.x](CHANGELOG_V22.md)
  * [20.x](CHANGELOG_V20.md)
  * [19.x](CHANGELOG_V19.md)
  * [18.x](CHANGELOG_V18.md)
  * [17.x](CHANGELOG_V17.md)
  * [16.x](CHANGELOG_V16.md)
  * [15.x](CHANGELOG_V15.md)
  * [14.x](CHANGELOG_V14.md)
  * [13.x](CHANGELOG_V13.md)
  * [12.x](CHANGELOG_V12.md)
  * [11.x](CHANGELOG_V11.md)
  * [10.x](CHANGELOG_V10.md)
  * [9.x](CHANGELOG_V9.md)
  * [8.x](CHANGELOG_V8.md)
  * [7.x](CHANGELOG_V7.md)
  * [6.x](CHANGELOG_V6.md)
  * [5.x](CHANGELOG_V5.md)
  * [4.x](CHANGELOG_V4.md)
  * [0.12.x](CHANGELOG_V012.md)
  * [0.10.x](CHANGELOG_V010.md)
  * [io.js](CHANGELOG_IOJS.md)
  * [Archive](CHANGELOG_ARCHIVE.md)

<a id="21.7.3"></a>

## 2024-04-10, Version 21.7.3 (Current), @RafaelGSS

This is a security release.

### Notable Changes

* CVE-2024-27980 - Command injection via args parameter of `child_process.spawn` without shell option enabled on Windows

### Commits

* \[[`9095c914ed`](https://github.com/nodejs/node/commit/9095c914ed)] - **src**: disallow direct .bat and .cmd file spawning (Ben Noordhuis) [nodejs-private/node-private#562](https://github.com/nodejs-private/node-private/pull/562)

<a id="21.7.2"></a>

## 2024-04-03, Version 21.7.2 (Current), @RafaelGSS prepared by @marco-ippolito

This is a security release.

### Notable changes

* CVE-2024-27983 - Assertion failed in node::http2::Http2Session::\~Http2Session() leads to HTTP/2 server crash- (High)
* CVE-2024-27982 - HTTP Request Smuggling via Content Length Obfuscation- (Medium)
* llhttp version 9.2.1
* undici version 6.11.1

### Commits

* \[[`3dfc10c851`](https://github.com/nodejs/node/commit/3dfc10c851)] - **deps**: update undici to 6.11.1 (Node.js GitHub Bot) [#52328](https://github.com/nodejs/node/pull/52328)
* \[[`aceea1c5e7`](https://github.com/nodejs/node/commit/aceea1c5e7)] - **deps**: update undici to 6.10.2 (Node.js GitHub Bot) [#52227](https://github.com/nodejs/node/pull/52227)
* \[[`5f0f96b275`](https://github.com/nodejs/node/commit/5f0f96b275)] - **deps**: update llhttp to 9.2.0 (Node.js GitHub Bot) [#51719](https://github.com/nodejs/node/pull/51719)
* \[[`1a65e98e22`](https://github.com/nodejs/node/commit/1a65e98e22)] - **http**: do not allow OBS fold in headers by default (Paolo Insogna) [nodejs-private/node-private#556](https://github.com/nodejs-private/node-private/pull/556)
* \[[`3bd39fb474`](https://github.com/nodejs/node/commit/3bd39fb474)] - **src**: ensure to close stream when destroying session (RafaelGSS) [nodejs-private/node-private#561](https://github.com/nodejs-private/node-private/pull/561)

<a id="21.7.1"></a>

## 2024-03-08, Version 21.7.1 (Current), @targos

### Notable Changes

This release reverts [#51389](https://github.com/nodejs/node/pull/51389), which
landed in Node.js 21.7.0. It is a documented feature that `t.after()` hooks are
run even if a test has no subtests. The hook can be used to clean up the test
itself.

### Commits

* \[[`0dfe810ac7`](https://github.com/nodejs/node/commit/0dfe810ac7)] - **benchmark**: update iterations of benchmark/async\_hooks/async-local- (Lei Shi) [#51420](https://github.com/nodejs/node/pull/51420)
* \[[`625c9e0ac9`](https://github.com/nodejs/node/commit/625c9e0ac9)] - **benchmark**: update iterations of benchmark/domain/domain-fn-args.js (Lei Shi) [#51408](https://github.com/nodejs/node/pull/51408)
* \[[`7ff3551bad`](https://github.com/nodejs/node/commit/7ff3551bad)] - **build**: fix arm64 host cross-compilation in GN (Cheng Zhao) [#51903](https://github.com/nodejs/node/pull/51903)
* \[[`fd86ea8b71`](https://github.com/nodejs/node/commit/fd86ea8b71)] - _**Revert**_ "**build**: workaround for node-core-utils" (Richard Lau) [#51975](https://github.com/nodejs/node/pull/51975)
* \[[`23c32ab3a7`](https://github.com/nodejs/node/commit/23c32ab3a7)] - **build**: respect the `NODE` env variable in `Makefile` (Antoine du Hamel) [#51743](https://github.com/nodejs/node/pull/51743)
* \[[`9617adc064`](https://github.com/nodejs/node/commit/9617adc064)] - _**Revert**_ "**build**: fix warning in cares under GN build" (Luigi Pinca) [#51865](https://github.com/nodejs/node/pull/51865)
* \[[`5864534095`](https://github.com/nodejs/node/commit/5864534095)] - **deps**: update nghttp2 to 1.60.0 (Node.js GitHub Bot) [#51948](https://github.com/nodejs/node/pull/51948)
* \[[`fcf235d623`](https://github.com/nodejs/node/commit/fcf235d623)] - **doc**: add policy for distribution (Geoffrey Booth) [#51918](https://github.com/nodejs/node/pull/51918)
* \[[`87d2acc8b1`](https://github.com/nodejs/node/commit/87d2acc8b1)] - **doc**: fix actual result of example is different in events (Deokjin Kim) [#51925](https://github.com/nodejs/node/pull/51925)
* \[[`5908c121c6`](https://github.com/nodejs/node/commit/5908c121c6)] - **doc**: clarify Corepack threat model (Antoine du Hamel) [#51917](https://github.com/nodejs/node/pull/51917)
* \[[`20e0ba3b94`](https://github.com/nodejs/node/commit/20e0ba3b94)] - **doc,module**: clarify hook chain execution sequence (Jacob Smith) [#51884](https://github.com/nodejs/node/pull/51884)
* \[[`4d997971ac`](https://github.com/nodejs/node/commit/4d997971ac)] - **lib**: make sure close net server (theanarkh) [#51929](https://github.com/nodejs/node/pull/51929)
* \[[`fcc6d54aa3`](https://github.com/nodejs/node/commit/fcc6d54aa3)] - **lib**: return directly if udp socket close before lookup (theanarkh) [#51914](https://github.com/nodejs/node/pull/51914)
* \[[`10aaabd158`](https://github.com/nodejs/node/commit/10aaabd158)] - **meta**: bump github/codeql-action from 3.23.2 to 3.24.6 (dependabot\[bot]) [#51942](https://github.com/nodejs/node/pull/51942)
* \[[`78f38a0143`](https://github.com/nodejs/node/commit/78f38a0143)] - **meta**: bump actions/upload-artifact from 4.3.0 to 4.3.1 (dependabot\[bot]) [#51941](https://github.com/nodejs/node/pull/51941)
* \[[`42ca5452c4`](https://github.com/nodejs/node/commit/42ca5452c4)] - **meta**: bump codecov/codecov-action from 4.0.1 to 4.1.0 (dependabot\[bot]) [#51940](https://github.com/nodejs/node/pull/51940)
* \[[`015a157375`](https://github.com/nodejs/node/commit/015a157375)] - **meta**: bump actions/cache from 4.0.0 to 4.0.1 (dependabot\[bot]) [#51939](https://github.com/nodejs/node/pull/51939)
* \[[`e476cb4a32`](https://github.com/nodejs/node/commit/e476cb4a32)] - **meta**: bump actions/download-artifact from 4.1.1 to 4.1.3 (dependabot\[bot]) [#51938](https://github.com/nodejs/node/pull/51938)
* \[[`67e8001790`](https://github.com/nodejs/node/commit/67e8001790)] - **meta**: bump actions/setup-node from 4.0.1 to 4.0.2 (dependabot\[bot]) [#51937](https://github.com/nodejs/node/pull/51937)
* \[[`50343636e8`](https://github.com/nodejs/node/commit/50343636e8)] - **src**: fix --disable-single-executable-application (Joyee Cheung) [#51808](https://github.com/nodejs/node/pull/51808)
* \[[`a48c9ca0db`](https://github.com/nodejs/node/commit/a48c9ca0db)] - **stream**: do not defer construction by one microtick (Matteo Collina) [#52005](https://github.com/nodejs/node/pull/52005)
* \[[`bee3b364f9`](https://github.com/nodejs/node/commit/bee3b364f9)] - **test**: add regression test for test\_runner after hook (Colin Ihrig) [#51998](https://github.com/nodejs/node/pull/51998)
* \[[`fff7f48f50`](https://github.com/nodejs/node/commit/fff7f48f50)] - **test**: reduce flakiness of `test-runner-output` (Antoine du Hamel) [#51952](https://github.com/nodejs/node/pull/51952)
* \[[`57ba8f5acb`](https://github.com/nodejs/node/commit/57ba8f5acb)] - **test**: fix flaky http-chunk-extensions-limit test (Ethan Arrowood) [#51943](https://github.com/nodejs/node/pull/51943)
* \[[`9d2c03990a`](https://github.com/nodejs/node/commit/9d2c03990a)] - **test**: remove flaky designation (Luigi Pinca) [#51736](https://github.com/nodejs/node/pull/51736)
* \[[`e992af81d3`](https://github.com/nodejs/node/commit/e992af81d3)] - **test**: skip SEA tests when SEA generation fails (Joyee Cheung) [#51887](https://github.com/nodejs/node/pull/51887)
* \[[`85aa6ca850`](https://github.com/nodejs/node/commit/85aa6ca850)] - _**Revert**_ "**test\_runner**: do not invoke after hook when test is empty" (Colin Ihrig) [#51998](https://github.com/nodejs/node/pull/51998)

<a id="21.7.0"></a>

## 2024-03-06, Version 21.7.0 (Current), @RafaelGSS prepared by @marco-ippolito

### Text Styling

* `util.styleText(format, text)`: This function returns a formatted text considering the `format` passed.

A new API has been created to format text based on `util.inspect.colors`, enabling you to style text in different colors (such as red, blue, ...) and emphasis (italic, bold, ...).

```cjs
const { styleText } = require('node:util');
const errorMessage = styleText('red', 'Error! Error!');
console.log(errorMessage);
```

Contributed by Rafael Gonzaga and Hemanth HM in [#51850](https://github.com/nodejs/node/pull/51850).

### Loading and parsing environment variables

* `process.loadEnvFile(path)`:
  * Use this function to load the `.env` file. If no path is specified, it automatically loads the .env file in the current directory. Example: `process.loadEnvFile()`.
  * Load a specific .env file by specifying its path. Example: `process.loadEnvFile('./development.env')`.

* `util.parseEnv(content)`:
  * Use this function to parse an existing string containing environment variable assignments.
  * Example usage: `require('node:util').parseEnv('HELLO=world')`.

Contributed by Yagiz Nizipli in [#51476](https://github.com/nodejs/node/pull/51476)

### Support for multi-line values for `.env` file

Node.js 21.7.0 will now support multi-line values in the .env file:

```text
MULTI_LINE="HELLO
WORLD"
```

Contributed by Ilyas Shabi [#51289](https://github.com/nodejs/node/pull/51289)

### sea: support embedding assets

Users can now include assets by adding a key-path dictionary
to the configuration as the `assets` field. At build time, Node.js
would read the assets from the specified paths and bundle them into
the preparation blob. In the generated executable, users can retrieve
the assets using the `sea.getAsset()` and `sea.getAssetAsBlob()` API.

```json
{
  "main": "/path/to/bundled/script.js",
  "output": "/path/to/write/the/generated/blob.blob",
  "assets": {
    "a.jpg": "/path/to/a.jpg",
    "b.txt": "/path/to/b.txt"
  }
}
```

The single-executable application can access the assets as follows:

```cjs
const { getAsset } = require('node:sea');
// Returns a copy of the data in an ArrayBuffer
const image = getAsset('a.jpg');
// Returns a string decoded from the asset as UTF8.
const text = getAsset('b.txt', 'utf8');
// Returns a Blob containing the asset without copying.
const blob = getAssetAsBlob('a.jpg');
```

Contributed by Joyee Cheung in [#50960](https://github.com/nodejs/node/pull/50960)

### vm: support using the default loader to handle dynamic import()

This patch adds support for using `vm.constants.USE_MAIN_CONTEXT_DEFAULT_LOADER` as the
`importModuleDynamically` option in all vm APIs that take this option except `vm.SourceTextModule`. This allows users to have a shortcut to support dynamic `import()` in the compiled code without missing the compilation cache if they don't need customization of the loading process. We emit an experimental warning when the `import()` is actually handled by the default loader through this option instead of requiring `--experimental-vm-modules`.

```js
const { Script, constants } = require('node:vm');
const { resolve } = require('node:path');
const { writeFileSync } = require('node:fs');

// Write test.js and test.txt to the directory where the current script
// being run is located.
writeFileSync(resolve(__dirname, 'test.mjs'),
              'export const filename = "./test.json";');
writeFileSync(resolve(__dirname, 'test.json'),
              '{"hello": "world"}');

// Compile a script that loads test.mjs and then test.json
// as if the script is placed in the same directory.
const script = new Script(
  `(async function() {
    const { filename } = await import('./test.mjs');
    return import(filename, { with: { type: 'json' } })
  })();`,
  {
    filename: resolve(__dirname, 'test-with-default.js'),
    importModuleDynamically: constants.USE_MAIN_CONTEXT_DEFAULT_LOADER,
  });

// { default: { hello: 'world' } }
script.runInThisContext().then(console.log);
```

Contributed by Joyee Cheung in [#51244](https://github.com/nodejs/node/pull/51244)

### crypto: implement crypto.hash()

This patch introduces a helper crypto.hash() that computes
a digest from the input at one shot. This can be 1.2-2x faster
than the object-based createHash() for smaller inputs (<= 5MB)
that are readily available (not streamed) and incur less memory
overhead since no intermediate objects will be created.

```js
const crypto = require('node:crypto');

// Hashing a string and return the result as a hex-encoded string.
const string = 'Node.js';
// 10b3493287f831e81a438811a1ffba01f8cec4b7
console.log(crypto.hash('sha1', string));
```

Contributed by Joyee Cheung in [#51044](https://github.com/nodejs/node/pull/51044)

### Other Notable Changes

* \[[`8ae0eeb7f4`](https://github.com/nodejs/node/commit/8ae0eeb7f4)] - **(SEMVER-MINOR)** **build**: build opt to set local location of headers (Michael Dawson) [#51525](https://github.com/nodejs/node/pull/51525)
* \[[`496776cc78`](https://github.com/nodejs/node/commit/496776cc78)] - **crypto**: update root certificates to NSS 3.98 (Node.js GitHub Bot) [#51794](https://github.com/nodejs/node/pull/51794)
* \[[`a8c9e6f7e9`](https://github.com/nodejs/node/commit/a8c9e6f7e9)] - **doc**: add zcbenz to collaborators (Cheng Zhao) [#51812](https://github.com/nodejs/node/pull/51812)
* \[[`adbf2d3837`](https://github.com/nodejs/node/commit/adbf2d3837)] - **doc**: add lemire to collaborators (Daniel Lemire) [#51572](https://github.com/nodejs/node/pull/51572)
* \[[`4b1c6839f4`](https://github.com/nodejs/node/commit/4b1c6839f4)] - **(SEMVER-MINOR)** **http2**: add h2 compat support for appendHeader (Tim Perry) [#51412](https://github.com/nodejs/node/pull/51412)
* \[[`d8aa2bac0b`](https://github.com/nodejs/node/commit/d8aa2bac0b)] - **(SEMVER-MINOR)** **http2**: add server handshake utility (snek) [#51172](https://github.com/nodejs/node/pull/51172)
* \[[`b9275d9039`](https://github.com/nodejs/node/commit/b9275d9039)] - **(SEMVER-MINOR)** **http2**: receive customsettings (Marten Richter) [#51323](https://github.com/nodejs/node/pull/51323)
* \[[`5a2d2daad5`](https://github.com/nodejs/node/commit/5a2d2daad5)] - **(SEMVER-MINOR)** **lib**: move encodingsMap to internal/util (Joyee Cheung) [#51044](https://github.com/nodejs/node/pull/51044)
* \[[`e8d9065262`](https://github.com/nodejs/node/commit/e8d9065262)] - **(SEMVER-MINOR)** **sea**: support sea.getRawAsset() (Joyee Cheung) [#50960](https://github.com/nodejs/node/pull/50960)
* \[[`47186fbad5`](https://github.com/nodejs/node/commit/47186fbad5)] - **(SEMVER-MINOR)** **src**: print string content better in BlobDeserializer (Joyee Cheung) [#50960](https://github.com/nodejs/node/pull/50960)
* \[[`119e045053`](https://github.com/nodejs/node/commit/119e045053)] - **(SEMVER-MINOR)** **src**: do not coerce dotenv paths (Tobias Nießen) [#51425](https://github.com/nodejs/node/pull/51425)
* \[[`9ab353af00`](https://github.com/nodejs/node/commit/9ab353af00)] - **(SEMVER-MINOR)** **stream**: implement `min` option for `ReadableStreamBYOBReader.read` (Mattias Buelens) [#50888](https://github.com/nodejs/node/pull/50888)

### Commits

* \[[`4ddb9b33d5`](https://github.com/nodejs/node/commit/4ddb9b33d5)] - **async\_hooks,inspector**: implement inspector api without async\_wrap (Gabriel Bota) [#51501](https://github.com/nodejs/node/pull/51501)
* \[[`7e06c11f55`](https://github.com/nodejs/node/commit/7e06c11f55)] - **benchmark**: update iterations of assert/deepequal-typedarrays.js (Lei Shi) [#51419](https://github.com/nodejs/node/pull/51419)
* \[[`72be232006`](https://github.com/nodejs/node/commit/72be232006)] - **benchmark**: update iterations of benchmark/assert/deepequal-map.js (Lei Shi) [#51416](https://github.com/nodejs/node/pull/51416)
* \[[`92e7c310cb`](https://github.com/nodejs/node/commit/92e7c310cb)] - **benchmark**: rename startup.js to startup-core.js (Joyee Cheung) [#51669](https://github.com/nodejs/node/pull/51669)
* \[[`c9ada533a2`](https://github.com/nodejs/node/commit/c9ada533a2)] - **build**: remove `librt` libs link for Android compatibility (BuShe Pie) [#51632](https://github.com/nodejs/node/pull/51632)
* \[[`86ac787889`](https://github.com/nodejs/node/commit/86ac787889)] - **build**: do not rely on gn\_helpers in GN build (Cheng Zhao) [#51439](https://github.com/nodejs/node/pull/51439)
* \[[`9be6b7ccf0`](https://github.com/nodejs/node/commit/9be6b7ccf0)] - **build**: fix warning in cares under GN build (Cheng Zhao) [#51687](https://github.com/nodejs/node/pull/51687)
* \[[`d1a8c2e989`](https://github.com/nodejs/node/commit/d1a8c2e989)] - **build**: fix building js2c with GN (Cheng Zhao) [#51818](https://github.com/nodejs/node/pull/51818)
* \[[`9840715dc0`](https://github.com/nodejs/node/commit/9840715dc0)] - **build**: encode non-ASCII Latin1 characters as one byte in JS2C (Joyee Cheung) [#51605](https://github.com/nodejs/node/pull/51605)
* \[[`8ae0eeb7f4`](https://github.com/nodejs/node/commit/8ae0eeb7f4)] - **(SEMVER-MINOR)** **build**: build opt to set local location of headers (Michael Dawson) [#51525](https://github.com/nodejs/node/pull/51525)
* \[[`1999719877`](https://github.com/nodejs/node/commit/1999719877)] - **build**: use macOS m1 machines for testing (Yagiz Nizipli) [#51620](https://github.com/nodejs/node/pull/51620)
* \[[`85f63f3d7d`](https://github.com/nodejs/node/commit/85f63f3d7d)] - **build**: check before removing %config% link (liudonghua) [#51437](https://github.com/nodejs/node/pull/51437)
* \[[`cc37959232`](https://github.com/nodejs/node/commit/cc37959232)] - **build**: increase parallel executions in github (Yagiz Nizipli) [#51554](https://github.com/nodejs/node/pull/51554)
* \[[`2921d55121`](https://github.com/nodejs/node/commit/2921d55121)] - **build**: remove copyright header in node.gni (Cheng Zhao) [#51535](https://github.com/nodejs/node/pull/51535)
* \[[`9da0926396`](https://github.com/nodejs/node/commit/9da0926396)] - **build**: update GN build files for ngtcp2 (Cheng Zhao) [#51313](https://github.com/nodejs/node/pull/51313)
* \[[`59117317f3`](https://github.com/nodejs/node/commit/59117317f3)] - **build,tools**: make addons tests work with GN (Cheng Zhao) [#50737](https://github.com/nodejs/node/pull/50737)
* \[[`78c226281c`](https://github.com/nodejs/node/commit/78c226281c)] - **(SEMVER-MINOR)** **crypto**: implement crypto.hash() (Joyee Cheung) [#51044](https://github.com/nodejs/node/pull/51044)
* \[[`496776cc78`](https://github.com/nodejs/node/commit/496776cc78)] - **crypto**: update root certificates to NSS 3.98 (Node.js GitHub Bot) [#51794](https://github.com/nodejs/node/pull/51794)
* \[[`17c554f1ca`](https://github.com/nodejs/node/commit/17c554f1ca)] - **crypto**: use EVP\_MD\_fetch and cache EVP\_MD for hashes (Joyee Cheung) [#51034](https://github.com/nodejs/node/pull/51034)
* \[[`014cc53541`](https://github.com/nodejs/node/commit/014cc53541)] - **deps**: upgrade npm to 10.5.0 (npm team) [#51913](https://github.com/nodejs/node/pull/51913)
* \[[`4ebb944800`](https://github.com/nodejs/node/commit/4ebb944800)] - **deps**: update undici to 6.6.2 (Michaël Zasso) [#51667](https://github.com/nodejs/node/pull/51667)
* \[[`3b29dff0ed`](https://github.com/nodejs/node/commit/3b29dff0ed)] - **deps**: update ngtcp2 to 1.3.0 (Node.js GitHub Bot) [#51796](https://github.com/nodejs/node/pull/51796)
* \[[`28c0ffb363`](https://github.com/nodejs/node/commit/28c0ffb363)] - **deps**: update simdjson to 3.7.0 (Daniel Lemire) [#51859](https://github.com/nodejs/node/pull/51859)
* \[[`58b1403693`](https://github.com/nodejs/node/commit/58b1403693)] - **deps**: update corepack to 0.25.2 (Node.js GitHub Bot) [#51810](https://github.com/nodejs/node/pull/51810)
* \[[`c7083720cc`](https://github.com/nodejs/node/commit/c7083720cc)] - **deps**: update c-ares to 1.27.0 (Node.js GitHub Bot) [#51846](https://github.com/nodejs/node/pull/51846)
* \[[`6d2699d40b`](https://github.com/nodejs/node/commit/6d2699d40b)] - **deps**: update timezone to 2024a (Michaël Zasso) [#51723](https://github.com/nodejs/node/pull/51723)
* \[[`8d2222714d`](https://github.com/nodejs/node/commit/8d2222714d)] - **deps**: update icu to 74.2 (Michaël Zasso) [#51723](https://github.com/nodejs/node/pull/51723)
* \[[`c3dbd7cccd`](https://github.com/nodejs/node/commit/c3dbd7cccd)] - **deps**: update c-ares to 1.26.0 (Node.js GitHub Bot) [#51582](https://github.com/nodejs/node/pull/51582)
* \[[`dfc3811056`](https://github.com/nodejs/node/commit/dfc3811056)] - **deps**: update googletest to 6a59382 (Node.js GitHub Bot) [#51580](https://github.com/nodejs/node/pull/51580)
* \[[`8235c2676e`](https://github.com/nodejs/node/commit/8235c2676e)] - **deps**: update nghttp2 to 1.59.0 (Node.js GitHub Bot) [#51581](https://github.com/nodejs/node/pull/51581)
* \[[`2ad665e24f`](https://github.com/nodejs/node/commit/2ad665e24f)] - **deps**: V8: cherry-pick efb1133eb894 (Joyee Cheung) [#51551](https://github.com/nodejs/node/pull/51551)
* \[[`e5db8d416f`](https://github.com/nodejs/node/commit/e5db8d416f)] - **deps**: update corepack to 0.24.1 (Node.js GitHub Bot) [#51459](https://github.com/nodejs/node/pull/51459)
* \[[`fe597de72e`](https://github.com/nodejs/node/commit/fe597de72e)] - **deps**: update ada to 2.7.6 (Node.js GitHub Bot) [#51542](https://github.com/nodejs/node/pull/51542)
* \[[`5aca6f54f9`](https://github.com/nodejs/node/commit/5aca6f54f9)] - **deps**: update ada to 2.7.5 (Node.js GitHub Bot) [#51542](https://github.com/nodejs/node/pull/51542)
* \[[`8f63f6ff57`](https://github.com/nodejs/node/commit/8f63f6ff57)] - **deps**: update ngtcp2 to 1.2.0 (Node.js GitHub Bot) [#51584](https://github.com/nodejs/node/pull/51584)
* \[[`a04aa36ce8`](https://github.com/nodejs/node/commit/a04aa36ce8)] - **deps**: update googletest to 7c07a86 (Node.js GitHub Bot) [#51458](https://github.com/nodejs/node/pull/51458)
* \[[`4b1d25b68d`](https://github.com/nodejs/node/commit/4b1d25b68d)] - **deps**: update ngtcp2 to 1.1.0 (Node.js GitHub Bot) [#51319](https://github.com/nodejs/node/pull/51319)
* \[[`682f4f67b0`](https://github.com/nodejs/node/commit/682f4f67b0)] - **deps**: update acorn-walk to 8.3.2 (Node.js GitHub Bot) [#51457](https://github.com/nodejs/node/pull/51457)
* \[[`365a9dc2cb`](https://github.com/nodejs/node/commit/365a9dc2cb)] - **deps**: update timezone to 2023d (Node.js GitHub Bot) [#51461](https://github.com/nodejs/node/pull/51461)
* \[[`40e8b362a2`](https://github.com/nodejs/node/commit/40e8b362a2)] - **deps**: update base64 to 0.5.2 (Node.js GitHub Bot) [#51455](https://github.com/nodejs/node/pull/51455)
* \[[`139a626264`](https://github.com/nodejs/node/commit/139a626264)] - **deps**: compile c-ares with C11 support (Michaël Zasso) [#51410](https://github.com/nodejs/node/pull/51410)
* \[[`1cc37c4355`](https://github.com/nodejs/node/commit/1cc37c4355)] - **deps**: upgrade npm to 10.3.0 (npm team) [#51431](https://github.com/nodejs/node/pull/51431)
* \[[`942e10f5b5`](https://github.com/nodejs/node/commit/942e10f5b5)] - **deps**: update c-ares to 1.25.0 (Node.js GitHub Bot) [#51385](https://github.com/nodejs/node/pull/51385)
* \[[`17cb4af5a9`](https://github.com/nodejs/node/commit/17cb4af5a9)] - **deps**: update uvwasi to 0.0.20 and fixup tests (Michael Dawson) [#51355](https://github.com/nodejs/node/pull/51355)
* \[[`76582e434c`](https://github.com/nodejs/node/commit/76582e434c)] - **deps**: add nghttp3/\*\*/.deps to .gitignore (Luigi Pinca) [#51400](https://github.com/nodejs/node/pull/51400)
* \[[`4a889e8ea3`](https://github.com/nodejs/node/commit/4a889e8ea3)] - **doc**: add stability index to crypto.hash() (Joyee Cheung) [#51978](https://github.com/nodejs/node/pull/51978)
* \[[`3fdaeba9e6`](https://github.com/nodejs/node/commit/3fdaeba9e6)] - **doc**: remove redundant backquote which breaks sentence (JounQin) [#51904](https://github.com/nodejs/node/pull/51904)
* \[[`58747734a2`](https://github.com/nodejs/node/commit/58747734a2)] - **doc**: update node-api/node-addon-api team link to sharing project news (Ulises Gascón) [#51877](https://github.com/nodejs/node/pull/51877)
* \[[`2cdfe35437`](https://github.com/nodejs/node/commit/2cdfe35437)] - **doc**: add website team to sharing project news (Ulises Gascón) [#49002](https://github.com/nodejs/node/pull/49002)
* \[[`db30428f06`](https://github.com/nodejs/node/commit/db30428f06)] - **doc**: update guide link for Event Loop (Shrujal Shah) [#51874](https://github.com/nodejs/node/pull/51874)
* \[[`a5a17a18e3`](https://github.com/nodejs/node/commit/a5a17a18e3)] - **doc**: change `ExperimentalWarnings` to `ExperimentalWarning` (Ameet Kaustav) [#51741](https://github.com/nodejs/node/pull/51741)
* \[[`32d92aca1f`](https://github.com/nodejs/node/commit/32d92aca1f)] - **doc**: add Paolo to TSC members (Michael Dawson) [#51825](https://github.com/nodejs/node/pull/51825)
* \[[`2de1e85268`](https://github.com/nodejs/node/commit/2de1e85268)] - **doc**: reserve 123 for Electron 30 (Keeley Hammond) [#51803](https://github.com/nodejs/node/pull/51803)
* \[[`ad8cefcbf1`](https://github.com/nodejs/node/commit/ad8cefcbf1)] - **doc**: add mention to GPG\_TTY (Rafael Gonzaga) [#51806](https://github.com/nodejs/node/pull/51806)
* \[[`a8c9e6f7e9`](https://github.com/nodejs/node/commit/a8c9e6f7e9)] - **doc**: add zcbenz to collaborators (Cheng Zhao) [#51812](https://github.com/nodejs/node/pull/51812)
* \[[`4e0c79bea9`](https://github.com/nodejs/node/commit/4e0c79bea9)] - **doc**: add entry to stewards (Rafael Gonzaga) [#51760](https://github.com/nodejs/node/pull/51760)
* \[[`7fa30812ea`](https://github.com/nodejs/node/commit/7fa30812ea)] - **doc**: update technical priorities for 2023 (Jean Burellier) [#47523](https://github.com/nodejs/node/pull/47523)
* \[[`af6b5b1722`](https://github.com/nodejs/node/commit/af6b5b1722)] - **doc**: mark isWebAssemblyCompiledModule eol (Marco Ippolito) [#51442](https://github.com/nodejs/node/pull/51442)
* \[[`a62f69ecad`](https://github.com/nodejs/node/commit/a62f69ecad)] - **doc**: fix `globals.md` introduction (Antoine du Hamel) [#51742](https://github.com/nodejs/node/pull/51742)
* \[[`519dc8aad6`](https://github.com/nodejs/node/commit/519dc8aad6)] - **doc**: updates for better json generating (Dmitry Semigradsky) [#51592](https://github.com/nodejs/node/pull/51592)
* \[[`1b45ca4e38`](https://github.com/nodejs/node/commit/1b45ca4e38)] - **doc**: document the GN build (Cheng Zhao) [#51676](https://github.com/nodejs/node/pull/51676)
* \[[`37182c4c1f`](https://github.com/nodejs/node/commit/37182c4c1f)] - **doc**: fix uncaught exception example (Gabriel Schulhof) [#51638](https://github.com/nodejs/node/pull/51638)
* \[[`c9be260b7d`](https://github.com/nodejs/node/commit/c9be260b7d)] - **doc**: clarify execution of `after` hook on test suite completion (Ognjen Jevremović) [#51523](https://github.com/nodejs/node/pull/51523)
* \[[`8c0a257021`](https://github.com/nodejs/node/commit/8c0a257021)] - **doc**: fix `dns.lookup` and `dnsPromises.lookup` description (Duncan Chiu) [#51517](https://github.com/nodejs/node/pull/51517)
* \[[`177e13cb0d`](https://github.com/nodejs/node/commit/177e13cb0d)] - **doc**: note that path.normalize deviates from POSIX (Tobias Nießen) [#51513](https://github.com/nodejs/node/pull/51513)
* \[[`adbf2d3837`](https://github.com/nodejs/node/commit/adbf2d3837)] - **doc**: add lemire to collaborators (Daniel Lemire) [#51572](https://github.com/nodejs/node/pull/51572)
* \[[`fd2a3cef57`](https://github.com/nodejs/node/commit/fd2a3cef57)] - **doc**: fix historical experimental fetch flag (Kenrick) [#51506](https://github.com/nodejs/node/pull/51506)
* \[[`1d40a0067a`](https://github.com/nodejs/node/commit/1d40a0067a)] - **doc**: fix type of connectionAttempt parameter (Rafael Gonzaga) [#51490](https://github.com/nodejs/node/pull/51490)
* \[[`d3b78051ce`](https://github.com/nodejs/node/commit/d3b78051ce)] - **doc**: remove reference to resolved child\_process v8 issue (Ian Kerins) [#51467](https://github.com/nodejs/node/pull/51467)
* \[[`2bf371886a`](https://github.com/nodejs/node/commit/2bf371886a)] - **doc**: update typos (Aranđel Šarenac) [#51475](https://github.com/nodejs/node/pull/51475)
* \[[`10f95283c6`](https://github.com/nodejs/node/commit/10f95283c6)] - **doc**: add notes on inspector breakpoints (Chengzhong Wu) [#51417](https://github.com/nodejs/node/pull/51417)
* \[[`4ea194ab33`](https://github.com/nodejs/node/commit/4ea194ab33)] - **doc**: add links in `offboarding.md` (Antoine du Hamel) [#51440](https://github.com/nodejs/node/pull/51440)
* \[[`fc5629616b`](https://github.com/nodejs/node/commit/fc5629616b)] - **doc**: fix spelling mistake (u9g) [#51454](https://github.com/nodejs/node/pull/51454)
* \[[`70e88cf159`](https://github.com/nodejs/node/commit/70e88cf159)] - **doc**: add check for security reverts (Michael Dawson) [#51376](https://github.com/nodejs/node/pull/51376)
* \[[`74d4e382a7`](https://github.com/nodejs/node/commit/74d4e382a7)] - **doc**: fix some policy scope typos (Tim Kuijsten) [#51234](https://github.com/nodejs/node/pull/51234)
* \[[`d7658ca6a2`](https://github.com/nodejs/node/commit/d7658ca6a2)] - **doc**: improve subtests documentation (Marco Ippolito) [#51379](https://github.com/nodejs/node/pull/51379)
* \[[`c18a813638`](https://github.com/nodejs/node/commit/c18a813638)] - **doc**: add missing word in `child_process.md` (Joseph Joy) [#50370](https://github.com/nodejs/node/pull/50370)
* \[[`fabd5c4b21`](https://github.com/nodejs/node/commit/fabd5c4b21)] - **doc**: fixup alignment of warning subsection (James M Snell) [#51374](https://github.com/nodejs/node/pull/51374)
* \[[`80c750c8c1`](https://github.com/nodejs/node/commit/80c750c8c1)] - **doc,crypto**: further clarify RSA\_PKCS1\_PADDING support (Tobias Nießen) [#51799](https://github.com/nodejs/node/pull/51799)
* \[[`b53919d988`](https://github.com/nodejs/node/commit/b53919d988)] - **doc,crypto**: add changelog and note about disabled RSA\_PKCS1\_PADDING (Filip Skokan) [#51782](https://github.com/nodejs/node/pull/51782)
* \[[`08832e76d3`](https://github.com/nodejs/node/commit/08832e76d3)] - **esm**: improve error when calling `import.meta.resolve` from `data:` URL (Antoine du Hamel) [#49516](https://github.com/nodejs/node/pull/49516)
* \[[`78f818069a`](https://github.com/nodejs/node/commit/78f818069a)] - **events**: no stopPropagation call in cancelBubble (mert.altin) [#50405](https://github.com/nodejs/node/pull/50405)
* \[[`f130a33438`](https://github.com/nodejs/node/commit/f130a33438)] - _**Revert**_ "**fs**: remove workaround for `esm` package" (Jeremy Meng) [#50907](https://github.com/nodejs/node/pull/50907)
* \[[`5a8af3d362`](https://github.com/nodejs/node/commit/5a8af3d362)] - **fs**: load rimraf lazily in fs/promises (Joyee Cheung) [#51617](https://github.com/nodejs/node/pull/51617)
* \[[`15a7f2103b`](https://github.com/nodejs/node/commit/15a7f2103b)] - **fs**: remove race condition for recursive watch on Linux (Matteo Collina) [#51406](https://github.com/nodejs/node/pull/51406)
* \[[`d8bb4b2c1e`](https://github.com/nodejs/node/commit/d8bb4b2c1e)] - **fs**: update jsdoc for `filehandle.createWriteStream` and `appendFile` (Jungku Lee) [#51494](https://github.com/nodejs/node/pull/51494)
* \[[`e8fffebdd3`](https://github.com/nodejs/node/commit/e8fffebdd3)] - **fs,test**: add URL to string to fs.watch (Rafael Gonzaga) [#51346](https://github.com/nodejs/node/pull/51346)
* \[[`ec17fd73cc`](https://github.com/nodejs/node/commit/ec17fd73cc)] - **http**: fix `close` return value mismatch between doc and implementation (kylo5aby) [#51797](https://github.com/nodejs/node/pull/51797)
* \[[`b8e7a87aa9`](https://github.com/nodejs/node/commit/b8e7a87aa9)] - **http**: split set-cookie when using setHeaders (Marco Ippolito) [#51649](https://github.com/nodejs/node/pull/51649)
* \[[`682951af60`](https://github.com/nodejs/node/commit/682951af60)] - **http2**: close idle connections when allowHTTP1 is true (xsbchen) [#51569](https://github.com/nodejs/node/pull/51569)
* \[[`4b1c6839f4`](https://github.com/nodejs/node/commit/4b1c6839f4)] - **(SEMVER-MINOR)** **http2**: add h2 compat support for appendHeader (Tim Perry) [#51412](https://github.com/nodejs/node/pull/51412)
* \[[`d8aa2bac0b`](https://github.com/nodejs/node/commit/d8aa2bac0b)] - **(SEMVER-MINOR)** **http2**: add server handshake utility (snek) [#51172](https://github.com/nodejs/node/pull/51172)
* \[[`b9275d9039`](https://github.com/nodejs/node/commit/b9275d9039)] - **(SEMVER-MINOR)** **http2**: receive customsettings (Marten Richter) [#51323](https://github.com/nodejs/node/pull/51323)
* \[[`58e2015a03`](https://github.com/nodejs/node/commit/58e2015a03)] - **inspector**: add NodeRuntime.waitingForDebugger event (mary marchini) [#51560](https://github.com/nodejs/node/pull/51560)
* \[[`af32d433ee`](https://github.com/nodejs/node/commit/af32d433ee)] - **lib**: account for cwd access from snapshot serialization cb (Anna Henningsen) [#51901](https://github.com/nodejs/node/pull/51901)
* \[[`1edbc7d353`](https://github.com/nodejs/node/commit/1edbc7d353)] - **lib**: fix http client socket path (theanarkh) [#51900](https://github.com/nodejs/node/pull/51900)
* \[[`4dfc9e092e`](https://github.com/nodejs/node/commit/4dfc9e092e)] - **lib**: only build the ESM facade for builtins when they are needed (Joyee Cheung) [#51669](https://github.com/nodejs/node/pull/51669)
* \[[`5a2d2daad5`](https://github.com/nodejs/node/commit/5a2d2daad5)] - **(SEMVER-MINOR)** **lib**: move encodingsMap to internal/util (Joyee Cheung) [#51044](https://github.com/nodejs/node/pull/51044)
* \[[`eb1089ab17`](https://github.com/nodejs/node/commit/eb1089ab17)] - **lib**: do not access process.noDeprecation at build time (Joyee Cheung) [#51447](https://github.com/nodejs/node/pull/51447)
* \[[`614ca327c8`](https://github.com/nodejs/node/commit/614ca327c8)] - **lib**: add assertion for user ESM execution (Joyee Cheung) [#51748](https://github.com/nodejs/node/pull/51748)
* \[[`77ae03f723`](https://github.com/nodejs/node/commit/77ae03f723)] - **lib**: create global console properties at snapshot build time (Joyee Cheung) [#51700](https://github.com/nodejs/node/pull/51700)
* \[[`7f698f064e`](https://github.com/nodejs/node/commit/7f698f064e)] - **lib**: define FormData and fetch etc. in the built-in snapshot (Joyee Cheung) [#51598](https://github.com/nodejs/node/pull/51598)
* \[[`4b583bfcc5`](https://github.com/nodejs/node/commit/4b583bfcc5)] - **lib**: allow checking the test result from afterEach (Tim Stableford) [#51485](https://github.com/nodejs/node/pull/51485)
* \[[`ec60639cc0`](https://github.com/nodejs/node/commit/ec60639cc0)] - **lib**: remove unnecessary refreshHrtimeBuffer() (Joyee Cheung) [#51446](https://github.com/nodejs/node/pull/51446)
* \[[`8dc3f91eb4`](https://github.com/nodejs/node/commit/8dc3f91eb4)] - **lib,src**: extract sourceMappingURL from module (unbyte) [#51690](https://github.com/nodejs/node/pull/51690)
* \[[`84c71fa895`](https://github.com/nodejs/node/commit/84c71fa895)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#51726](https://github.com/nodejs/node/pull/51726)
* \[[`e5c52a2a6b`](https://github.com/nodejs/node/commit/e5c52a2a6b)] - **meta**: bump codecov/codecov-action from 3.1.4 to 4.0.1 (dependabot\[bot]) [#51648](https://github.com/nodejs/node/pull/51648)
* \[[`16aa6e5341`](https://github.com/nodejs/node/commit/16aa6e5341)] - **meta**: bump actions/download-artifact from 4.1.0 to 4.1.1 (dependabot\[bot]) [#51644](https://github.com/nodejs/node/pull/51644)
* \[[`97825603ae`](https://github.com/nodejs/node/commit/97825603ae)] - **meta**: bump actions/upload-artifact from 4.0.0 to 4.3.0 (dependabot\[bot]) [#51643](https://github.com/nodejs/node/pull/51643)
* \[[`51f0d80876`](https://github.com/nodejs/node/commit/51f0d80876)] - **meta**: bump step-security/harden-runner from 2.6.1 to 2.7.0 (dependabot\[bot]) [#51641](https://github.com/nodejs/node/pull/51641)
* \[[`97e3cb5844`](https://github.com/nodejs/node/commit/97e3cb5844)] - **meta**: bump actions/cache from 3.3.2 to 4.0.0 (dependabot\[bot]) [#51640](https://github.com/nodejs/node/pull/51640)
* \[[`dcf5f28d68`](https://github.com/nodejs/node/commit/dcf5f28d68)] - **meta**: bump github/codeql-action from 3.22.12 to 3.23.2 (dependabot\[bot]) [#51639](https://github.com/nodejs/node/pull/51639)
* \[[`c4a28b2211`](https://github.com/nodejs/node/commit/c4a28b2211)] - **meta**: add .mailmap entry for lemire (Daniel Lemire) [#51600](https://github.com/nodejs/node/pull/51600)
* \[[`dbf44744ba`](https://github.com/nodejs/node/commit/dbf44744ba)] - **meta**: mark security-wg codeowner for deps folder (Marco Ippolito) [#51529](https://github.com/nodejs/node/pull/51529)
* \[[`16fea71d08`](https://github.com/nodejs/node/commit/16fea71d08)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#51468](https://github.com/nodejs/node/pull/51468)
* \[[`015c4dcacf`](https://github.com/nodejs/node/commit/015c4dcacf)] - **meta**: move RaisinTen to emeritus and remove from strategic initiatives (Darshan Sen) [#51411](https://github.com/nodejs/node/pull/51411)
* \[[`e942dc1d0c`](https://github.com/nodejs/node/commit/e942dc1d0c)] - **meta**: add .temp and .lock tags to ignore (Rafael Gonzaga) [#51343](https://github.com/nodejs/node/pull/51343)
* \[[`595542e330`](https://github.com/nodejs/node/commit/595542e330)] - **meta**: bump actions/setup-python from 4.7.1 to 5.0.0 (dependabot\[bot]) [#51335](https://github.com/nodejs/node/pull/51335)
* \[[`6c3ba73d03`](https://github.com/nodejs/node/commit/6c3ba73d03)] - **meta**: bump actions/setup-node from 4.0.0 to 4.0.1 (dependabot\[bot]) [#51334](https://github.com/nodejs/node/pull/51334)
* \[[`e4f9f0a260`](https://github.com/nodejs/node/commit/e4f9f0a260)] - **meta**: bump github/codeql-action from 2.22.8 to 3.22.12 (dependabot\[bot]) [#51333](https://github.com/nodejs/node/pull/51333)
* \[[`77598c3a8e`](https://github.com/nodejs/node/commit/77598c3a8e)] - **meta**: bump actions/stale from 8.0.0 to 9.0.0 (dependabot\[bot]) [#51332](https://github.com/nodejs/node/pull/51332)
* \[[`22a11c32c0`](https://github.com/nodejs/node/commit/22a11c32c0)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#51329](https://github.com/nodejs/node/pull/51329)
* \[[`391aeb1996`](https://github.com/nodejs/node/commit/391aeb1996)] - **module**: fix crash when built-in module export a `default` key (Antoine du Hamel) [#51481](https://github.com/nodejs/node/pull/51481)
* \[[`615b0ae307`](https://github.com/nodejs/node/commit/615b0ae307)] - **module**: fix `--preserve-symlinks-main` (per4uk) [#51312](https://github.com/nodejs/node/pull/51312)
* \[[`c6cc3ed3b4`](https://github.com/nodejs/node/commit/c6cc3ed3b4)] - **net**: fix connect crash when call destroy in lookup handler (theanarkh) [#51826](https://github.com/nodejs/node/pull/51826)
* \[[`63e0ceb48f`](https://github.com/nodejs/node/commit/63e0ceb48f)] - **net**: fix example IPv4 in dns docs (Aras Abbasi) [#51377](https://github.com/nodejs/node/pull/51377)
* \[[`bc6f33d8d1`](https://github.com/nodejs/node/commit/bc6f33d8d1)] - **node-api**: make napi\_get\_buffer\_info check if passed buffer is valid (Janrupf) [#51571](https://github.com/nodejs/node/pull/51571)
* \[[`5b94ff44ec`](https://github.com/nodejs/node/commit/5b94ff44ec)] - **node-api**: move NAPI\_EXPERIMENTAL definition to gyp file (Gabriel Schulhof) [#51254](https://github.com/nodejs/node/pull/51254)
* \[[`66c11f31c3`](https://github.com/nodejs/node/commit/66c11f31c3)] - **node-api**: optimize napi\_set\_property for perf (Mert Can Altın) [#50282](https://github.com/nodejs/node/pull/50282)
* \[[`cb621863c6`](https://github.com/nodejs/node/commit/cb621863c6)] - **perf\_hooks**: performance milestone time origin timestamp improvement (IlyasShabi) [#51713](https://github.com/nodejs/node/pull/51713)
* \[[`4d06d80675`](https://github.com/nodejs/node/commit/4d06d80675)] - **quic**: various additional cleanups, fixes in Endpoint (James M Snell) [#51310](https://github.com/nodejs/node/pull/51310)
* \[[`3e579ab2fd`](https://github.com/nodejs/node/commit/3e579ab2fd)] - **repl**: fix `NO_COLORS` env var is ignored (Moshe Atlow) [#51568](https://github.com/nodejs/node/pull/51568)
* \[[`7ceb6d6700`](https://github.com/nodejs/node/commit/7ceb6d6700)] - **sea**: update stability index (Joyee Cheung) [#51774](https://github.com/nodejs/node/pull/51774)
* \[[`e8d9065262`](https://github.com/nodejs/node/commit/e8d9065262)] - **(SEMVER-MINOR)** **sea**: support sea.getRawAsset() (Joyee Cheung) [#50960](https://github.com/nodejs/node/pull/50960)
* \[[`cea5295c16`](https://github.com/nodejs/node/commit/cea5295c16)] - **(SEMVER-MINOR)** **sea**: support embedding assets (Joyee Cheung) [#50960](https://github.com/nodejs/node/pull/50960)
* \[[`7543e774bd`](https://github.com/nodejs/node/commit/7543e774bd)] - **src**: simplify direct queries of env vars in C++ land (Joyee Cheung) [#51829](https://github.com/nodejs/node/pull/51829)
* \[[`8f10543c58`](https://github.com/nodejs/node/commit/8f10543c58)] - **src**: stop the profiler and the inspector before snapshot serialization (Joyee Cheung) [#51815](https://github.com/nodejs/node/pull/51815)
* \[[`ccc76bbfd7`](https://github.com/nodejs/node/commit/ccc76bbfd7)] - **src**: simplify embedder entry point execution (Joyee Cheung) [#51557](https://github.com/nodejs/node/pull/51557)
* \[[`0c41210865`](https://github.com/nodejs/node/commit/0c41210865)] - **src**: compile code eagerly in snapshot builder (Joyee Cheung) [#51672](https://github.com/nodejs/node/pull/51672)
* \[[`2a46dc7b86`](https://github.com/nodejs/node/commit/2a46dc7b86)] - **src**: check empty before accessing string (Cheng Zhao) [#51665](https://github.com/nodejs/node/pull/51665)
* \[[`47186fbad5`](https://github.com/nodejs/node/commit/47186fbad5)] - **(SEMVER-MINOR)** **src**: print string content better in BlobDeserializer (Joyee Cheung) [#50960](https://github.com/nodejs/node/pull/50960)
* \[[`6603d32ce3`](https://github.com/nodejs/node/commit/6603d32ce3)] - **src**: fix vm bug for configurable globalThis (F. Hinkelmann) [#51602](https://github.com/nodejs/node/pull/51602)
* \[[`c7912c3d5a`](https://github.com/nodejs/node/commit/c7912c3d5a)] - **(SEMVER-MINOR)** **src**: support multi-line values for .env file (IlyasShabi) [#51289](https://github.com/nodejs/node/pull/51289)
* \[[`b8ae5c27c6`](https://github.com/nodejs/node/commit/b8ae5c27c6)] - **(SEMVER-MINOR)** **src**: add `process.loadEnvFile` and `util.parseEnv` (Yagiz Nizipli) [#51476](https://github.com/nodejs/node/pull/51476)
* \[[`e3a63843f2`](https://github.com/nodejs/node/commit/e3a63843f2)] - **src**: terminate correctly double-quote in env variable (Marco Ippolito) [#51510](https://github.com/nodejs/node/pull/51510)
* \[[`119e045053`](https://github.com/nodejs/node/commit/119e045053)] - **(SEMVER-MINOR)** **src**: do not coerce dotenv paths (Tobias Nießen) [#51425](https://github.com/nodejs/node/pull/51425)
* \[[`b271cc5b16`](https://github.com/nodejs/node/commit/b271cc5b16)] - **src**: refactor `GetCreationContext` calls (Jungku Lee) [#51367](https://github.com/nodejs/node/pull/51367)
* \[[`36e42aa570`](https://github.com/nodejs/node/commit/36e42aa570)] - **src**: do not read string out of bounds (Cheng Zhao) [#51358](https://github.com/nodejs/node/pull/51358)
* \[[`8ea7d79082`](https://github.com/nodejs/node/commit/8ea7d79082)] - **src**: avoid shadowed string in fs\_permission (Shelley Vohr) [#51123](https://github.com/nodejs/node/pull/51123)
* \[[`5b06af7814`](https://github.com/nodejs/node/commit/5b06af7814)] - **stream**: fix eventNames() to not return not defined events (IlyasShabi) [#51331](https://github.com/nodejs/node/pull/51331)
* \[[`438b7fd049`](https://github.com/nodejs/node/commit/438b7fd049)] - **stream**: fix cloned webstreams not being unref correctly (tsctx) [#51526](https://github.com/nodejs/node/pull/51526)
* \[[`9ab353af00`](https://github.com/nodejs/node/commit/9ab353af00)] - **(SEMVER-MINOR)** **stream**: implement `min` option for `ReadableStreamBYOBReader.read` (Mattias Buelens) [#50888](https://github.com/nodejs/node/pull/50888)
* \[[`17ab5ae570`](https://github.com/nodejs/node/commit/17ab5ae570)] - **test**: fix unreliable assumption in js-native-api/test\_cannot\_run\_js (Joyee Cheung) [#51898](https://github.com/nodejs/node/pull/51898)
* \[[`e2c51385c7`](https://github.com/nodejs/node/commit/e2c51385c7)] - **test**: test surrogate pair filenames on windows (Mert Can Altın) [#51800](https://github.com/nodejs/node/pull/51800)
* \[[`049e5f5e8c`](https://github.com/nodejs/node/commit/049e5f5e8c)] - **test**: deflake test-http2-large-write-multiple-requests (Joyee Cheung) [#51863](https://github.com/nodejs/node/pull/51863)
* \[[`2bf03ee678`](https://github.com/nodejs/node/commit/2bf03ee678)] - **test**: fix test-debugger-profile for coverage generation (Joyee Cheung) [#51816](https://github.com/nodejs/node/pull/51816)
* \[[`d47a95f3b1`](https://github.com/nodejs/node/commit/d47a95f3b1)] - **test**: fix test-bootstrap-modules for coverage generation (Joyee Cheung) [#51816](https://github.com/nodejs/node/pull/51816)
* \[[`c0918f082f`](https://github.com/nodejs/node/commit/c0918f082f)] - **test**: ensure delay in recursive fs watch tests (Joyee Cheung) [#51842](https://github.com/nodejs/node/pull/51842)
* \[[`1f6551dda2`](https://github.com/nodejs/node/commit/1f6551dda2)] - **test**: fix test-child-process-fork-net (Joyee Cheung) [#51841](https://github.com/nodejs/node/pull/51841)
* \[[`f845a16c58`](https://github.com/nodejs/node/commit/f845a16c58)] - **test**: split wasi tests (Joyee Cheung) [#51836](https://github.com/nodejs/node/pull/51836)
* \[[`275cea0fdb`](https://github.com/nodejs/node/commit/275cea0fdb)] - **test**: mark test-wasi as flaky on Windows on ARM (Joyee Cheung) [#51834](https://github.com/nodejs/node/pull/51834)
* \[[`2fb620cd00`](https://github.com/nodejs/node/commit/2fb620cd00)] - **test**: remove test-fs-stat-bigint flaky designation (Luigi Pinca) [#51735](https://github.com/nodejs/node/pull/51735)
* \[[`b046712e86`](https://github.com/nodejs/node/commit/b046712e86)] - **test**: skip test-http-correct-hostname on loong64 (Shi Pujin) [#51663](https://github.com/nodejs/node/pull/51663)
* \[[`83f581d4c1`](https://github.com/nodejs/node/commit/83f581d4c1)] - **test**: increase platform timeout zlib-brotli-16gb (Rafael Gonzaga) [#51792](https://github.com/nodejs/node/pull/51792)
* \[[`ea08350c83`](https://github.com/nodejs/node/commit/ea08350c83)] - **test**: remove test-cli-node-options flaky designation (Luigi Pinca) [#51716](https://github.com/nodejs/node/pull/51716)
* \[[`9d3a014f67`](https://github.com/nodejs/node/commit/9d3a014f67)] - **test**: remove test-domain-error-types flaky designation (Luigi Pinca) [#51717](https://github.com/nodejs/node/pull/51717)
* \[[`d7563a5448`](https://github.com/nodejs/node/commit/d7563a5448)] - **test**: fix `internet/test-inspector-help-page` (Richard Lau) [#51693](https://github.com/nodejs/node/pull/51693)
* \[[`e9299255ca`](https://github.com/nodejs/node/commit/e9299255ca)] - **test**: remove duplicate entry for flaky test (Luigi Pinca) [#51654](https://github.com/nodejs/node/pull/51654)
* \[[`a8ac337250`](https://github.com/nodejs/node/commit/a8ac337250)] - **test**: remove test-crypto-keygen flaky designation (Luigi Pinca) [#51567](https://github.com/nodejs/node/pull/51567)
* \[[`c820166e4b`](https://github.com/nodejs/node/commit/c820166e4b)] - **test**: remove test-fs-rmdir-recursive flaky designation (Luigi Pinca) [#51566](https://github.com/nodejs/node/pull/51566)
* \[[`db88bf185f`](https://github.com/nodejs/node/commit/db88bf185f)] - **test**: remove common.expectsError calls for asserts (Paulo Chaves) [#51504](https://github.com/nodejs/node/pull/51504)
* \[[`fc0c1309b2`](https://github.com/nodejs/node/commit/fc0c1309b2)] - **test**: mark test-http2-large-file as flaky (Michaël Zasso) [#51549](https://github.com/nodejs/node/pull/51549)
* \[[`c88f0b6db9`](https://github.com/nodejs/node/commit/c88f0b6db9)] - **test**: use checkIfCollectableByCounting in SourceTextModule leak test (Joyee Cheung) [#51512](https://github.com/nodejs/node/pull/51512)
* \[[`d4d07f4a44`](https://github.com/nodejs/node/commit/d4d07f4a44)] - **test**: remove test-file-write-stream4 flaky designation (Luigi Pinca) [#51472](https://github.com/nodejs/node/pull/51472)
* \[[`7420a7d2f8`](https://github.com/nodejs/node/commit/7420a7d2f8)] - **test**: add URL tests to fs-write (Rafael Gonzaga) [#51352](https://github.com/nodejs/node/pull/51352)
* \[[`28c2bf3e42`](https://github.com/nodejs/node/commit/28c2bf3e42)] - **test**: remove unneeded common.expectsError for asserts (Andrés Morelos) [#51353](https://github.com/nodejs/node/pull/51353)
* \[[`9dfb36fbe5`](https://github.com/nodejs/node/commit/9dfb36fbe5)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#51533](https://github.com/nodejs/node/pull/51533)
* \[[`e4d4bc6f9a`](https://github.com/nodejs/node/commit/e4d4bc6f9a)] - **test\_runner**: serialize 'expected' and 'actual' in isolation (Malthe Borch) [#51851](https://github.com/nodejs/node/pull/51851)
* \[[`5f9491237c`](https://github.com/nodejs/node/commit/5f9491237c)] - **test\_runner**: add ref methods to mocked timers (Marco Ippolito) [#51809](https://github.com/nodejs/node/pull/51809)
* \[[`af5875c6e8`](https://github.com/nodejs/node/commit/af5875c6e8)] - **test\_runner**: check if timeout was cleared by own callback (Ben Richeson) [#51673](https://github.com/nodejs/node/pull/51673)
* \[[`e0789fbc8a`](https://github.com/nodejs/node/commit/e0789fbc8a)] - **test\_runner**: do not invoke after hook when test is empty (Marco Ippolito) [#51389](https://github.com/nodejs/node/pull/51389)
* \[[`27f8549903`](https://github.com/nodejs/node/commit/27f8549903)] - **tools**: fix installing node with shared mode (Cheng Zhao) [#51910](https://github.com/nodejs/node/pull/51910)
* \[[`71a809bd43`](https://github.com/nodejs/node/commit/71a809bd43)] - **tools**: update eslint to 8.57.0 (Node.js GitHub Bot) [#51867](https://github.com/nodejs/node/pull/51867)
* \[[`20effe638a`](https://github.com/nodejs/node/commit/20effe638a)] - **tools**: update lint-md-dependencies to rollup\@4.12.0 (Node.js GitHub Bot) [#51795](https://github.com/nodejs/node/pull/51795)
* \[[`6c0a3b9c0d`](https://github.com/nodejs/node/commit/6c0a3b9c0d)] - **tools**: update lint-md-dependencies to rollup\@4.12.0 (Node.js GitHub Bot) [#51795](https://github.com/nodejs/node/pull/51795)
* \[[`03f926ddca`](https://github.com/nodejs/node/commit/03f926ddca)] - **tools**: fix missing \[\[fallthrough]] in js2c (Cheng Zhao) [#51845](https://github.com/nodejs/node/pull/51845)
* \[[`b502be1d09`](https://github.com/nodejs/node/commit/b502be1d09)] - **tools**: disable automated libuv updates (Rafael Gonzaga) [#51775](https://github.com/nodejs/node/pull/51775)
* \[[`787fb32557`](https://github.com/nodejs/node/commit/787fb32557)] - **tools**: fix update-icu.sh (Michaël Zasso) [#51723](https://github.com/nodejs/node/pull/51723)
* \[[`ba22c614c1`](https://github.com/nodejs/node/commit/ba22c614c1)] - **tools**: update lint-md-dependencies to rollup\@4.10.0 (Node.js GitHub Bot) [#51720](https://github.com/nodejs/node/pull/51720)
* \[[`751821fa21`](https://github.com/nodejs/node/commit/751821fa21)] - **tools**: update github\_reporter to 1.6.0 (Node.js GitHub Bot) [#51658](https://github.com/nodejs/node/pull/51658)
* \[[`5fe493d0e4`](https://github.com/nodejs/node/commit/5fe493d0e4)] - **tools**: run `build-windows` workflow only on source changes (Antoine du Hamel) [#51596](https://github.com/nodejs/node/pull/51596)
* \[[`e1b9655bdc`](https://github.com/nodejs/node/commit/e1b9655bdc)] - **tools**: update lint-md-dependencies to rollup\@4.9.6 (Node.js GitHub Bot) [#51583](https://github.com/nodejs/node/pull/51583)
* \[[`d8e1058f18`](https://github.com/nodejs/node/commit/d8e1058f18)] - **tools**: fix loong64 build (Shi Pujin) [#51401](https://github.com/nodejs/node/pull/51401)
* \[[`e0eeebc960`](https://github.com/nodejs/node/commit/e0eeebc960)] - **tools**: set normalizeTD text default to empty string (Marco Ippolito) [#51543](https://github.com/nodejs/node/pull/51543)
* \[[`81fd7d1ca9`](https://github.com/nodejs/node/commit/81fd7d1ca9)] - **tools**: limit parallelism with ninja in V8 builds (Richard Lau) [#51473](https://github.com/nodejs/node/pull/51473)
* \[[`e88a301e98`](https://github.com/nodejs/node/commit/e88a301e98)] - **tools**: do not pass invalid flag to C compiler (Michaël Zasso) [#51409](https://github.com/nodejs/node/pull/51409)
* \[[`129d3b3293`](https://github.com/nodejs/node/commit/129d3b3293)] - **tools**: update lint-md-dependencies to rollup\@4.9.5 (Node.js GitHub Bot) [#51460](https://github.com/nodejs/node/pull/51460)
* \[[`f3845de204`](https://github.com/nodejs/node/commit/f3845de204)] - **tools**: update inspector\_protocol to 83b1154 (Kohei Ueno) [#51309](https://github.com/nodejs/node/pull/51309)
* \[[`58901d08fd`](https://github.com/nodejs/node/commit/58901d08fd)] - **tools**: update github\_reporter to 1.5.4 (Node.js GitHub Bot) [#51395](https://github.com/nodejs/node/pull/51395)
* \[[`29492e2c88`](https://github.com/nodejs/node/commit/29492e2c88)] - **tools**: fix version parsing in brotli update script (Richard Lau) [#51373](https://github.com/nodejs/node/pull/51373)
* \[[`17593d95ba`](https://github.com/nodejs/node/commit/17593d95ba)] - **tools**: update lint-md-dependencies to rollup\@4.9.4 (Node.js GitHub Bot) [#51396](https://github.com/nodejs/node/pull/51396)
* \[[`35f33d3a31`](https://github.com/nodejs/node/commit/35f33d3a31)] - **tools**: remove openssl v1 update script (Marco Ippolito) [#51378](https://github.com/nodejs/node/pull/51378)
* \[[`83b3aa838b`](https://github.com/nodejs/node/commit/83b3aa838b)] - **tools**: remove deprecated python api (Alex Yang) [#49731](https://github.com/nodejs/node/pull/49731)
* \[[`adb2c36f0f`](https://github.com/nodejs/node/commit/adb2c36f0f)] - **typings**: lib/internal/vm.js (Geoffrey Booth) [#50112](https://github.com/nodejs/node/pull/50112)
* \[[`407341e25c`](https://github.com/nodejs/node/commit/407341e25c)] - **url**: don't update URL immediately on update to URLSearchParams (Matt Cowley) [#51520](https://github.com/nodejs/node/pull/51520)
* \[[`88e08bbe80`](https://github.com/nodejs/node/commit/88e08bbe80)] - **(SEMVER-MINOR)** **util**: add styleText API to text formatting (Rafael Gonzaga) [#51850](https://github.com/nodejs/node/pull/51850)
* \[[`ba444a949d`](https://github.com/nodejs/node/commit/ba444a949d)] - **vm**: implement isContext() directly in JS land with private symbol (Joyee Cheung) [#51685](https://github.com/nodejs/node/pull/51685)
* \[[`4c508269cd`](https://github.com/nodejs/node/commit/4c508269cd)] - **(SEMVER-MINOR)** **vm**: support using the default loader to handle dynamic import() (Joyee Cheung) [#51244](https://github.com/nodejs/node/pull/51244)

<a id="21.6.2"></a>

## 2024-02-14, Version 21.6.2 (Current), @RafaelGSS

### Notable changes

This is a security release.

### Notable changes

* CVE-2024-21892 - Code injection and privilege escalation through Linux capabilities- (High)
* CVE-2024-22019 - http: Reading unprocessed HTTP request with unbounded chunk extension allows DoS attacks- (High)
* CVE-2024-21896 - Path traversal by monkey-patching Buffer internals- (High)
* CVE-2024-22017 - setuid() does not drop all privileges due to io\_uring - (High)
* CVE-2023-46809 - Node.js is vulnerable to the Marvin Attack (timing variant of the Bleichenbacher attack against PKCS#1 v1.5 padding) - (Medium)
* CVE-2024-21891 - Multiple permission model bypasses due to improper path traversal sequence sanitization - (Medium)
* CVE-2024-21890 - Improper handling of wildcards in --allow-fs-read and --allow-fs-write (Medium)
* CVE-2024-22025 - Denial of Service by resource exhaustion in fetch() brotli decoding - (Medium)
* undici version 5.28.3
* libuv version 1.48.0
* OpenSSL version 3.0.13+quic1

### Commits

* \[[`8344719369`](https://github.com/nodejs/node/commit/8344719369)] - **crypto**: disable PKCS#1 padding for privateDecrypt (Michael Dawson) [nodejs-private/node-private#525](https://github.com/nodejs-private/node-private/pull/525)
* \[[`d093600ac4`](https://github.com/nodejs/node/commit/d093600ac4)] - **deps**: update archs files for openssl-3.0.13+quic1 (Node.js GitHub Bot) [#51614](https://github.com/nodejs/node/pull/51614)
* \[[`6cd930e5e8`](https://github.com/nodejs/node/commit/6cd930e5e8)] - **deps**: upgrade openssl sources to quictls/openssl-3.0.13+quic1 (Node.js GitHub Bot) [#51614](https://github.com/nodejs/node/pull/51614)
* \[[`9590c15d3d`](https://github.com/nodejs/node/commit/9590c15d3d)] - **deps**: upgrade libuv to 1.48.0 (Santiago Gimeno) [#51698](https://github.com/nodejs/node/pull/51698)
* \[[`666096298c`](https://github.com/nodejs/node/commit/666096298c)] - **deps**: disable io\_uring support in libuv by default (Tobias Nießen) [nodejs-private/node-private#528](https://github.com/nodejs-private/node-private/pull/528)
* \[[`a4edd22e30`](https://github.com/nodejs/node/commit/a4edd22e30)] - **fs**: protect against modified Buffer internals in possiblyTransformPath (Tobias Nießen) [nodejs-private/node-private#497](https://github.com/nodejs-private/node-private/pull/497)
* \[[`6155a1ffaf`](https://github.com/nodejs/node/commit/6155a1ffaf)] - **http**: add maximum chunk extension size (Paolo Insogna) [nodejs-private/node-private#518](https://github.com/nodejs-private/node-private/pull/518)
* \[[`777509495e`](https://github.com/nodejs/node/commit/777509495e)] - **lib**: use cache fs internals against path traversal (RafaelGSS) [nodejs-private/node-private#516](https://github.com/nodejs-private/node-private/pull/516)
* \[[`9d2ac2b3fc`](https://github.com/nodejs/node/commit/9d2ac2b3fc)] - **lib**: update undici to v5.28.3 (Matteo Collina) [nodejs-private/node-private#538](https://github.com/nodejs-private/node-private/pull/538)
* \[[`208b3940c7`](https://github.com/nodejs/node/commit/208b3940c7)] - **src**: fix HasOnly(capability) in node::credentials (Tobias Nießen) [nodejs-private/node-private#505](https://github.com/nodejs-private/node-private/pull/505)
* \[[`fc2454f29c`](https://github.com/nodejs/node/commit/fc2454f29c)] - **src,deps**: disable setuid() etc if io\_uring enabled (Tobias Nießen) [nodejs-private/node-private#528](https://github.com/nodejs-private/node-private/pull/528)
* \[[`ef3eea20be`](https://github.com/nodejs/node/commit/ef3eea20be)] - **test,doc**: clarify wildcard usage (RafaelGSS) [nodejs-private/node-private#517](https://github.com/nodejs-private/node-private/pull/517)
* \[[`8547196964`](https://github.com/nodejs/node/commit/8547196964)] - **zlib**: pause stream if outgoing buffer is full (Matteo Collina) [nodejs-private/node-private#540](https://github.com/nodejs-private/node-private/pull/540)

<a id="21.6.1"></a>

## 2024-01-22, Version 21.6.1 (Current), @RafaelGSS

### Notable Changes

This release fixes a bug in `undici` using WebStreams

### Commits

* \[[`662ac95729`](https://github.com/nodejs/node/commit/662ac95729)] - _**Revert**_ "**stream**: fix cloned webstreams not being unref'd" (Matteo Collina) [#51491](https://github.com/nodejs/node/pull/51491)
* \[[`1b8bba8aee`](https://github.com/nodejs/node/commit/1b8bba8aee)] - **test**: add regression test for 51586 (Matteo Collina) [#51491](https://github.com/nodejs/node/pull/51491)

<a id="21.6.0"></a>

## 2024-01-15, Version 21.6.0 (Current), @RafaelGSS

### New connection attempt events

Three new events were added in the `net.createConnection` flow:

* `connectionAttempt`: Emitted when a new connection attempt is established. In case of Happy Eyeballs, this might emitted multiple times.
* `connectionAttemptFailed`: Emitted when a connection attempt failed. In case of Happy Eyeballs, this might emitted multiple times.
* `connectionAttemptTimeout`: Emitted when a connection attempt timed out. In case of Happy Eyeballs, this will not be emitted for the last attempt. This is not emitted at all if Happy Eyeballs is not used.

Additionally, a previous bug has been fixed where a new connection attempt could have been started after a previous one failed and after the connection was destroyed by the user.
This led to a failed assertion.

Contributed by Paolo Insogna in [#51045](https://github.com/nodejs/node/pull/51045).

### Changes to the Permission Model

Node.js 21.6.0 comes with several fixes for the experimental permission model and two new semver-minor commits.
We're adding a new flag `--allow-addons` to enable addon usage when using the Permission Model.

```console
node --experimental-permission --allow-addons
```

Contributed by Rafael Gonzaga in [#51183](https://github.com/nodejs/node/pull/51183)

And relative paths are now supported through the `--allow-fs-*` flags.
Therefore, with this release one can use:

```console
node --experimental-permission --allow-fs-read=./index.js
```

To give only read access to the entrypoint of the application.

Contributed by Rafael Gonzaga and Carlos Espa in [#50758](https://github.com/nodejs/node/pull/50758)

### Support configurable snapshot through `--build-snapshot-config` flag

We are adding a new flag `--build-snapshot-config` to configure snapshots through a custom JSON configuration file.

```console
node --build-snapshot-config=/path/to/myconfig.json
```

When using this flag, additional script files provided on the command line will
not be executed and instead be interpreted as regular command line arguments.

These changes were contributed by Joyee Cheung and Anna Henningsen in [#50453](https://github.com/nodejs/node/pull/50453)

### Other Notable Changes

* \[[`c31ed51373`](https://github.com/nodejs/node/commit/c31ed51373)] - **(SEMVER-MINOR)** **timers**: export timers.promises (Marco Ippolito) [#51246](https://github.com/nodejs/node/pull/51246)

### Commits

* \[[`13a1241b83`](https://github.com/nodejs/node/commit/13a1241b83)] - **assert,crypto**: make KeyObject and CryptoKey testable for equality (Filip Skokan) [#50897](https://github.com/nodejs/node/pull/50897)
* \[[`4dcc5114aa`](https://github.com/nodejs/node/commit/4dcc5114aa)] - **benchmark**: remove dependency on unshipped tools (Adam Majer) [#51146](https://github.com/nodejs/node/pull/51146)
* \[[`2eb41f86b3`](https://github.com/nodejs/node/commit/2eb41f86b3)] - **build**: fix for VScode "Reopen in Container" (Serg Kryvonos) [#51271](https://github.com/nodejs/node/pull/51271)
* \[[`e03ac83c19`](https://github.com/nodejs/node/commit/e03ac83c19)] - **build**: fix arm64 cross-compilation (Michaël Zasso) [#51256](https://github.com/nodejs/node/pull/51256)
* \[[`cd61fce34e`](https://github.com/nodejs/node/commit/cd61fce34e)] - **build**: add `-flax-vector-conversions` to V8 build (Michaël Zasso) [#51257](https://github.com/nodejs/node/pull/51257)
* \[[`e5017a522e`](https://github.com/nodejs/node/commit/e5017a522e)] - **crypto**: update CryptoKey symbol properties (Filip Skokan) [#50897](https://github.com/nodejs/node/pull/50897)
* \[[`c0d2e8be11`](https://github.com/nodejs/node/commit/c0d2e8be11)] - **deps**: update corepack to 0.24.0 (Node.js GitHub Bot) [#51318](https://github.com/nodejs/node/pull/51318)
* \[[`24a9a72492`](https://github.com/nodejs/node/commit/24a9a72492)] - **deps**: update acorn to 8.11.3 (Node.js GitHub Bot) [#51317](https://github.com/nodejs/node/pull/51317)
* \[[`e53cbb22c2`](https://github.com/nodejs/node/commit/e53cbb22c2)] - **deps**: update ngtcp2 and nghttp3 (James M Snell) [#51291](https://github.com/nodejs/node/pull/51291)
* \[[`f00f1204f1`](https://github.com/nodejs/node/commit/f00f1204f1)] - **deps**: update brotli to 1.1.0 (Node.js GitHub Bot) [#50804](https://github.com/nodejs/node/pull/50804)
* \[[`a41dca0c51`](https://github.com/nodejs/node/commit/a41dca0c51)] - **deps**: update zlib to 1.3.0.1-motley-40e35a7 (Node.js GitHub Bot) [#51274](https://github.com/nodejs/node/pull/51274)
* \[[`efa12a89c6`](https://github.com/nodejs/node/commit/efa12a89c6)] - **deps**: update simdutf to 4.0.8 (Node.js GitHub Bot) [#51000](https://github.com/nodejs/node/pull/51000)
* \[[`25eba3d20b`](https://github.com/nodejs/node/commit/25eba3d20b)] - **deps**: V8: cherry-pick de611e69ad51 (Keyhan Vakil) [#51200](https://github.com/nodejs/node/pull/51200)
* \[[`a07d6e23e4`](https://github.com/nodejs/node/commit/a07d6e23e4)] - **deps**: update simdjson to 3.6.3 (Node.js GitHub Bot) [#51104](https://github.com/nodejs/node/pull/51104)
* \[[`6d1bfcb2dd`](https://github.com/nodejs/node/commit/6d1bfcb2dd)] - **deps**: update googletest to 530d5c8 (Node.js GitHub Bot) [#51191](https://github.com/nodejs/node/pull/51191)
* \[[`75e5615c43`](https://github.com/nodejs/node/commit/75e5615c43)] - **deps**: update acorn-walk to 8.3.1 (Node.js GitHub Bot) [#50457](https://github.com/nodejs/node/pull/50457)
* \[[`3ecc7dcc00`](https://github.com/nodejs/node/commit/3ecc7dcc00)] - **deps**: update acorn-walk to 8.3.0 (Node.js GitHub Bot) [#50457](https://github.com/nodejs/node/pull/50457)
* \[[`e2f8d741c8`](https://github.com/nodejs/node/commit/e2f8d741c8)] - **deps**: update zlib to 1.3.0.1-motley-dd5fc13 (Node.js GitHub Bot) [#51105](https://github.com/nodejs/node/pull/51105)
* \[[`4a5d3bda72`](https://github.com/nodejs/node/commit/4a5d3bda72)] - **doc**: the GN files should use Node's license (Cheng Zhao) [#50694](https://github.com/nodejs/node/pull/50694)
* \[[`84127514ba`](https://github.com/nodejs/node/commit/84127514ba)] - **doc**: improve localWindowSize event descriptions (Davy Landman) [#51071](https://github.com/nodejs/node/pull/51071)
* \[[`8ee882a49c`](https://github.com/nodejs/node/commit/8ee882a49c)] - **doc**: mark `--jitless` as experimental (Antoine du Hamel) [#51247](https://github.com/nodejs/node/pull/51247)
* \[[`876743ece1`](https://github.com/nodejs/node/commit/876743ece1)] - **doc**: run license-builder (github-actions\[bot]) [#51199](https://github.com/nodejs/node/pull/51199)
* \[[`ec6fcff009`](https://github.com/nodejs/node/commit/ec6fcff009)] - **doc**: fix limitations and known issues in pm (Rafael Gonzaga) [#51184](https://github.com/nodejs/node/pull/51184)
* \[[`c13a5c0373`](https://github.com/nodejs/node/commit/c13a5c0373)] - **doc**: mention node:wasi in the Threat Model (Rafael Gonzaga) [#51211](https://github.com/nodejs/node/pull/51211)
* \[[`4b19e62444`](https://github.com/nodejs/node/commit/4b19e62444)] - **doc**: remove ambiguous 'considered' (Rich Trott) [#51207](https://github.com/nodejs/node/pull/51207)
* \[[`5453abd6ad`](https://github.com/nodejs/node/commit/5453abd6ad)] - **doc**: set exit code in custom test runner example (Matteo Collina) [#51056](https://github.com/nodejs/node/pull/51056)
* \[[`f9d4e07faf`](https://github.com/nodejs/node/commit/f9d4e07faf)] - **doc**: remove version from `maintaining-dependencies.md` (Antoine du Hamel) [#51195](https://github.com/nodejs/node/pull/51195)
* \[[`df8927a073`](https://github.com/nodejs/node/commit/df8927a073)] - **doc**: mention native addons are restricted in pm (Rafael Gonzaga) [#51185](https://github.com/nodejs/node/pull/51185)
* \[[`e636d83914`](https://github.com/nodejs/node/commit/e636d83914)] - **doc**: correct note on behavior of stats.isDirectory (Nick Reilingh) [#50946](https://github.com/nodejs/node/pull/50946)
* \[[`1c71435c2a`](https://github.com/nodejs/node/commit/1c71435c2a)] - **doc**: fix `TestsStream` parent class (Jungku Lee) [#51181](https://github.com/nodejs/node/pull/51181)
* \[[`2c227b0d64`](https://github.com/nodejs/node/commit/2c227b0d64)] - **doc**: fix simdjson wrong link (Marco Ippolito) [#51177](https://github.com/nodejs/node/pull/51177)
* \[[`efa13e1943`](https://github.com/nodejs/node/commit/efa13e1943)] - **(SEMVER-MINOR)** **doc**: add documentation for --build-snapshot-config (Anna Henningsen) [#50453](https://github.com/nodejs/node/pull/50453)
* \[[`941aedc6fc`](https://github.com/nodejs/node/commit/941aedc6fc)] - **errors**: fix stacktrace of SystemError (uzlopak) [#49956](https://github.com/nodejs/node/pull/49956)
* \[[`47548d9e61`](https://github.com/nodejs/node/commit/47548d9e61)] - **esm**: fix hint on invalid module specifier (Antoine du Hamel) [#51223](https://github.com/nodejs/node/pull/51223)
* \[[`091098f40a`](https://github.com/nodejs/node/commit/091098f40a)] - **fs**: fix fs.promises.realpath for long paths on Windows (翠 / green) [#51032](https://github.com/nodejs/node/pull/51032)
* \[[`e5a8fa01aa`](https://github.com/nodejs/node/commit/e5a8fa01aa)] - **fs**: make offset, position & length args in fh.read() optional (Pulkit Gupta) [#51087](https://github.com/nodejs/node/pull/51087)
* \[[`c87e5d51cc`](https://github.com/nodejs/node/commit/c87e5d51cc)] - **fs**: add missing jsdoc parameters to `readSync` (Yagiz Nizipli) [#51225](https://github.com/nodejs/node/pull/51225)
* \[[`e24249cf37`](https://github.com/nodejs/node/commit/e24249cf37)] - **fs**: remove `internalModuleReadJSON` binding (Yagiz Nizipli) [#51224](https://github.com/nodejs/node/pull/51224)
* \[[`7421467812`](https://github.com/nodejs/node/commit/7421467812)] - **fs**: improve mkdtemp performance for buffer prefix (Yagiz Nizipli) [#51078](https://github.com/nodejs/node/pull/51078)
* \[[`5b229d775f`](https://github.com/nodejs/node/commit/5b229d775f)] - **fs**: validate fd synchronously on c++ (Yagiz Nizipli) [#51027](https://github.com/nodejs/node/pull/51027)
* \[[`c7a135962d`](https://github.com/nodejs/node/commit/c7a135962d)] - **http**: remove misleading warning (Luigi Pinca) [#51204](https://github.com/nodejs/node/pull/51204)
* \[[`a325746ff4`](https://github.com/nodejs/node/commit/a325746ff4)] - **http**: do not override user-provided options object (KuthorX) [#33633](https://github.com/nodejs/node/pull/33633)
* \[[`89eee7763f`](https://github.com/nodejs/node/commit/89eee7763f)] - **http2**: addtl http/2 settings (Marten Richter) [#49025](https://github.com/nodejs/node/pull/49025)
* \[[`624142947f`](https://github.com/nodejs/node/commit/624142947f)] - **lib**: fix use of `--frozen-intrinsics` with `--jitless` (Antoine du Hamel) [#51248](https://github.com/nodejs/node/pull/51248)
* \[[`8f845eb001`](https://github.com/nodejs/node/commit/8f845eb001)] - **lib**: move function declaration outside of loop (Sanjaiyan Parthipan) [#51242](https://github.com/nodejs/node/pull/51242)
* \[[`ed7305e49b`](https://github.com/nodejs/node/commit/ed7305e49b)] - **lib**: reduce overhead of `SafePromiseAllSettledReturnVoid` calls (Antoine du Hamel) [#51243](https://github.com/nodejs/node/pull/51243)
* \[[`291265ce27`](https://github.com/nodejs/node/commit/291265ce27)] - **lib**: expose default prepareStackTrace (Chengzhong Wu) [#50827](https://github.com/nodejs/node/pull/50827)
* \[[`8ff6bc45ca`](https://github.com/nodejs/node/commit/8ff6bc45ca)] - **lib,permission**: handle buffer on fs.symlink (Rafael Gonzaga) [#51212](https://github.com/nodejs/node/pull/51212)
* \[[`416b4f8063`](https://github.com/nodejs/node/commit/416b4f8063)] - **(SEMVER-MINOR)** **lib,src,permission**: port path.resolve to C++ (Rafael Gonzaga) [#50758](https://github.com/nodejs/node/pull/50758)
* \[[`6648a5c576`](https://github.com/nodejs/node/commit/6648a5c576)] - **meta**: notify tsc on changes in SECURITY.md (Rafael Gonzaga) [#51259](https://github.com/nodejs/node/pull/51259)
* \[[`83a99ccedd`](https://github.com/nodejs/node/commit/83a99ccedd)] - **meta**: update artifact actions to v4 (Michaël Zasso) [#51219](https://github.com/nodejs/node/pull/51219)
* \[[`b621ada69a`](https://github.com/nodejs/node/commit/b621ada69a)] - **module**: move the CJS exports cache to internal/modules/cjs/loader (Joyee Cheung) [#51157](https://github.com/nodejs/node/pull/51157)
* \[[`e4be5b60f0`](https://github.com/nodejs/node/commit/e4be5b60f0)] - **(SEMVER-MINOR)** **net**: add connection attempt events (Paolo Insogna) [#51045](https://github.com/nodejs/node/pull/51045)
* \[[`3a492056e2`](https://github.com/nodejs/node/commit/3a492056e2)] - **node-api**: type tag external values without v8::Private (Chengzhong Wu) [#51149](https://github.com/nodejs/node/pull/51149)
* \[[`b2135ae7dc`](https://github.com/nodejs/node/commit/b2135ae7dc)] - **node-api**: segregate nogc APIs from rest via type system (Gabriel Schulhof) [#50060](https://github.com/nodejs/node/pull/50060)
* \[[`8f4325dcd5`](https://github.com/nodejs/node/commit/8f4325dcd5)] - **permission**: fix wildcard when children > 1 (Rafael Gonzaga) [#51209](https://github.com/nodejs/node/pull/51209)
* \[[`7ecf99404e`](https://github.com/nodejs/node/commit/7ecf99404e)] - **quic**: update quic impl to use latest ngtcp2/nghttp3 (James M Snell) [#51291](https://github.com/nodejs/node/pull/51291)
* \[[`5b32e21f3b`](https://github.com/nodejs/node/commit/5b32e21f3b)] - **quic**: add quic internalBinding, refine Endpoint, add types (James M Snell) [#51112](https://github.com/nodejs/node/pull/51112)
* \[[`3310095bea`](https://github.com/nodejs/node/commit/3310095bea)] - **repl**: fix prepareStackTrace frames array order (Chengzhong Wu) [#50827](https://github.com/nodejs/node/pull/50827)
* \[[`a0ff00b526`](https://github.com/nodejs/node/commit/a0ff00b526)] - **src**: avoid draining platform tasks at FreeEnvironment (Chengzhong Wu) [#51290](https://github.com/nodejs/node/pull/51290)
* \[[`115e0585cd`](https://github.com/nodejs/node/commit/115e0585cd)] - **src**: add fast api for Histogram (James M Snell) [#51296](https://github.com/nodejs/node/pull/51296)
* \[[`29b81576c6`](https://github.com/nodejs/node/commit/29b81576c6)] - **src**: refactor `GetCreationContext` calls (Yagiz Nizipli) [#51287](https://github.com/nodejs/node/pull/51287)
* \[[`54dd978400`](https://github.com/nodejs/node/commit/54dd978400)] - **src**: enter isolate before destructing IsolateData (Ben Noordhuis) [#51138](https://github.com/nodejs/node/pull/51138)
* \[[`864ecb0dfa`](https://github.com/nodejs/node/commit/864ecb0dfa)] - **src**: do not treat all paths ending with node\_modules as such (Michaël Zasso) [#51269](https://github.com/nodejs/node/pull/51269)
* \[[`df31c8114c`](https://github.com/nodejs/node/commit/df31c8114c)] - **src**: eliminate duplicate code in histogram.cc (James M Snell) [#51263](https://github.com/nodejs/node/pull/51263)
* \[[`17c73e6d0c`](https://github.com/nodejs/node/commit/17c73e6d0c)] - **src**: fix unix abstract socket path for trace event (theanarkh) [#50858](https://github.com/nodejs/node/pull/50858)
* \[[`96d64edc94`](https://github.com/nodejs/node/commit/96d64edc94)] - **src**: use BignumPointer and use BN\_clear\_free (James M Snell) [#50454](https://github.com/nodejs/node/pull/50454)
* \[[`8a2dd93a14`](https://github.com/nodejs/node/commit/8a2dd93a14)] - **src**: implement FastByteLengthUtf8 with simdutf::utf8\_length\_from\_latin1 (Daniel Lemire) [#50840](https://github.com/nodejs/node/pull/50840)
* \[[`e54ddf898f`](https://github.com/nodejs/node/commit/e54ddf898f)] - **(SEMVER-MINOR)** **src**: support configurable snapshot (Joyee Cheung) [#50453](https://github.com/nodejs/node/pull/50453)
* \[[`a69c7d7bc3`](https://github.com/nodejs/node/commit/a69c7d7bc3)] - **(SEMVER-MINOR)** **src,permission**: add --allow-addon flag (Rafael Gonzaga) [#51183](https://github.com/nodejs/node/pull/51183)
* \[[`e7925e66fc`](https://github.com/nodejs/node/commit/e7925e66fc)] - **src,stream**: improve WriteString (ywave620) [#51155](https://github.com/nodejs/node/pull/51155)
* \[[`82de6603af`](https://github.com/nodejs/node/commit/82de6603af)] - **stream**: fix code style (Mattias Buelens) [#51168](https://github.com/nodejs/node/pull/51168)
* \[[`e443953656`](https://github.com/nodejs/node/commit/e443953656)] - **stream**: fix cloned webstreams not being unref'd (James M Snell) [#51255](https://github.com/nodejs/node/pull/51255)
* \[[`757a84c9ea`](https://github.com/nodejs/node/commit/757a84c9ea)] - **test**: fix flaky conditions for ppc64 SEA tests (Richard Lau) [#51422](https://github.com/nodejs/node/pull/51422)
* \[[`85ee2f7255`](https://github.com/nodejs/node/commit/85ee2f7255)] - **test**: replace forEach() with for...of (Alexander Jones) [#50608](https://github.com/nodejs/node/pull/50608)
* \[[`549e4b4142`](https://github.com/nodejs/node/commit/549e4b4142)] - **test**: replace forEach with for...of (Ospite Privilegiato) [#50787](https://github.com/nodejs/node/pull/50787)
* \[[`ef44f9bef2`](https://github.com/nodejs/node/commit/ef44f9bef2)] - **test**: replace foreach with for of (lucacapocci94-dev) [#50790](https://github.com/nodejs/node/pull/50790)
* \[[`652af45485`](https://github.com/nodejs/node/commit/652af45485)] - **test**: replace forEach() with for...of (Jia) [#50610](https://github.com/nodejs/node/pull/50610)
* \[[`684dd9db2f`](https://github.com/nodejs/node/commit/684dd9db2f)] - **test**: fix inconsistency write size in `test-fs-readfile-tostring-fail` (Jungku Lee) [#51141](https://github.com/nodejs/node/pull/51141)
* \[[`aaf710f535`](https://github.com/nodejs/node/commit/aaf710f535)] - **test**: replace forEach test-http-server-multiheaders2 (Marco Mac) [#50794](https://github.com/nodejs/node/pull/50794)
* \[[`57c64550cc`](https://github.com/nodejs/node/commit/57c64550cc)] - **test**: replace forEach with for-of in test-webcrypto-export-import-ec (Chiara Ricciardi) [#51249](https://github.com/nodejs/node/pull/51249)
* \[[`88e865181b`](https://github.com/nodejs/node/commit/88e865181b)] - **test**: move to for of loop in test-http-hostname-typechecking.js (Luca Del Puppo) [#50782](https://github.com/nodejs/node/pull/50782)
* \[[`3db376f67a`](https://github.com/nodejs/node/commit/3db376f67a)] - **test**: skip test-watch-mode-inspect on arm (Michael Dawson) [#51210](https://github.com/nodejs/node/pull/51210)
* \[[`38232d1c52`](https://github.com/nodejs/node/commit/38232d1c52)] - **test**: replace forEach with for of in file test-trace-events-net.js (Ianna83) [#50789](https://github.com/nodejs/node/pull/50789)
* \[[`f1cb58355a`](https://github.com/nodejs/node/commit/f1cb58355a)] - **test**: replace forEach() with for...of in test/parallel/test-util-log.js (Edoardo Dusi) [#50783](https://github.com/nodejs/node/pull/50783)
* \[[`9bfd84c117`](https://github.com/nodejs/node/commit/9bfd84c117)] - **test**: replace forEach with for of in test-trace-events-api.js (Andrea Pavone) [#50784](https://github.com/nodejs/node/pull/50784)
* \[[`7e9834915a`](https://github.com/nodejs/node/commit/7e9834915a)] - **test**: replace forEach with for-of in test-v8-serders.js (Mattia Iannone) [#50791](https://github.com/nodejs/node/pull/50791)
* \[[`b6f232e841`](https://github.com/nodejs/node/commit/b6f232e841)] - **test**: add URL tests to fs-read in pm (Rafael Gonzaga) [#51213](https://github.com/nodejs/node/pull/51213)
* \[[`8a2178c5f5`](https://github.com/nodejs/node/commit/8a2178c5f5)] - **test**: use tmpdir.refresh() in test-esm-loader-resolve-type.mjs (Luigi Pinca) [#51206](https://github.com/nodejs/node/pull/51206)
* \[[`7e9a0b192a`](https://github.com/nodejs/node/commit/7e9a0b192a)] - **test**: use tmpdir.refresh() in test-esm-json.mjs (Luigi Pinca) [#51205](https://github.com/nodejs/node/pull/51205)
* \[[`d7c2572fe0`](https://github.com/nodejs/node/commit/d7c2572fe0)] - **test**: fix flakiness in worker\*.test-free-called (Jithil P Ponnan) [#51013](https://github.com/nodejs/node/pull/51013)
* \[[`979cebc955`](https://github.com/nodejs/node/commit/979cebc955)] - **test\_runner**: fixed test object is incorrectly passed to setup() (Pulkit Gupta) [#50982](https://github.com/nodejs/node/pull/50982)
* \[[`63db82abe6`](https://github.com/nodejs/node/commit/63db82abe6)] - **test\_runner**: fixed to run after hook if before throws an error (Pulkit Gupta) [#51062](https://github.com/nodejs/node/pull/51062)
* \[[`c31ed51373`](https://github.com/nodejs/node/commit/c31ed51373)] - **(SEMVER-MINOR)** **timers**: export timers.promises (Marco Ippolito) [#51246](https://github.com/nodejs/node/pull/51246)
* \[[`fc10f889eb`](https://github.com/nodejs/node/commit/fc10f889eb)] - **tools**: update lint-md-dependencies to rollup\@4.9.2 (Node.js GitHub Bot) [#51320](https://github.com/nodejs/node/pull/51320)
* \[[`d5a5f12d15`](https://github.com/nodejs/node/commit/d5a5f12d15)] - **tools**: fix dep\_updaters dir updates (Michaël Zasso) [#51294](https://github.com/nodejs/node/pull/51294)
* \[[`bdcb5ed510`](https://github.com/nodejs/node/commit/bdcb5ed510)] - **tools**: update inspector\_protocol to c488ba2 (cola119) [#51293](https://github.com/nodejs/node/pull/51293)
* \[[`69a46add77`](https://github.com/nodejs/node/commit/69a46add77)] - **tools**: update inspector\_protocol to 9b4a4aa (cola119) [#51293](https://github.com/nodejs/node/pull/51293)
* \[[`e325f49d19`](https://github.com/nodejs/node/commit/e325f49d19)] - **tools**: update inspector\_protocol to 2f51e05 (cola119) [#51293](https://github.com/nodejs/node/pull/51293)
* \[[`60d804851b`](https://github.com/nodejs/node/commit/60d804851b)] - **tools**: update inspector\_protocol to d7b099b (cola119) [#51293](https://github.com/nodejs/node/pull/51293)
* \[[`d18168489f`](https://github.com/nodejs/node/commit/d18168489f)] - **tools**: update inspector\_protocol to 912eb68 (cola119) [#51293](https://github.com/nodejs/node/pull/51293)
* \[[`ef4f46fc39`](https://github.com/nodejs/node/commit/ef4f46fc39)] - **tools**: update inspector\_protocol to 547c5b8 (cola119) [#51293](https://github.com/nodejs/node/pull/51293)
* \[[`c3126fc016`](https://github.com/nodejs/node/commit/c3126fc016)] - **tools**: update inspector\_protocol to ca525fc (cola119) [#51293](https://github.com/nodejs/node/pull/51293)
* \[[`917d887dde`](https://github.com/nodejs/node/commit/917d887dde)] - **tools**: update lint-md-dependencies to rollup\@4.9.1 (Node.js GitHub Bot) [#51276](https://github.com/nodejs/node/pull/51276)
* \[[`37594918e0`](https://github.com/nodejs/node/commit/37594918e0)] - **tools**: check timezone current version (Marco Ippolito) [#51178](https://github.com/nodejs/node/pull/51178)
* \[[`d0d2faf899`](https://github.com/nodejs/node/commit/d0d2faf899)] - **tools**: update lint-md-dependencies to rollup\@4.9.0 (Node.js GitHub Bot) [#51193](https://github.com/nodejs/node/pull/51193)
* \[[`c96ef6533c`](https://github.com/nodejs/node/commit/c96ef6533c)] - **tools**: update eslint to 8.56.0 (Node.js GitHub Bot) [#51194](https://github.com/nodejs/node/pull/51194)
* \[[`f4f781d493`](https://github.com/nodejs/node/commit/f4f781d493)] - **util**: pass invalidSubtypeIndex instead of trimmedSubtype to error (Gaurish Sethia) [#51264](https://github.com/nodejs/node/pull/51264)
* \[[`867b484429`](https://github.com/nodejs/node/commit/867b484429)] - **watch**: clarify that the fileName parameter can be null (Luigi Pinca) [#51305](https://github.com/nodejs/node/pull/51305)
* \[[`56e8969b65`](https://github.com/nodejs/node/commit/56e8969b65)] - **watch**: fix null `fileName` on windows systems (vnc5) [#49891](https://github.com/nodejs/node/pull/49891)
* \[[`3f4fd6efbb`](https://github.com/nodejs/node/commit/3f4fd6efbb)] - **watch**: fix infinite loop when passing --watch=true flag (Pulkit Gupta) [#51160](https://github.com/nodejs/node/pull/51160)

<a id="21.5.0"></a>

## 2023-12-19, Version 21.5.0 (Current), @RafaelGSS

### Notable Changes

* \[[`0dd53da722`](https://github.com/nodejs/node/commit/0dd53da722)] - **(SEMVER-MINOR)** **deps**: add simdjson (Yagiz Nizipli) [#50322](https://github.com/nodejs/node/pull/50322)
* \[[`9f54987fbc`](https://github.com/nodejs/node/commit/9f54987fbc)] -  **module**: merge config with `package_json_reader` (Yagiz Nizipli) [#50322](https://github.com/nodejs/node/pull/50322)
* \[[`45e4f82912`](https://github.com/nodejs/node/commit/45e4f82912)] -  **src**: move package resolver to c++ (Yagiz Nizipli) [#50322](https://github.com/nodejs/node/pull/50322)

#### Deprecations

* \[[`26ed4ad01f`](https://github.com/nodejs/node/commit/26ed4ad01f)] - **doc**: deprecate hash constructor (Marco Ippolito) [#51077](https://github.com/nodejs/node/pull/51077)
* \[[`58ca66a1a7`](https://github.com/nodejs/node/commit/58ca66a1a7)] - **doc**: deprecate `dirent.path` (Antoine du Hamel) [#51020](https://github.com/nodejs/node/pull/51020)

### Commits

* \[[`1bbdbdfbeb`](https://github.com/nodejs/node/commit/1bbdbdfbeb)] - **benchmark**: update iterations in benchmark/perf\_hooks (Lei Shi) [#50869](https://github.com/nodejs/node/pull/50869)
* \[[`087fb0908e`](https://github.com/nodejs/node/commit/087fb0908e)] - **benchmark**: update iterations in benchmark/crypto/aes-gcm-throughput.js (Lei Shi) [#50929](https://github.com/nodejs/node/pull/50929)
* \[[`53b16c71fb`](https://github.com/nodejs/node/commit/53b16c71fb)] - **benchmark**: update iteration and size in benchmark/crypto/randomBytes.js (Lei Shi) [#50868](https://github.com/nodejs/node/pull/50868)
* \[[`38fd0ca753`](https://github.com/nodejs/node/commit/38fd0ca753)] - **benchmark**: add undici websocket benchmark (Chenyu Yang) [#50586](https://github.com/nodejs/node/pull/50586)
* \[[`b148c43244`](https://github.com/nodejs/node/commit/b148c43244)] - **benchmark**: add create-hash benchmark (Joyee Cheung) [#51026](https://github.com/nodejs/node/pull/51026)
* \[[`fdd8c18f96`](https://github.com/nodejs/node/commit/fdd8c18f96)] - **benchmark**: update interations and len in benchmark/util/text-decoder.js (Lei Shi) [#50938](https://github.com/nodejs/node/pull/50938)
* \[[`a9972057ac`](https://github.com/nodejs/node/commit/a9972057ac)] - **benchmark**: update iterations of benchmark/util/type-check.js (Lei Shi) [#50937](https://github.com/nodejs/node/pull/50937)
* \[[`b80bb1329b`](https://github.com/nodejs/node/commit/b80bb1329b)] - **benchmark**: update iterations in benchmark/util/normalize-encoding.js (Lei Shi) [#50934](https://github.com/nodejs/node/pull/50934)
* \[[`dbee03d646`](https://github.com/nodejs/node/commit/dbee03d646)] - **benchmark**: update iterations in benchmark/util/inspect-array.js (Lei Shi) [#50933](https://github.com/nodejs/node/pull/50933)
* \[[`f2d83a3a84`](https://github.com/nodejs/node/commit/f2d83a3a84)] - **benchmark**: update iterations in benchmark/util/format.js (Lei Shi) [#50932](https://github.com/nodejs/node/pull/50932)
* \[[`2581fce553`](https://github.com/nodejs/node/commit/2581fce553)] - **bootstrap**: improve snapshot unsupported builtin warnings (Joyee Cheung) [#50944](https://github.com/nodejs/node/pull/50944)
* \[[`735bad3694`](https://github.com/nodejs/node/commit/735bad3694)] - **build**: fix warnings from uv for gn build (Cheng Zhao) [#51069](https://github.com/nodejs/node/pull/51069)
* \[[`8da9d969f9`](https://github.com/nodejs/node/commit/8da9d969f9)] - **deps**: V8: cherry-pick 0fd478bcdabd (Joyee Cheung) [#50572](https://github.com/nodejs/node/pull/50572)
* \[[`429fbb37c1`](https://github.com/nodejs/node/commit/429fbb37c1)] - **deps**: update simdjson to v3.6.2 (Yagiz Nizipli) [#50986](https://github.com/nodejs/node/pull/50986)
* \[[`9950103253`](https://github.com/nodejs/node/commit/9950103253)] - **deps**: update zlib to 1.3-22124f5 (Node.js GitHub Bot) [#50910](https://github.com/nodejs/node/pull/50910)
* \[[`0b61823e8b`](https://github.com/nodejs/node/commit/0b61823e8b)] - **deps**: update undici to 5.28.2 (Node.js GitHub Bot) [#51024](https://github.com/nodejs/node/pull/51024)
* \[[`95d8a273cc`](https://github.com/nodejs/node/commit/95d8a273cc)] - **deps**: cherry-pick bfbe4e38d7 from libuv upstream (Abdirahim Musse) [#50650](https://github.com/nodejs/node/pull/50650)
* \[[`06038a489e`](https://github.com/nodejs/node/commit/06038a489e)] - **deps**: update libuv to 1.47.0 (Node.js GitHub Bot) [#50650](https://github.com/nodejs/node/pull/50650)
* \[[`0dd53da722`](https://github.com/nodejs/node/commit/0dd53da722)] - **(SEMVER-MINOR)** **deps**: add simdjson (Yagiz Nizipli) [#50322](https://github.com/nodejs/node/pull/50322)
* \[[`04eaa5cdd7`](https://github.com/nodejs/node/commit/04eaa5cdd7)] - **doc**: run license-builder (github-actions\[bot]) [#51111](https://github.com/nodejs/node/pull/51111)
* \[[`26ed4ad01f`](https://github.com/nodejs/node/commit/26ed4ad01f)] - **doc**: deprecate hash constructor (Marco Ippolito) [#51077](https://github.com/nodejs/node/pull/51077)
* \[[`637ffce4c4`](https://github.com/nodejs/node/commit/637ffce4c4)] - **doc**: add note regarding `--experimental-detect-module` (Shubherthi Mitra) [#51089](https://github.com/nodejs/node/pull/51089)
* \[[`838179b096`](https://github.com/nodejs/node/commit/838179b096)] - **doc**: correct tracingChannel.traceCallback() (Gerhard Stöbich) [#51068](https://github.com/nodejs/node/pull/51068)
* \[[`539bee4f0a`](https://github.com/nodejs/node/commit/539bee4f0a)] - **doc**: use length argument in pbkdf2Key (Tobias Nießen) [#51066](https://github.com/nodejs/node/pull/51066)
* \[[`c45a9a3187`](https://github.com/nodejs/node/commit/c45a9a3187)] - **doc**: add deprecation notice to `dirent.path` (Antoine du Hamel) [#51059](https://github.com/nodejs/node/pull/51059)
* \[[`58ca66a1a7`](https://github.com/nodejs/node/commit/58ca66a1a7)] - **doc**: deprecate `dirent.path` (Antoine du Hamel) [#51020](https://github.com/nodejs/node/pull/51020)
* \[[`c2b6edf9ab`](https://github.com/nodejs/node/commit/c2b6edf9ab)] - **esm**: fix hook name in error message (Bruce MacNaughton) [#50466](https://github.com/nodejs/node/pull/50466)
* \[[`35e8f26f07`](https://github.com/nodejs/node/commit/35e8f26f07)] - **fs**: throw fchownSync error from c++ (Yagiz Nizipli) [#51075](https://github.com/nodejs/node/pull/51075)
* \[[`c3c8237089`](https://github.com/nodejs/node/commit/c3c8237089)] - **fs**: update params in jsdoc for createReadStream and createWriteStream (Jungku Lee) [#51063](https://github.com/nodejs/node/pull/51063)
* \[[`3f7f3ce8c9`](https://github.com/nodejs/node/commit/3f7f3ce8c9)] - **fs**: improve error performance of readvSync (IlyasShabi) [#50100](https://github.com/nodejs/node/pull/50100)
* \[[`7f95926f17`](https://github.com/nodejs/node/commit/7f95926f17)] - **http**: handle multi-value content-disposition header (Arsalan Ahmad) [#50977](https://github.com/nodejs/node/pull/50977)
* \[[`7a8a2d5632`](https://github.com/nodejs/node/commit/7a8a2d5632)] - **lib**: don't parse windows drive letters as schemes (华) [#50580](https://github.com/nodejs/node/pull/50580)
* \[[`aa2be4bb76`](https://github.com/nodejs/node/commit/aa2be4bb76)] - **module**: load source maps in `commonjs` translator (Hiroki Osame) [#51033](https://github.com/nodejs/node/pull/51033)
* \[[`c0e5e74876`](https://github.com/nodejs/node/commit/c0e5e74876)] - **module**: document `parentURL` in register options (Hiroki Osame) [#51039](https://github.com/nodejs/node/pull/51039)
* \[[`4eedf5e694`](https://github.com/nodejs/node/commit/4eedf5e694)] - **module**: fix recently introduced coverity warning (Michael Dawson) [#50843](https://github.com/nodejs/node/pull/50843)
* \[[`9f54987fbc`](https://github.com/nodejs/node/commit/9f54987fbc)] -  **module**: merge config with `package_json_reader` (Yagiz Nizipli) [#50322](https://github.com/nodejs/node/pull/50322)
* \[[`5f95dca638`](https://github.com/nodejs/node/commit/5f95dca638)] - **node-api**: introduce experimental feature flags (Gabriel Schulhof) [#50991](https://github.com/nodejs/node/pull/50991)
* \[[`3fb7fc909e`](https://github.com/nodejs/node/commit/3fb7fc909e)] - **quic**: further implementation details (James M Snell) [#48244](https://github.com/nodejs/node/pull/48244)
* \[[`fa25e069fc`](https://github.com/nodejs/node/commit/fa25e069fc)] - **src**: implement countObjectsWithPrototype (Joyee Cheung) [#50572](https://github.com/nodejs/node/pull/50572)
* \[[`abe90527e4`](https://github.com/nodejs/node/commit/abe90527e4)] - **src**: register udp\_wrap external references (Joyee Cheung) [#50943](https://github.com/nodejs/node/pull/50943)
* \[[`84e2f51d14`](https://github.com/nodejs/node/commit/84e2f51d14)] - **src**: register spawn\_sync external references (Joyee Cheung) [#50943](https://github.com/nodejs/node/pull/50943)
* \[[`2cfee53d7b`](https://github.com/nodejs/node/commit/2cfee53d7b)] - **src**: register process\_wrap external references (Joyee Cheung) [#50943](https://github.com/nodejs/node/pull/50943)
* \[[`9b7f79a8bd`](https://github.com/nodejs/node/commit/9b7f79a8bd)] - **src**: fix double free reported by coverity (Michael Dawson) [#51046](https://github.com/nodejs/node/pull/51046)
* \[[`fc5503246e`](https://github.com/nodejs/node/commit/fc5503246e)] - **src**: remove unused headers in `node_file.cc` (Jungku Lee) [#50927](https://github.com/nodejs/node/pull/50927)
* \[[`c3abdc58af`](https://github.com/nodejs/node/commit/c3abdc58af)] - **src**: implement --trace-promises (Joyee Cheung) [#50899](https://github.com/nodejs/node/pull/50899)
* \[[`f90fc83e97`](https://github.com/nodejs/node/commit/f90fc83e97)] - **src**: fix dynamically linked zlib version (Richard Lau) [#51007](https://github.com/nodejs/node/pull/51007)
* \[[`9bf144379f`](https://github.com/nodejs/node/commit/9bf144379f)] - **src**: omit bool values of package.json main field (Yagiz Nizipli) [#50965](https://github.com/nodejs/node/pull/50965)
* \[[`45e4f82912`](https://github.com/nodejs/node/commit/45e4f82912)] -  **src**: move package resolver to c++ (Yagiz Nizipli) [#50322](https://github.com/nodejs/node/pull/50322)
* \[[`71acd36778`](https://github.com/nodejs/node/commit/71acd36778)] - **stream**: implement TransformStream cleanup using "transformer.cancel" (Debadree Chatterjee) [#50126](https://github.com/nodejs/node/pull/50126)
* \[[`5112306064`](https://github.com/nodejs/node/commit/5112306064)] - **stream**: fix fd is null when calling clearBuffer (kylo5aby) [#50994](https://github.com/nodejs/node/pull/50994)
* \[[`ed070755ec`](https://github.com/nodejs/node/commit/ed070755ec)] - **test**: deflake test-diagnostics-channel-memory-leak (Joyee Cheung) [#50572](https://github.com/nodejs/node/pull/50572)
* \[[`aee01ff1b4`](https://github.com/nodejs/node/commit/aee01ff1b4)] - **test**: test syncrhnous methods of child\_process in snapshot (Joyee Cheung) [#50943](https://github.com/nodejs/node/pull/50943)
* \[[`cc949869a3`](https://github.com/nodejs/node/commit/cc949869a3)] - **test**: handle relative https redirect (Richard Lau) [#51121](https://github.com/nodejs/node/pull/51121)
* \[[`048349ed4c`](https://github.com/nodejs/node/commit/048349ed4c)] - **test**: fix test runner colored output test (Moshe Atlow) [#51064](https://github.com/nodejs/node/pull/51064)
* \[[`7f5291d783`](https://github.com/nodejs/node/commit/7f5291d783)] - **test**: resolve path of embedtest binary correctly (Cheng Zhao) [#50276](https://github.com/nodejs/node/pull/50276)
* \[[`4ddd0daf5f`](https://github.com/nodejs/node/commit/4ddd0daf5f)] - **test**: escape cwd in regexp (Jérémy Lal) [#50980](https://github.com/nodejs/node/pull/50980)
* \[[`3ccd5faabb`](https://github.com/nodejs/node/commit/3ccd5faabb)] - **test\_runner**: format coverage report for tap reporter (Pulkit Gupta) [#51119](https://github.com/nodejs/node/pull/51119)
* \[[`d5c9adf3df`](https://github.com/nodejs/node/commit/d5c9adf3df)] - **test\_runner**: fix infinite loop when files are undefined in test runner (Pulkit Gupta) [#51047](https://github.com/nodejs/node/pull/51047)
* \[[`328a41701c`](https://github.com/nodejs/node/commit/328a41701c)] - **tools**: update lint-md-dependencies to rollup\@4.7.0 (Node.js GitHub Bot) [#51106](https://github.com/nodejs/node/pull/51106)
* \[[`297cb6f5c2`](https://github.com/nodejs/node/commit/297cb6f5c2)] - **tools**: update doc to highlight.js\@11.9.0 unified\@11.0.4 (Node.js GitHub Bot) [#50459](https://github.com/nodejs/node/pull/50459)
* \[[`4705023343`](https://github.com/nodejs/node/commit/4705023343)] - **tools**: fix simdjson updater (Yagiz Nizipli) [#50986](https://github.com/nodejs/node/pull/50986)
* \[[`c9841583db`](https://github.com/nodejs/node/commit/c9841583db)] - **tools**: update eslint to 8.55.0 (Node.js GitHub Bot) [#51025](https://github.com/nodejs/node/pull/51025)
* \[[`2b4671125e`](https://github.com/nodejs/node/commit/2b4671125e)] - **tools**: update lint-md-dependencies to rollup\@4.6.1 (Node.js GitHub Bot) [#51022](https://github.com/nodejs/node/pull/51022)
* \[[`cd891b37f6`](https://github.com/nodejs/node/commit/cd891b37f6)] - **util**: improve performance of function areSimilarFloatArrays (Liu Jia) [#51040](https://github.com/nodejs/node/pull/51040)
* \[[`e178a43509`](https://github.com/nodejs/node/commit/e178a43509)] - **vm**: use v8::DeserializeInternalFieldsCallback explicitly (Joyee Cheung) [#50984](https://github.com/nodejs/node/pull/50984)
* \[[`fd028e146f`](https://github.com/nodejs/node/commit/fd028e146f)] - **win,tools**: upgrade Windows signing to smctl (Stefan Stojanovic) [#50956](https://github.com/nodejs/node/pull/50956)

<a id="21.4.0"></a>

## 2023-12-05, Version 21.4.0 (Current), @targos

### Notable Changes

This release fixes a regression introduced in v21.3.0 that caused the `fs.writeFileSync`
method to throw when called with `'utf8'` encoding, no flag option, and if the target file didn't exist yet.

* \[[`32acafeeb6`](https://github.com/nodejs/node/commit/32acafeeb6)] - **(SEMVER-MINOR)** **fs**: introduce `dirent.parentPath` (Antoine du Hamel) [#50976](https://github.com/nodejs/node/pull/50976)
* \[[`724548674d`](https://github.com/nodejs/node/commit/724548674d)] - **fs**: use default w flag for writeFileSync with utf8 encoding (Murilo Kakazu) [#50990](https://github.com/nodejs/node/pull/50990)

### Commits

* \[[`b24ee15fb2`](https://github.com/nodejs/node/commit/b24ee15fb2)] - **benchmark**: update iterations in benchmark/crypto/hkdf.js (Lei Shi) [#50866](https://github.com/nodejs/node/pull/50866)
* \[[`f79b54e60e`](https://github.com/nodejs/node/commit/f79b54e60e)] - **benchmark**: update iterations in benchmark/crypto/get-ciphers.js (Lei Shi) [#50863](https://github.com/nodejs/node/pull/50863)
* \[[`dc049acbbb`](https://github.com/nodejs/node/commit/dc049acbbb)] - **benchmark**: update number of iterations for `util.inspect` (kylo5aby) [#50651](https://github.com/nodejs/node/pull/50651)
* \[[`d7c562ae38`](https://github.com/nodejs/node/commit/d7c562ae38)] - **deps**: update googletest to 76bb2af (Node.js GitHub Bot) [#50555](https://github.com/nodejs/node/pull/50555)
* \[[`59a45ddbef`](https://github.com/nodejs/node/commit/59a45ddbef)] - **deps**: update googletest to b10fad3 (Node.js GitHub Bot) [#50555](https://github.com/nodejs/node/pull/50555)
* \[[`099ebdb781`](https://github.com/nodejs/node/commit/099ebdb781)] - **deps**: update undici to 5.28.1 (Node.js GitHub Bot) [#50975](https://github.com/nodejs/node/pull/50975)
* \[[`4b1bed04f7`](https://github.com/nodejs/node/commit/4b1bed04f7)] - **deps**: update undici to 5.28.0 (Node.js GitHub Bot) [#50915](https://github.com/nodejs/node/pull/50915)
* \[[`b281e98b1e`](https://github.com/nodejs/node/commit/b281e98b1e)] - **doc**: add additional details about `--input-type` (Shubham Pandey) [#50796](https://github.com/nodejs/node/pull/50796)
* \[[`b7036f2028`](https://github.com/nodejs/node/commit/b7036f2028)] - **doc**: add procedure when CVEs don't get published (Rafael Gonzaga) [#50945](https://github.com/nodejs/node/pull/50945)
* \[[`7adf239af0`](https://github.com/nodejs/node/commit/7adf239af0)] - **doc**: fix some errors in esm resolution algorithms (Christopher Jeffrey (JJ)) [#50898](https://github.com/nodejs/node/pull/50898)
* \[[`759ebcaead`](https://github.com/nodejs/node/commit/759ebcaead)] - **doc**: reserve 121 for Electron 29 (Shelley Vohr) [#50957](https://github.com/nodejs/node/pull/50957)
* \[[`cedc3427fa`](https://github.com/nodejs/node/commit/cedc3427fa)] - **doc**: run license-builder (github-actions\[bot]) [#50926](https://github.com/nodejs/node/pull/50926)
* \[[`30a6f19769`](https://github.com/nodejs/node/commit/30a6f19769)] - **doc**: document non-node\_modules-only runtime deprecation (Joyee Cheung) [#50748](https://github.com/nodejs/node/pull/50748)
* \[[`eecab883f0`](https://github.com/nodejs/node/commit/eecab883f0)] - **doc**: add doc for Unix abstract socket (theanarkh) [#50904](https://github.com/nodejs/node/pull/50904)
* \[[`ec74b93b38`](https://github.com/nodejs/node/commit/ec74b93b38)] - **doc**: remove flicker on page load on dark theme (Dima Demakov) [#50942](https://github.com/nodejs/node/pull/50942)
* \[[`724548674d`](https://github.com/nodejs/node/commit/724548674d)] - **fs**: use default w flag for writeFileSync with utf8 encoding (Murilo Kakazu) [#50990](https://github.com/nodejs/node/pull/50990)
* \[[`32acafeeb6`](https://github.com/nodejs/node/commit/32acafeeb6)] - **(SEMVER-MINOR)** **fs**: introduce `dirent.parentPath` (Antoine du Hamel) [#50976](https://github.com/nodejs/node/pull/50976)
* \[[`c1ee506454`](https://github.com/nodejs/node/commit/c1ee506454)] - **fs**: remove workaround for `esm` package (Yagiz Nizipli) [#50907](https://github.com/nodejs/node/pull/50907)
* \[[`1cf087dfb3`](https://github.com/nodejs/node/commit/1cf087dfb3)] - **lib**: refactor to use validateFunction in diagnostics\_channel (Deokjin Kim) [#50955](https://github.com/nodejs/node/pull/50955)
* \[[`c37d18d5e1`](https://github.com/nodejs/node/commit/c37d18d5e1)] - **lib**: streamline process.binding() handling (Joyee Cheung) [#50773](https://github.com/nodejs/node/pull/50773)
* \[[`246cf73631`](https://github.com/nodejs/node/commit/246cf73631)] - **lib,src**: replace toUSVString with `toWellFormed()` (Yagiz Nizipli) [#47342](https://github.com/nodejs/node/pull/47342)
* \[[`9bc79173a0`](https://github.com/nodejs/node/commit/9bc79173a0)] - **loader**: speed up line length calc used by moduleProvider (Mudit) [#50969](https://github.com/nodejs/node/pull/50969)
* \[[`812ab9e4f8`](https://github.com/nodejs/node/commit/812ab9e4f8)] - **meta**: bump step-security/harden-runner from 2.6.0 to 2.6.1 (dependabot\[bot]) [#50999](https://github.com/nodejs/node/pull/50999)
* \[[`1dbe1af19a`](https://github.com/nodejs/node/commit/1dbe1af19a)] - **meta**: bump github/codeql-action from 2.22.5 to 2.22.8 (dependabot\[bot]) [#50998](https://github.com/nodejs/node/pull/50998)
* \[[`bed1b93f8a`](https://github.com/nodejs/node/commit/bed1b93f8a)] - **meta**: move one or more collaborators to emeritus (Node.js GitHub Bot) [#50931](https://github.com/nodejs/node/pull/50931)
* \[[`1e7d101428`](https://github.com/nodejs/node/commit/1e7d101428)] - **src**: make ModifyCodeGenerationFromStrings more robust (Joyee Cheung) [#50763](https://github.com/nodejs/node/pull/50763)
* \[[`709ac479eb`](https://github.com/nodejs/node/commit/709ac479eb)] - **src**: disable uncaught exception abortion for ESM syntax detection (Yagiz Nizipli) [#50987](https://github.com/nodejs/node/pull/50987)
* \[[`f6ff11c9f9`](https://github.com/nodejs/node/commit/f6ff11c9f9)] - **src**: fix backtrace with tail \[\[noreturn]] abort (Chengzhong Wu) [#50849](https://github.com/nodejs/node/pull/50849)
* \[[`74f5a1cbc9`](https://github.com/nodejs/node/commit/74f5a1cbc9)] - **src**: print MKSNAPSHOT debug logs to stderr (Joyee Cheung) [#50759](https://github.com/nodejs/node/pull/50759)
* \[[`3a1c664a97`](https://github.com/nodejs/node/commit/3a1c664a97)] - **test**: replace forEach to for.. test-webcrypto-export-import-cfrg.js (Angelo Parziale) [#50785](https://github.com/nodejs/node/pull/50785)
* \[[`ac3a6eefe3`](https://github.com/nodejs/node/commit/ac3a6eefe3)] - **test**: log more information in SEA tests (Joyee Cheung) [#50759](https://github.com/nodejs/node/pull/50759)
* \[[`94462d42f5`](https://github.com/nodejs/node/commit/94462d42f5)] - **test**: consolidate utf8 text fixtures in tests (Joyee Cheung) [#50732](https://github.com/nodejs/node/pull/50732)
* \[[`8e1a70a347`](https://github.com/nodejs/node/commit/8e1a70a347)] - **tools**: add triggers to update release links workflow (Moshe Atlow) [#50974](https://github.com/nodejs/node/pull/50974)
* \[[`ca10cbb774`](https://github.com/nodejs/node/commit/ca10cbb774)] - **tools**: update lint-md-dependencies to rollup\@4.5.2 (Node.js GitHub Bot) [#50913](https://github.com/nodejs/node/pull/50913)
* \[[`1e40c4a366`](https://github.com/nodejs/node/commit/1e40c4a366)] - **tools**: fix current version check (Marco Ippolito) [#50951](https://github.com/nodejs/node/pull/50951)
* \[[`3faed331e1`](https://github.com/nodejs/node/commit/3faed331e1)] - **typings**: fix JSDoc in `internal/modules/esm/hooks` (Alex Yang) [#50887](https://github.com/nodejs/node/pull/50887)
* \[[`6a087ceffa`](https://github.com/nodejs/node/commit/6a087ceffa)] - **url**: throw error if argument length of revokeObjectURL is 0 (DylanTet) [#50433](https://github.com/nodejs/node/pull/50433)

<a id="21.3.0"></a>

## 2023-11-30, Version 21.3.0 (Current), @RafaelGSS

### Notable Changes

#### New `--disable-warning` flag

This version adds a new `--disable-warning` option that allows users to disable specific warnings either by code
(i.e. DEP0025) or type (i.e. DeprecationWarning, ExperimentalWarning).

This option works alongside existing `--warnings` and `--no-warnings`.

For example, the following script will not emit DEP0025 `require('node:sys')` when executed with
`node --disable-warning=DEP0025`:

```mjs
import sys from 'node:sys';
```

Contributed by Ethan-Arrowood in [#50661](https://github.com/nodejs/node/pull/50661)

#### Update Root Certificates to NSS 3.95

This is the [certdata.txt](https://hg.mozilla.org/projects/nss/raw-file/NSS_3_95_RTM/lib/ckfw/builtins/certdata.txt) from NSS 3.95, released on 2023-11-16.

This is the version of NSS that will ship in Firefox 121 on
2023-12-19.

Certificates added:

* TrustAsia Global Root CA G3
* TrustAsia Global Root CA G4
* CommScope Public Trust ECC Root-01
* CommScope Public Trust ECC Root-02
* CommScope Public Trust RSA Root-01
* CommScope Public Trust RSA Root-02

Certificates removed:

* Autoridad de Certificacion Firmaprofesional CIF A62634068

#### Fast fs.writeFileSync with UTF-8 Strings

Enhanced writeFileSync functionality by implementing a highly efficient fast path primarily in C++ for UTF8-encoded string data.
Additionally, optimized the `appendFileSync` method by leveraging the improved `writeFileSync` functionality.
For simplicity and performance considerations, the current implementation supports only string data,
as benchmark results raise concerns about the efficacy of using Buffer for this purpose.
Future optimizations and expansions may be explored, but for now, the focus is on maximizing efficiency for string data operations.

Contributed by CanadaHonk in [#49884](https://github.com/nodejs/node/pull/49884).

#### Other Notable Changes

* \[[`c7a7493ca2`](https://github.com/nodejs/node/commit/c7a7493ca2)] - **(SEMVER-MINOR)** **module**: bootstrap module loaders in shadow realm (Chengzhong Wu) [#48655](https://github.com/nodejs/node/pull/48655)
* \[[`bc3f7b5401`](https://github.com/nodejs/node/commit/bc3f7b5401)] - **(SEMVER-MINOR)** **module**: remove useCustomLoadersIfPresent flag (Chengzhong Wu) [#48655](https://github.com/nodejs/node/pull/48655)
* \[[`aadff07e59`](https://github.com/nodejs/node/commit/aadff07e59)] - **(SEMVER-MINOR)** **src**: create per isolate proxy env template (Chengzhong Wu) [#48655](https://github.com/nodejs/node/pull/48655)
* \[[`91aa9dd23a`](https://github.com/nodejs/node/commit/91aa9dd23a)] - **(SEMVER-MINOR)** **src**: create fs\_dir per isolate properties (Chengzhong Wu) [#48655](https://github.com/nodejs/node/pull/48655)
* \[[`5c5834190a`](https://github.com/nodejs/node/commit/5c5834190a)] - **(SEMVER-MINOR)** **src**: create worker per isolate properties (Chengzhong Wu) [#48655](https://github.com/nodejs/node/pull/48655)
* \[[`4a1ce45181`](https://github.com/nodejs/node/commit/4a1ce45181)] - **(SEMVER-MINOR)** **src**: make process binding data weak (Chengzhong Wu) [#48655](https://github.com/nodejs/node/pull/48655)

### Commits

* \[[`4a20912279`](https://github.com/nodejs/node/commit/4a20912279)] - **benchmark**: update iterations in benchmark/util/splice-one.js (Liu Jia) [#50698](https://github.com/nodejs/node/pull/50698)
* \[[`36380eb53d`](https://github.com/nodejs/node/commit/36380eb53d)] - **benchmark**: increase the iteration number to an appropriate value (Lei Shi) [#50766](https://github.com/nodejs/node/pull/50766)
* \[[`23f56d8bb3`](https://github.com/nodejs/node/commit/23f56d8bb3)] - **benchmark**: rewrite import.meta benchmark (Joyee Cheung) [#50683](https://github.com/nodejs/node/pull/50683)
* \[[`f7245d73d9`](https://github.com/nodejs/node/commit/f7245d73d9)] - **benchmark**: add misc/startup-cli-version benchmark (Joyee Cheung) [#50684](https://github.com/nodejs/node/pull/50684)
* \[[`c81d2acfe0`](https://github.com/nodejs/node/commit/c81d2acfe0)] - **benchmark**: remove punycode from require-builtins fixture (Joyee Cheung) [#50689](https://github.com/nodejs/node/pull/50689)
* \[[`5849f09874`](https://github.com/nodejs/node/commit/5849f09874)] - **build**: add GN configurations for simdjson (Cheng Zhao) [#50831](https://github.com/nodejs/node/pull/50831)
* \[[`12605e8f7d`](https://github.com/nodejs/node/commit/12605e8f7d)] - **build**: add configuration flag to enable Maglev (Keyhan Vakil) [#50692](https://github.com/nodejs/node/pull/50692)
* \[[`43da9ea9e5`](https://github.com/nodejs/node/commit/43da9ea9e5)] - **build**: fix GN configuration for deps/base64 (Cheng Zhao) [#50696](https://github.com/nodejs/node/pull/50696)
* \[[`465f75b58a`](https://github.com/nodejs/node/commit/465f75b58a)] - **build**: disable flag v8\_scriptormodule\_legacy\_lifetime (Chengzhong Wu) [#50616](https://github.com/nodejs/node/pull/50616)
* \[[`d2c0dfb1b7`](https://github.com/nodejs/node/commit/d2c0dfb1b7)] - **crypto**: update root certificates to NSS 3.95 (Node.js GitHub Bot) [#50805](https://github.com/nodejs/node/pull/50805)
* \[[`8d3a1d8911`](https://github.com/nodejs/node/commit/8d3a1d8911)] - **deps**: update zlib to 1.2.13.1-motley-5daffc7 (Node.js GitHub Bot) [#50803](https://github.com/nodejs/node/pull/50803)
* \[[`e02f304de7`](https://github.com/nodejs/node/commit/e02f304de7)] - **deps**: V8: cherry-pick 0f9ebbc672c7 (Chengzhong Wu) [#50867](https://github.com/nodejs/node/pull/50867)
* \[[`c31ad5ceaa`](https://github.com/nodejs/node/commit/c31ad5ceaa)] - **deps**: update icu to 74.1 (Node.js GitHub Bot) [#50515](https://github.com/nodejs/node/pull/50515)
* \[[`3ff2bda34e`](https://github.com/nodejs/node/commit/3ff2bda34e)] - **deps**: update ada to 2.7.4 (Node.js GitHub Bot) [#50815](https://github.com/nodejs/node/pull/50815)
* \[[`221f02df6d`](https://github.com/nodejs/node/commit/221f02df6d)] - **deps**: update undici to 5.27.2 (Node.js GitHub Bot) [#50813](https://github.com/nodejs/node/pull/50813)
* \[[`ee69c613a2`](https://github.com/nodejs/node/commit/ee69c613a2)] - **deps**: update minimatch to 9.0.3 (Node.js GitHub Bot) [#50806](https://github.com/nodejs/node/pull/50806)
* \[[`00dab30fd2`](https://github.com/nodejs/node/commit/00dab30fd2)] - **deps**: V8: cherry-pick 475c8cdf9a95 (Keyhan Vakil) [#50680](https://github.com/nodejs/node/pull/50680)
* \[[`a0c01b23b4`](https://github.com/nodejs/node/commit/a0c01b23b4)] - **deps**: update simdutf to 4.0.4 (Node.js GitHub Bot) [#50772](https://github.com/nodejs/node/pull/50772)
* \[[`071e46ae56`](https://github.com/nodejs/node/commit/071e46ae56)] - **deps**: upgrade npm to 10.2.4 (npm team) [#50751](https://github.com/nodejs/node/pull/50751)
* \[[`5d28f8d18f`](https://github.com/nodejs/node/commit/5d28f8d18f)] - **deps**: escape Python strings correctly (Michaël Zasso) [#50695](https://github.com/nodejs/node/pull/50695)
* \[[`3731f836ed`](https://github.com/nodejs/node/commit/3731f836ed)] - **deps**: V8: cherry-pick 8f0b94671ddb (Lu Yahan) [#50654](https://github.com/nodejs/node/pull/50654)
* \[[`6dfe1023c3`](https://github.com/nodejs/node/commit/6dfe1023c3)] - **dns**: call handle.setServers() with a valid array (Luigi Pinca) [#50811](https://github.com/nodejs/node/pull/50811)
* \[[`2f13db475e`](https://github.com/nodejs/node/commit/2f13db475e)] - **doc**: make theme consistent across api and other docs (Dima Demakov) [#50877](https://github.com/nodejs/node/pull/50877)
* \[[`8c4976b732`](https://github.com/nodejs/node/commit/8c4976b732)] - **doc**: add a section regarding `instanceof` in `primordials.md` (Antoine du Hamel) [#50874](https://github.com/nodejs/node/pull/50874)
* \[[`6485687642`](https://github.com/nodejs/node/commit/6485687642)] - **doc**: update email to reflect affiliation (Yagiz Nizipli) [#50856](https://github.com/nodejs/node/pull/50856)
* \[[`bc31375a09`](https://github.com/nodejs/node/commit/bc31375a09)] - **doc**: shard not supported with watch mode (Pulkit Gupta) [#50640](https://github.com/nodejs/node/pull/50640)
* \[[`08c3b0ab20`](https://github.com/nodejs/node/commit/08c3b0ab20)] - **doc**: get rid of unnecessary `eslint-skip` comments (Antoine du Hamel) [#50829](https://github.com/nodejs/node/pull/50829)
* \[[`98fb1faff1`](https://github.com/nodejs/node/commit/98fb1faff1)] - **doc**: create deprecation code for isWebAssemblyCompiledModule (Marco Ippolito) [#50486](https://github.com/nodejs/node/pull/50486)
* \[[`e116fcdb01`](https://github.com/nodejs/node/commit/e116fcdb01)] - **doc**: add CanadaHonk to triagers (CanadaHonk) [#50848](https://github.com/nodejs/node/pull/50848)
* \[[`a37d9ee1e3`](https://github.com/nodejs/node/commit/a37d9ee1e3)] - **doc**: fix typos in --allow-fs-\* (Tobias Nießen) [#50845](https://github.com/nodejs/node/pull/50845)
* \[[`8468daf1a9`](https://github.com/nodejs/node/commit/8468daf1a9)] - **doc**: update Crypto API doc for x509.keyUsage (Daniel Meechan) [#50603](https://github.com/nodejs/node/pull/50603)
* \[[`b4935dde60`](https://github.com/nodejs/node/commit/b4935dde60)] - **doc**: fix fs.writeFileSync return value documentation (Ryan Zimmerman) [#50760](https://github.com/nodejs/node/pull/50760)
* \[[`ead9879a04`](https://github.com/nodejs/node/commit/ead9879a04)] - **doc**: update print results(detail) in `PerformanceEntry` (Jungku Lee) [#50723](https://github.com/nodejs/node/pull/50723)
* \[[`6b7403c5df`](https://github.com/nodejs/node/commit/6b7403c5df)] - **doc**: fix `Buffer.allocUnsafe` documentation (Mert Can Altın) [#50686](https://github.com/nodejs/node/pull/50686)
* \[[`713fdf1fc3`](https://github.com/nodejs/node/commit/713fdf1fc3)] - **doc**: run license-builder (github-actions\[bot]) [#50691](https://github.com/nodejs/node/pull/50691)
* \[[`50f336c06f`](https://github.com/nodejs/node/commit/50f336c06f)] - **esm**: fallback to `getSource` when `load` returns nullish `source` (Antoine du Hamel) [#50825](https://github.com/nodejs/node/pull/50825)
* \[[`bd58870556`](https://github.com/nodejs/node/commit/bd58870556)] - **esm**: do not call `getSource` when format is `commonjs` (Francesco Trotta) [#50465](https://github.com/nodejs/node/pull/50465)
* \[[`e59268a076`](https://github.com/nodejs/node/commit/e59268a076)] - **fs**: add c++ fast path for writeFileSync utf8 (CanadaHonk) [#49884](https://github.com/nodejs/node/pull/49884)
* \[[`483200f68f`](https://github.com/nodejs/node/commit/483200f68f)] - **fs**: improve error performance for `rmdirSync` (CanadaHonk) [#49846](https://github.com/nodejs/node/pull/49846)
* \[[`e4e0add0de`](https://github.com/nodejs/node/commit/e4e0add0de)] - **fs**: fix glob returning duplicates (Moshe Atlow) [#50881](https://github.com/nodejs/node/pull/50881)
* \[[`45b2bb09f2`](https://github.com/nodejs/node/commit/45b2bb09f2)] - **fs**: fix to not return for void function (Jungku Lee) [#50769](https://github.com/nodejs/node/pull/50769)
* \[[`492e3e30b7`](https://github.com/nodejs/node/commit/492e3e30b7)] - **fs**: replace deprecated `path._makeLong` in copyFile (CanadaHonk) [#50844](https://github.com/nodejs/node/pull/50844)
* \[[`9dc4cde75b`](https://github.com/nodejs/node/commit/9dc4cde75b)] - **fs**: improve error perf of sync `lstat`+`fstat` (CanadaHonk) [#49868](https://github.com/nodejs/node/pull/49868)
* \[[`c3eee590be`](https://github.com/nodejs/node/commit/c3eee590be)] - **inspector**: use private fields instead of symbols (Yagiz Nizipli) [#50776](https://github.com/nodejs/node/pull/50776)
* \[[`1a0069b13d`](https://github.com/nodejs/node/commit/1a0069b13d)] - **meta**: clarify nomination process according to Node.js charter (Matteo Collina) [#50834](https://github.com/nodejs/node/pull/50834)
* \[[`65a811a86d`](https://github.com/nodejs/node/commit/65a811a86d)] - **meta**: clarify recommendation for bug reproductions (Antoine du Hamel) [#50882](https://github.com/nodejs/node/pull/50882)
* \[[`5811a59016`](https://github.com/nodejs/node/commit/5811a59016)] - **meta**: move cjihrig to TSC regular member (Colin Ihrig) [#50816](https://github.com/nodejs/node/pull/50816)
* \[[`c7a7493ca2`](https://github.com/nodejs/node/commit/c7a7493ca2)] - **(SEMVER-MINOR)** **module**: bootstrap module loaders in shadow realm (Chengzhong Wu) [#48655](https://github.com/nodejs/node/pull/48655)
* \[[`bc3f7b5401`](https://github.com/nodejs/node/commit/bc3f7b5401)] - **(SEMVER-MINOR)** **module**: remove useCustomLoadersIfPresent flag (Chengzhong Wu) [#48655](https://github.com/nodejs/node/pull/48655)
* \[[`9197b0f2fc`](https://github.com/nodejs/node/commit/9197b0f2fc)] - **net**: check pipe mode and path (theanarkh) [#50770](https://github.com/nodejs/node/pull/50770)
* \[[`673de300b4`](https://github.com/nodejs/node/commit/673de300b4)] - **node-api**: factor out common code into macros (Gabriel Schulhof) [#50664](https://github.com/nodejs/node/pull/50664)
* \[[`aebe2fc702`](https://github.com/nodejs/node/commit/aebe2fc702)] - **perf\_hooks**: implement performance.now() with fast API calls (Joyee Cheung) [#50492](https://github.com/nodejs/node/pull/50492)
* \[[`3fdecc4a8b`](https://github.com/nodejs/node/commit/3fdecc4a8b)] - **permission**: do not create symlinks if target is relative (Tobias Nießen) [#49156](https://github.com/nodejs/node/pull/49156)
* \[[`27a4f58640`](https://github.com/nodejs/node/commit/27a4f58640)] - **permission**: mark const functions as such (Tobias Nießen) [#50705](https://github.com/nodejs/node/pull/50705)
* \[[`feb8ff9427`](https://github.com/nodejs/node/commit/feb8ff9427)] - **src**: assert return value of BN\_bn2binpad (Tobias Nießen) [#50860](https://github.com/nodejs/node/pull/50860)
* \[[`fd9195d750`](https://github.com/nodejs/node/commit/fd9195d750)] - **src**: fix coverity warning (Michael Dawson) [#50846](https://github.com/nodejs/node/pull/50846)
* \[[`adcab85c0c`](https://github.com/nodejs/node/commit/adcab85c0c)] - **src**: fix compatility with upcoming V8 12.1 APIs (Cheng Zhao) [#50709](https://github.com/nodejs/node/pull/50709)
* \[[`79ef39b8c8`](https://github.com/nodejs/node/commit/79ef39b8c8)] - **(SEMVER-MINOR)** **src**: add `--disable-warning` option (Ethan Arrowood) [#50661](https://github.com/nodejs/node/pull/50661)
* \[[`faf6a04ba6`](https://github.com/nodejs/node/commit/faf6a04ba6)] - **src**: add IsolateScopes before using isolates (Keyhan Vakil) [#50680](https://github.com/nodejs/node/pull/50680)
* \[[`eacf4ba485`](https://github.com/nodejs/node/commit/eacf4ba485)] - **src**: iterate on import attributes array correctly (Michaël Zasso) [#50703](https://github.com/nodejs/node/pull/50703)
* \[[`0fb35b6a67`](https://github.com/nodejs/node/commit/0fb35b6a67)] - **src**: avoid copying strings in FSPermission::Apply (Tobias Nießen) [#50662](https://github.com/nodejs/node/pull/50662)
* \[[`83ad272fa6`](https://github.com/nodejs/node/commit/83ad272fa6)] - **src**: remove erroneous default argument in RadixTree (Tobias Nießen) [#50736](https://github.com/nodejs/node/pull/50736)
* \[[`2e8e237ce2`](https://github.com/nodejs/node/commit/2e8e237ce2)] - **src**: fix JSONParser leaking internal V8 scopes (Keyhan Vakil) [#50688](https://github.com/nodejs/node/pull/50688)
* \[[`0d3aa725cf`](https://github.com/nodejs/node/commit/0d3aa725cf)] - **src**: return error --env-file if file is not found (Ardi Nugraha) [#50588](https://github.com/nodejs/node/pull/50588)
* \[[`aadff07e59`](https://github.com/nodejs/node/commit/aadff07e59)] - **(SEMVER-MINOR)** **src**: create per isolate proxy env template (Chengzhong Wu) [#48655](https://github.com/nodejs/node/pull/48655)
* \[[`91aa9dd23a`](https://github.com/nodejs/node/commit/91aa9dd23a)] - **(SEMVER-MINOR)** **src**: create fs\_dir per isolate properties (Chengzhong Wu) [#48655](https://github.com/nodejs/node/pull/48655)
* \[[`5c5834190a`](https://github.com/nodejs/node/commit/5c5834190a)] - **(SEMVER-MINOR)** **src**: create worker per isolate properties (Chengzhong Wu) [#48655](https://github.com/nodejs/node/pull/48655)
* \[[`4a1ce45181`](https://github.com/nodejs/node/commit/4a1ce45181)] - **(SEMVER-MINOR)** **src**: make process binding data weak (Chengzhong Wu) [#48655](https://github.com/nodejs/node/pull/48655)
* \[[`8746073664`](https://github.com/nodejs/node/commit/8746073664)] - **src**: avoid silent coercion to signed/unsigned int (Tobias Nießen) [#50663](https://github.com/nodejs/node/pull/50663)
* \[[`57587de1fa`](https://github.com/nodejs/node/commit/57587de1fa)] - **src**: handle errors from uv\_pipe\_connect2() (Deokjin Kim) [#50657](https://github.com/nodejs/node/pull/50657)
* \[[`e5cce004e8`](https://github.com/nodejs/node/commit/e5cce004e8)] - **stream**: fix enumerability of ReadableStream.from (Mattias Buelens) [#50779](https://github.com/nodejs/node/pull/50779)
* \[[`4522e229c0`](https://github.com/nodejs/node/commit/4522e229c0)] - **stream**: fix enumerability of ReadableStream.prototype.values (Mattias Buelens) [#50779](https://github.com/nodejs/node/pull/50779)
* \[[`2e0abed973`](https://github.com/nodejs/node/commit/2e0abed973)] - **stream**: yield expected Error class on zlib errors (Filip Skokan) [#50712](https://github.com/nodejs/node/pull/50712)
* \[[`a275155e81`](https://github.com/nodejs/node/commit/a275155e81)] - **stream**: add Symbol.toStringTag to Compression Streams (Filip Skokan) [#50712](https://github.com/nodejs/node/pull/50712)
* \[[`146ad9cab0`](https://github.com/nodejs/node/commit/146ad9cab0)] - **stream**: treat compression web stream format per its WebIDL definition (Filip Skokan) [#50631](https://github.com/nodejs/node/pull/50631)
* \[[`087cffc7c2`](https://github.com/nodejs/node/commit/087cffc7c2)] - **test**: fix message v8 not normalising alphanumeric paths (Jithil P Ponnan) [#50730](https://github.com/nodejs/node/pull/50730)
* \[[`7de900a442`](https://github.com/nodejs/node/commit/7de900a442)] - **test**: fix dns test case failures after c-ares update to 1.21.0+ (Brad House) [#50743](https://github.com/nodejs/node/pull/50743)
* \[[`b1b6c44712`](https://github.com/nodejs/node/commit/b1b6c44712)] - **test**: replace forEach with for of (Conor Watson) [#50594](https://github.com/nodejs/node/pull/50594)
* \[[`7f44164ad4`](https://github.com/nodejs/node/commit/7f44164ad4)] - **test**: replace forEach to for at test-webcrypto-sign-verify-ecdsa.js (Alessandro Di Nisio) [#50795](https://github.com/nodejs/node/pull/50795)
* \[[`9d76de1993`](https://github.com/nodejs/node/commit/9d76de1993)] - **test**: replace foreach with for in test-https-simple.js (Shikha Mehta) [#49793](https://github.com/nodejs/node/pull/49793)
* \[[`ce8fc56ee4`](https://github.com/nodejs/node/commit/ce8fc56ee4)] - **test**: add note about unresolved spec issue (Mattias Buelens) [#50779](https://github.com/nodejs/node/pull/50779)
* \[[`628a12ac18`](https://github.com/nodejs/node/commit/628a12ac18)] - **test**: add note about readable streams with type owning (Mattias Buelens) [#50779](https://github.com/nodejs/node/pull/50779)
* \[[`82f0882ce0`](https://github.com/nodejs/node/commit/82f0882ce0)] - **test**: replace forEach with for-of in test-url-relative (vitosorriso) [#50788](https://github.com/nodejs/node/pull/50788)
* \[[`3b7998305d`](https://github.com/nodejs/node/commit/3b7998305d)] - **test**: replace forEach() with for ... of in test-tls-getprotocol.js (Steve Goode) [#50600](https://github.com/nodejs/node/pull/50600)
* \[[`0e4d25eb5c`](https://github.com/nodejs/node/commit/0e4d25eb5c)] - **test**: use requires instead of flaky in console WPT status (Filip Skokan) [#50812](https://github.com/nodejs/node/pull/50812)
* \[[`221952a88e`](https://github.com/nodejs/node/commit/221952a88e)] - **test**: use ppc and ppc64 to skip SEA tests on PowerPC (Joyee Cheung) [#50828](https://github.com/nodejs/node/pull/50828)
* \[[`0e3b714069`](https://github.com/nodejs/node/commit/0e3b714069)] - **test**: enable idlharness tests for encoding (Mattias Buelens) [#50778](https://github.com/nodejs/node/pull/50778)
* \[[`c8d4cd68b4`](https://github.com/nodejs/node/commit/c8d4cd68b4)] - **test**: replace forEach in whatwg-encoding-custom-interop (Honza Machala) [#50607](https://github.com/nodejs/node/pull/50607)
* \[[`f25637b5c9`](https://github.com/nodejs/node/commit/f25637b5c9)] - **test**: update WPT files for WebIDL tests (Filip Skokan) [#50712](https://github.com/nodejs/node/pull/50712)
* \[[`f2e0fce389`](https://github.com/nodejs/node/commit/f2e0fce389)] - **test**: replace forEach() with for-loop (Jan) [#50596](https://github.com/nodejs/node/pull/50596)
* \[[`4b26f14a53`](https://github.com/nodejs/node/commit/4b26f14a53)] - **test**: improve test-bootstrap-modules.js (Joyee Cheung) [#50708](https://github.com/nodejs/node/pull/50708)
* \[[`28d78de0dd`](https://github.com/nodejs/node/commit/28d78de0dd)] - **test**: skip parallel/test-macos-app-sandbox if disk space < 120MB (Joyee Cheung) [#50764](https://github.com/nodejs/node/pull/50764)
* \[[`4088b750e7`](https://github.com/nodejs/node/commit/4088b750e7)] - **test**: mark SEA tests as flaky on PowerPC (Joyee Cheung) [#50750](https://github.com/nodejs/node/pull/50750)
* \[[`6475cee6a4`](https://github.com/nodejs/node/commit/6475cee6a4)] - **test**: give more time to GC in test-shadow-realm-gc-\* (Joyee Cheung) [#50735](https://github.com/nodejs/node/pull/50735)
* \[[`0e8275b610`](https://github.com/nodejs/node/commit/0e8275b610)] - **test**: replace foreach with for (Markus Muschol) [#50599](https://github.com/nodejs/node/pull/50599)
* \[[`377deded59`](https://github.com/nodejs/node/commit/377deded59)] - **test**: test streambase has already has a consumer (Jithil P Ponnan) [#48059](https://github.com/nodejs/node/pull/48059)
* \[[`342a83e728`](https://github.com/nodejs/node/commit/342a83e728)] - **test**: change forEach to for...of in path extname (Kyriakos Markakis) [#50667](https://github.com/nodejs/node/pull/50667)
* \[[`75265e491d`](https://github.com/nodejs/node/commit/75265e491d)] - **test**: replace forEach with for...of (Ryan Williams) [#50611](https://github.com/nodejs/node/pull/50611)
* \[[`982b57651b`](https://github.com/nodejs/node/commit/982b57651b)] - **test**: migrate message v8 tests from Python to JS (Joshua LeMay) [#50421](https://github.com/nodejs/node/pull/50421)
* \[[`7ebc8c2aed`](https://github.com/nodejs/node/commit/7ebc8c2aed)] - **test,stream**: enable compression WPTs (Filip Skokan) [#50631](https://github.com/nodejs/node/pull/50631)
* \[[`0bd694ab64`](https://github.com/nodejs/node/commit/0bd694ab64)] - **test\_runner**: add tests for various mock timer issues (Mika Fischer) [#50384](https://github.com/nodejs/node/pull/50384)
* \[[`dee8039c9b`](https://github.com/nodejs/node/commit/dee8039c9b)] - **tls**: fix order of setting cipher before setting cert and key (Kumar Rishav) [#50186](https://github.com/nodejs/node/pull/50186)
* \[[`5de30531b8`](https://github.com/nodejs/node/commit/5de30531b8)] - **tools**: add macOS notarization verification step (Ulises Gascón) [#50833](https://github.com/nodejs/node/pull/50833)
* \[[`a12d9e03f2`](https://github.com/nodejs/node/commit/a12d9e03f2)] - **tools**: use macOS keychain to notarize the releases (Ulises Gascón) [#50715](https://github.com/nodejs/node/pull/50715)
* \[[`f21637717f`](https://github.com/nodejs/node/commit/f21637717f)] - **tools**: update eslint to 8.54.0 (Node.js GitHub Bot) [#50809](https://github.com/nodejs/node/pull/50809)
* \[[`daa933d93a`](https://github.com/nodejs/node/commit/daa933d93a)] - **tools**: update lint-md-dependencies to rollup\@4.5.0 (Node.js GitHub Bot) [#50807](https://github.com/nodejs/node/pull/50807)
* \[[`52830b71cc`](https://github.com/nodejs/node/commit/52830b71cc)] - **tools**: add workflow to update release links (Michaël Zasso) [#50710](https://github.com/nodejs/node/pull/50710)
* \[[`db8ce5bbdd`](https://github.com/nodejs/node/commit/db8ce5bbdd)] - **tools**: recognize GN files in dep\_updaters (Cheng Zhao) [#50693](https://github.com/nodejs/node/pull/50693)
* \[[`5ef6729b66`](https://github.com/nodejs/node/commit/5ef6729b66)] - **tools**: remove unused file (Ulises Gascon) [#50622](https://github.com/nodejs/node/pull/50622)
* \[[`c49483820a`](https://github.com/nodejs/node/commit/c49483820a)] - **tools**: change minimatch install strategy (Marco Ippolito) [#50476](https://github.com/nodejs/node/pull/50476)
* \[[`0d556d9a59`](https://github.com/nodejs/node/commit/0d556d9a59)] - **tools**: update lint-md-dependencies to rollup\@4.3.1 (Node.js GitHub Bot) [#50675](https://github.com/nodejs/node/pull/50675)
* \[[`eaa4c14e6b`](https://github.com/nodejs/node/commit/eaa4c14e6b)] - **util**: improve performance of normalizeEncoding (kylo5aby) [#50721](https://github.com/nodejs/node/pull/50721)
* \[[`a5d959b765`](https://github.com/nodejs/node/commit/a5d959b765)] - **v8,tools**: expose necessary V8 defines (Cheng Zhao) [#50820](https://github.com/nodejs/node/pull/50820)

<a id="21.2.0"></a>

## 2023-11-14, Version 21.2.0 (Current), @targos

### Notable Changes

* \[[`e25c65ee2f`](https://github.com/nodejs/node/commit/e25c65ee2f)] - **doc**: add MrJithil to collaborators (Jithil P Ponnan) [#50666](https://github.com/nodejs/node/pull/50666)
* \[[`f2366573f9`](https://github.com/nodejs/node/commit/f2366573f9)] - **doc**: add Ethan-Arrowood as a collaborator (Ethan Arrowood) [#50393](https://github.com/nodejs/node/pull/50393)
* \[[`eac9cc5fcb`](https://github.com/nodejs/node/commit/eac9cc5fcb)] - **(SEMVER-MINOR)** **esm**: add import.meta.dirname and import.meta.filename (James Sumners) [#48740](https://github.com/nodejs/node/pull/48740)
* \[[`7e151114b1`](https://github.com/nodejs/node/commit/7e151114b1)] - **fs**: add stacktrace to fs/promises (翠 / green) [#49849](https://github.com/nodejs/node/pull/49849)
* \[[`6dbb280733`](https://github.com/nodejs/node/commit/6dbb280733)] - **(SEMVER-MINOR)** **lib**: add `--no-experimental-global-navigator` CLI flag (Antoine du Hamel) [#50562](https://github.com/nodejs/node/pull/50562)
* \[[`03c730b931`](https://github.com/nodejs/node/commit/03c730b931)] - **(SEMVER-MINOR)** **lib**: add navigator.language & navigator.languages (Aras Abbasi) [#50303](https://github.com/nodejs/node/pull/50303)
* \[[`f932f4c518`](https://github.com/nodejs/node/commit/f932f4c518)] - **(SEMVER-MINOR)** **lib**: add navigator.platform (Aras Abbasi) [#50385](https://github.com/nodejs/node/pull/50385)
* \[[`91f37d1dc3`](https://github.com/nodejs/node/commit/91f37d1dc3)] - **(SEMVER-MINOR)** **stream**: add support for `deflate-raw` format to webstreams compression (Damian Krzeminski) [#50097](https://github.com/nodejs/node/pull/50097)
* \[[`65850a67c7`](https://github.com/nodejs/node/commit/65850a67c7)] - **stream**: use Array for Readable buffer (Robert Nagy) [#50341](https://github.com/nodejs/node/pull/50341)
* \[[`e433fa54b7`](https://github.com/nodejs/node/commit/e433fa54b7)] - **stream**: optimize creation (Robert Nagy) [#50337](https://github.com/nodejs/node/pull/50337)
* \[[`c9b92bba58`](https://github.com/nodejs/node/commit/c9b92bba58)] - **(SEMVER-MINOR)** **test\_runner**: adds built in lcov reporter (Phil Nash) [#50018](https://github.com/nodejs/node/pull/50018)
* \[[`f6c496563e`](https://github.com/nodejs/node/commit/f6c496563e)] - **(SEMVER-MINOR)** **test\_runner**: add Date to the supported mock APIs (Lucas Santos) [#48638](https://github.com/nodejs/node/pull/48638)
* \[[`05e8b6ef20`](https://github.com/nodejs/node/commit/05e8b6ef20)] - **(SEMVER-MINOR)** **test\_runner, cli**: add --test-timeout flag (Shubham Pandey) [#50443](https://github.com/nodejs/node/pull/50443)

### Commits

* \[[`065d8844c5`](https://github.com/nodejs/node/commit/065d8844c5)] - **benchmark**: change iterations in benchmark/es/string-concatenations.js (Liu Jia) [#50585](https://github.com/nodejs/node/pull/50585)
* \[[`3f37ed9f0f`](https://github.com/nodejs/node/commit/3f37ed9f0f)] - **benchmark**: add benchmarks for encodings (Aras Abbasi) [#50348](https://github.com/nodejs/node/pull/50348)
* \[[`c4b6e1e9e4`](https://github.com/nodejs/node/commit/c4b6e1e9e4)] - **benchmark**: add more cases to Readable.from (Raz Luvaton) [#50351](https://github.com/nodejs/node/pull/50351)
* \[[`2006b57a9a`](https://github.com/nodejs/node/commit/2006b57a9a)] - **benchmark**: skip test-benchmark-os on IBMi (Michael Dawson) [#50286](https://github.com/nodejs/node/pull/50286)
* \[[`800206b04a`](https://github.com/nodejs/node/commit/800206b04a)] - **benchmark**: move permission-fs-read to permission-processhas-fs-read (Aki Hasegawa-Johnson) [#49770](https://github.com/nodejs/node/pull/49770)
* \[[`3bedaf9405`](https://github.com/nodejs/node/commit/3bedaf9405)] - **buffer**: improve Buffer.equals performance (kylo5aby) [#50621](https://github.com/nodejs/node/pull/50621)
* \[[`b9f3613908`](https://github.com/nodejs/node/commit/b9f3613908)] - **build**: add GN build files (Cheng Zhao) [#47637](https://github.com/nodejs/node/pull/47637)
* \[[`22eb0257d8`](https://github.com/nodejs/node/commit/22eb0257d8)] - **build**: fix build with Python 3.12 (Luigi Pinca) [#50582](https://github.com/nodejs/node/pull/50582)
* \[[`642c057299`](https://github.com/nodejs/node/commit/642c057299)] - **build**: support Python 3.12 (Shi Pujin) [#50209](https://github.com/nodejs/node/pull/50209)
* \[[`54ebfc10cb`](https://github.com/nodejs/node/commit/54ebfc10cb)] - **build**: fix building when there is only python3 (Cheng Zhao) [#48462](https://github.com/nodejs/node/pull/48462)
* \[[`5073a3e16d`](https://github.com/nodejs/node/commit/5073a3e16d)] - **deps**: update base64 to 0.5.1 (Node.js GitHub Bot) [#50629](https://github.com/nodejs/node/pull/50629)
* \[[`f70a59f4fa`](https://github.com/nodejs/node/commit/f70a59f4fa)] - **deps**: update corepack to 0.23.0 (Node.js GitHub Bot) [#50563](https://github.com/nodejs/node/pull/50563)
* \[[`78b3432be5`](https://github.com/nodejs/node/commit/78b3432be5)] - **deps**: V8: cherry-pick 13192d6e10fa (Levi Zim) [#50552](https://github.com/nodejs/node/pull/50552)
* \[[`93e3cc3907`](https://github.com/nodejs/node/commit/93e3cc3907)] - **deps**: upgrade npm to 10.2.3 (npm team) [#50531](https://github.com/nodejs/node/pull/50531)
* \[[`189e5e5326`](https://github.com/nodejs/node/commit/189e5e5326)] - **deps**: update nghttp2 to 1.58.0 (Node.js GitHub Bot) [#50441](https://github.com/nodejs/node/pull/50441)
* \[[`57bfe53095`](https://github.com/nodejs/node/commit/57bfe53095)] - **deps**: update zlib to 1.2.13.1-motley-dfc48fc (Node.js GitHub Bot) [#50456](https://github.com/nodejs/node/pull/50456)
* \[[`1e6922e67a`](https://github.com/nodejs/node/commit/1e6922e67a)] - **deps**: patch V8 to 11.8.172.17 (Michaël Zasso) [#50292](https://github.com/nodejs/node/pull/50292)
* \[[`28453ff966`](https://github.com/nodejs/node/commit/28453ff966)] - **deps**: update acorn to 8.11.2 (Node.js GitHub Bot) [#50460](https://github.com/nodejs/node/pull/50460)
* \[[`0a793a2566`](https://github.com/nodejs/node/commit/0a793a2566)] - **deps**: update undici to 5.27.0 (Node.js GitHub Bot) [#50463](https://github.com/nodejs/node/pull/50463)
* \[[`a90c6d669c`](https://github.com/nodejs/node/commit/a90c6d669c)] - **deps**: update archs files for openssl-3.0.12+quic1 (Node.js GitHub Bot) [#50411](https://github.com/nodejs/node/pull/50411)
* \[[`a64217c116`](https://github.com/nodejs/node/commit/a64217c116)] - **deps**: upgrade openssl sources to quictls/openssl-3.0.12+quic1 (Node.js GitHub Bot) [#50411](https://github.com/nodejs/node/pull/50411)
* \[[`62515e118c`](https://github.com/nodejs/node/commit/62515e118c)] - **deps**: update llhttp to 9.1.3 (Node.js GitHub Bot) [#50080](https://github.com/nodejs/node/pull/50080)
* \[[`d6f49c7bdc`](https://github.com/nodejs/node/commit/d6f49c7bdc)] - **deps**: update googletest to 116b7e5 (Node.js GitHub Bot) [#50324](https://github.com/nodejs/node/pull/50324)
* \[[`e25c65ee2f`](https://github.com/nodejs/node/commit/e25c65ee2f)] - **doc**: add MrJithil to collaborators (Jithil P Ponnan) [#50666](https://github.com/nodejs/node/pull/50666)
* \[[`8be0efd68f`](https://github.com/nodejs/node/commit/8be0efd68f)] - **doc**: fix typo in fs.md (fwio) [#50570](https://github.com/nodejs/node/pull/50570)
* \[[`a656bf2dee`](https://github.com/nodejs/node/commit/a656bf2dee)] - **doc**: add missing description of argument in `subtle.encrypt` (Deokjin Kim) [#50578](https://github.com/nodejs/node/pull/50578)
* \[[`4cbe44ed6f`](https://github.com/nodejs/node/commit/4cbe44ed6f)] - **doc**: update pm documentation to include resource (Ranieri Innocenti Spada) [#50601](https://github.com/nodejs/node/pull/50601)
* \[[`479c1ea9fe`](https://github.com/nodejs/node/commit/479c1ea9fe)] - **doc**: correct attribution in v20.6.0 changelog (Jacob Smith) [#50564](https://github.com/nodejs/node/pull/50564)
* \[[`1668798902`](https://github.com/nodejs/node/commit/1668798902)] - **doc**: update to align `console.table` row to the left (Jungku Lee) [#50553](https://github.com/nodejs/node/pull/50553)
* \[[`886fc48f87`](https://github.com/nodejs/node/commit/886fc48f87)] - **doc**: underline links (Rich Trott) [#50481](https://github.com/nodejs/node/pull/50481)
* \[[`98cfa3a72b`](https://github.com/nodejs/node/commit/98cfa3a72b)] - **doc**: recommend supported Python versions (Luigi Pinca) [#50407](https://github.com/nodejs/node/pull/50407)
* \[[`921e36ece9`](https://github.com/nodejs/node/commit/921e36ece9)] - **doc**: remove duplicate word (Gerhard Stöbich) [#50475](https://github.com/nodejs/node/pull/50475)
* \[[`43074ee21c`](https://github.com/nodejs/node/commit/43074ee21c)] - **doc**: fix typo in `webstreams.md` (André Santos) [#50426](https://github.com/nodejs/node/pull/50426)
* \[[`0b11bf16e8`](https://github.com/nodejs/node/commit/0b11bf16e8)] - **doc**: update notable changes in v21.1.0 (Joyee Cheung) [#50388](https://github.com/nodejs/node/pull/50388)
* \[[`d62e81229c`](https://github.com/nodejs/node/commit/d62e81229c)] - **doc**: add information about Node-API versions >=9 (Michael Dawson) [#50168](https://github.com/nodejs/node/pull/50168)
* \[[`f2366573f9`](https://github.com/nodejs/node/commit/f2366573f9)] - **doc**: add Ethan-Arrowood as a collaborator (Ethan Arrowood) [#50393](https://github.com/nodejs/node/pull/50393)
* \[[`d9f92bc042`](https://github.com/nodejs/node/commit/d9f92bc042)] - **doc**: fix TOC in `releases.md` (Bryce Seefieldt) [#50372](https://github.com/nodejs/node/pull/50372)
* \[[`14e3675b13`](https://github.com/nodejs/node/commit/14e3675b13)] - **errors**: improve hideStackFrames (Aras Abbasi) [#49990](https://github.com/nodejs/node/pull/49990)
* \[[`09c02ed26b`](https://github.com/nodejs/node/commit/09c02ed26b)] - **esm**: bypass CJS loader in default load under `--default-type=module` (Antoine du Hamel) [#50004](https://github.com/nodejs/node/pull/50004)
* \[[`eac9cc5fcb`](https://github.com/nodejs/node/commit/eac9cc5fcb)] - **(SEMVER-MINOR)** **esm**: add import.meta.dirname and import.meta.filename (James Sumners) [#48740](https://github.com/nodejs/node/pull/48740)
* \[[`44f19ce394`](https://github.com/nodejs/node/commit/44f19ce394)] - **fs**: update param in jsdoc for `readdir` (Jungku Lee) [#50448](https://github.com/nodejs/node/pull/50448)
* \[[`7e151114b1`](https://github.com/nodejs/node/commit/7e151114b1)] - **fs**: add stacktrace to fs/promises (翠 / green) [#49849](https://github.com/nodejs/node/pull/49849)
* \[[`3e7226a12f`](https://github.com/nodejs/node/commit/3e7226a12f)] - **fs**: do not throw error on cpSync internals (Yagiz Nizipli) [#50185](https://github.com/nodejs/node/pull/50185)
* \[[`67cbe1b80f`](https://github.com/nodejs/node/commit/67cbe1b80f)] - **fs,url**: move `FromNamespacedPath` to `node_url` (Yagiz Nizipli) [#50090](https://github.com/nodejs/node/pull/50090)
* \[[`b4db32e9cb`](https://github.com/nodejs/node/commit/b4db32e9cb)] - **fs,url**: refactor `FileURLToPath` method (Yagiz Nizipli) [#50090](https://github.com/nodejs/node/pull/50090)
* \[[`4345ee2ede`](https://github.com/nodejs/node/commit/4345ee2ede)] - **fs,url**: move `FileURLToPath` to node\_url (Yagiz Nizipli) [#50090](https://github.com/nodejs/node/pull/50090)
* \[[`ed293fc520`](https://github.com/nodejs/node/commit/ed293fc520)] - **lib**: remove deprecated string methods (Jithil P Ponnan) [#50592](https://github.com/nodejs/node/pull/50592)
* \[[`363bc46b92`](https://github.com/nodejs/node/commit/363bc46b92)] - **lib**: fix assert shows diff messages in ESM and CJS (Jithil P Ponnan) [#50634](https://github.com/nodejs/node/pull/50634)
* \[[`5fa40bea9e`](https://github.com/nodejs/node/commit/5fa40bea9e)] - **lib**: make event static properties non writable and configurable (Muthukumar) [#50425](https://github.com/nodejs/node/pull/50425)
* \[[`6dbb280733`](https://github.com/nodejs/node/commit/6dbb280733)] - **(SEMVER-MINOR)** **lib**: add `--no-experimental-global-navigator` CLI flag (Antoine du Hamel) [#50562](https://github.com/nodejs/node/pull/50562)
* \[[`03c730b931`](https://github.com/nodejs/node/commit/03c730b931)] - **(SEMVER-MINOR)** **lib**: add navigator.language & navigator.languages (Aras Abbasi) [#50303](https://github.com/nodejs/node/pull/50303)
* \[[`f932f4c518`](https://github.com/nodejs/node/commit/f932f4c518)] - **(SEMVER-MINOR)** **lib**: add navigator.platform (Aras Abbasi) [#50385](https://github.com/nodejs/node/pull/50385)
* \[[`c9bd0c5000`](https://github.com/nodejs/node/commit/c9bd0c5000)] - **lib**: use primordials for navigator.userAgent (Aras Abbasi) [#50467](https://github.com/nodejs/node/pull/50467)
* \[[`6dabe7cf60`](https://github.com/nodejs/node/commit/6dabe7cf60)] - **lib**: avoid memory allocation on nodeprecation flag (Vinicius Lourenço) [#50231](https://github.com/nodejs/node/pull/50231)
* \[[`3615a61ac8`](https://github.com/nodejs/node/commit/3615a61ac8)] - **lib**: align console.table row to the left (Jithil P Ponnan) [#50135](https://github.com/nodejs/node/pull/50135)
* \[[`9e7131ffda`](https://github.com/nodejs/node/commit/9e7131ffda)] - **meta**: add web-standards as WPTs owner (Filip Skokan) [#50636](https://github.com/nodejs/node/pull/50636)
* \[[`dedfb5ab26`](https://github.com/nodejs/node/commit/dedfb5ab26)] - **meta**: bump github/codeql-action from 2.21.9 to 2.22.5 (dependabot\[bot]) [#50513](https://github.com/nodejs/node/pull/50513)
* \[[`4e83036d89`](https://github.com/nodejs/node/commit/4e83036d89)] - **meta**: bump step-security/harden-runner from 2.5.1 to 2.6.0 (dependabot\[bot]) [#50512](https://github.com/nodejs/node/pull/50512)
* \[[`4bf9cffa95`](https://github.com/nodejs/node/commit/4bf9cffa95)] - **meta**: bump ossf/scorecard-action from 2.2.0 to 2.3.1 (dependabot\[bot]) [#50509](https://github.com/nodejs/node/pull/50509)
* \[[`49cce7634b`](https://github.com/nodejs/node/commit/49cce7634b)] - **meta**: fix spacing in collaborator list (Antoine du Hamel) [#50641](https://github.com/nodejs/node/pull/50641)
* \[[`12e54e360c`](https://github.com/nodejs/node/commit/12e54e360c)] - **meta**: bump actions/setup-python from 4.7.0 to 4.7.1 (dependabot\[bot]) [#50510](https://github.com/nodejs/node/pull/50510)
* \[[`85a527e6e0`](https://github.com/nodejs/node/commit/85a527e6e0)] - **meta**: add crypto as crypto and webcrypto docs owner (Filip Skokan) [#50579](https://github.com/nodejs/node/pull/50579)
* \[[`ff9b3bdf34`](https://github.com/nodejs/node/commit/ff9b3bdf34)] - **meta**: bump actions/setup-node from 3.8.1 to 4.0.0 (dependabot\[bot]) [#50514](https://github.com/nodejs/node/pull/50514)
* \[[`840303078f`](https://github.com/nodejs/node/commit/840303078f)] - **meta**: bump actions/checkout from 4.1.0 to 4.1.1 (dependabot\[bot]) [#50511](https://github.com/nodejs/node/pull/50511)
* \[[`c9e6e4e739`](https://github.com/nodejs/node/commit/c9e6e4e739)] - **meta**: add <ethan.arrowood@vercel.com> to mailmap (Ethan Arrowood) [#50491](https://github.com/nodejs/node/pull/50491)
* \[[`d94010b745`](https://github.com/nodejs/node/commit/d94010b745)] - **meta**: add web-standards as web api visibility owner (Chengzhong Wu) [#50418](https://github.com/nodejs/node/pull/50418)
* \[[`e008336b17`](https://github.com/nodejs/node/commit/e008336b17)] - **meta**: mention other notable changes section (Rafael Gonzaga) [#50309](https://github.com/nodejs/node/pull/50309)
* \[[`3606a0a848`](https://github.com/nodejs/node/commit/3606a0a848)] - **module**: execute `--import` sequentially (Antoine du Hamel) [#50474](https://github.com/nodejs/node/pull/50474)
* \[[`667d245e75`](https://github.com/nodejs/node/commit/667d245e75)] - **module**: add application/json in accept header when fetching json module (Marco Ippolito) [#50119](https://github.com/nodejs/node/pull/50119)
* \[[`905ca00cbc`](https://github.com/nodejs/node/commit/905ca00cbc)] - **perf\_hooks**: reduce overhead of createHistogram (Vinícius Lourenço) [#50074](https://github.com/nodejs/node/pull/50074)
* \[[`7c35055c8e`](https://github.com/nodejs/node/commit/7c35055c8e)] - **permission**: address coverity warning (Michael Dawson) [#50215](https://github.com/nodejs/node/pull/50215)
* \[[`b740324f7c`](https://github.com/nodejs/node/commit/b740324f7c)] - **src**: use v8::Isolate::TryGetCurrent() in DumpJavaScriptBacktrace() (Joyee Cheung) [#50518](https://github.com/nodejs/node/pull/50518)
* \[[`6e20e083dd`](https://github.com/nodejs/node/commit/6e20e083dd)] - **src**: print more information in C++ assertions (Joyee Cheung) [#50242](https://github.com/nodejs/node/pull/50242)
* \[[`9f55dfc266`](https://github.com/nodejs/node/commit/9f55dfc266)] - **src**: hide node::credentials::HasOnly outside unit (Tobias Nießen) [#50450](https://github.com/nodejs/node/pull/50450)
* \[[`4eb74a2c24`](https://github.com/nodejs/node/commit/4eb74a2c24)] - **src**: readiterable entries may be empty (Matthew Aitken) [#50398](https://github.com/nodejs/node/pull/50398)
* \[[`5b453d45d6`](https://github.com/nodejs/node/commit/5b453d45d6)] - **src**: implement structuredClone in native (Joyee Cheung) [#50330](https://github.com/nodejs/node/pull/50330)
* \[[`f1d79b3cbb`](https://github.com/nodejs/node/commit/f1d79b3cbb)] - **src**: use find instead of char-by-char in FromFilePath() (Daniel Lemire) [#50288](https://github.com/nodejs/node/pull/50288)
* \[[`541bdf1e92`](https://github.com/nodejs/node/commit/541bdf1e92)] - **src**: add commit hash shorthand in zlib version (Jithil P Ponnan) [#50158](https://github.com/nodejs/node/pull/50158)
* \[[`91f37d1dc3`](https://github.com/nodejs/node/commit/91f37d1dc3)] - **(SEMVER-MINOR)** **stream**: add support for `deflate-raw` format to webstreams compression (Damian Krzeminski) [#50097](https://github.com/nodejs/node/pull/50097)
* \[[`360f5d9088`](https://github.com/nodejs/node/commit/360f5d9088)] - **stream**: fix Writable.destroy performance regression (Robert Nagy) [#50478](https://github.com/nodejs/node/pull/50478)
* \[[`0116ae7601`](https://github.com/nodejs/node/commit/0116ae7601)] - **stream**: pre-allocate \_events (Robert Nagy) [#50428](https://github.com/nodejs/node/pull/50428)
* \[[`2c0d88e83e`](https://github.com/nodejs/node/commit/2c0d88e83e)] - **stream**: remove no longer relevant comment (Robert Nagy) [#50446](https://github.com/nodejs/node/pull/50446)
* \[[`03c4ff760d`](https://github.com/nodejs/node/commit/03c4ff760d)] - **stream**: use bit fields for construct/destroy (Robert Nagy) [#50408](https://github.com/nodejs/node/pull/50408)
* \[[`e20b272d46`](https://github.com/nodejs/node/commit/e20b272d46)] - **stream**: improve from perf (Raz Luvaton) [#50359](https://github.com/nodejs/node/pull/50359)
* \[[`893024cb7c`](https://github.com/nodejs/node/commit/893024cb7c)] - **stream**: avoid calls to listenerCount (Robert Nagy) [#50357](https://github.com/nodejs/node/pull/50357)
* \[[`586ec48e5f`](https://github.com/nodejs/node/commit/586ec48e5f)] - **stream**: readable use bitmap accessors (Robert Nagy) [#50350](https://github.com/nodejs/node/pull/50350)
* \[[`65850a67c7`](https://github.com/nodejs/node/commit/65850a67c7)] - **stream**: use Array for Readable buffer (Robert Nagy) [#50341](https://github.com/nodejs/node/pull/50341)
* \[[`e433fa54b7`](https://github.com/nodejs/node/commit/e433fa54b7)] - **stream**: optimize creation (Robert Nagy) [#50337](https://github.com/nodejs/node/pull/50337)
* \[[`f56ae67c7b`](https://github.com/nodejs/node/commit/f56ae67c7b)] - **stream**: refactor writable \_write (Robert Nagy) [#50198](https://github.com/nodejs/node/pull/50198)
* \[[`766bd9c8cc`](https://github.com/nodejs/node/commit/766bd9c8cc)] - **stream**: avoid getter for defaultEncoding (Robert Nagy) [#50203](https://github.com/nodejs/node/pull/50203)
* \[[`8be718a0bd`](https://github.com/nodejs/node/commit/8be718a0bd)] - **test**: use destructuring for accessing setting values (Honza Jedlička) [#50609](https://github.com/nodejs/node/pull/50609)
* \[[`b701567a46`](https://github.com/nodejs/node/commit/b701567a46)] - **test**: replace forEach() with for .. of (Evgenia Blajer) [#50605](https://github.com/nodejs/node/pull/50605)
* \[[`e978fd4375`](https://github.com/nodejs/node/commit/e978fd4375)] - **test**: replace forEach() with for ... of in test-readline-keys.js (William Liang) [#50604](https://github.com/nodejs/node/pull/50604)
* \[[`bc92be4ca9`](https://github.com/nodejs/node/commit/bc92be4ca9)] - **test**: replace forEach() with for ... of in test-http2-single-headers.js (spiritualized) [#50606](https://github.com/nodejs/node/pull/50606)
* \[[`864cd32003`](https://github.com/nodejs/node/commit/864cd32003)] - **test**: replace forEach with for of (john-mcinall) [#50602](https://github.com/nodejs/node/pull/50602)
* \[[`2fdcf5c3da`](https://github.com/nodejs/node/commit/2fdcf5c3da)] - **test**: remove unused file (James Sumners) [#50528](https://github.com/nodejs/node/pull/50528)
* \[[`2eeda3f09b`](https://github.com/nodejs/node/commit/2eeda3f09b)] - **test**: replace forEach with for of (Kevin Kühnemund) [#50597](https://github.com/nodejs/node/pull/50597)
* \[[`1d52a57cba`](https://github.com/nodejs/node/commit/1d52a57cba)] - **test**: replace forEach with for of (CorrWu) [#49785](https://github.com/nodejs/node/pull/49785)
* \[[`52b517f4ec`](https://github.com/nodejs/node/commit/52b517f4ec)] - **test**: replace forEach with for \[...] of (Gabriel Bota) [#50615](https://github.com/nodejs/node/pull/50615)
* \[[`931e1e756a`](https://github.com/nodejs/node/commit/931e1e756a)] - **test**: relax version check with shared OpenSSL (Luigi Pinca) [#50505](https://github.com/nodejs/node/pull/50505)
* \[[`6ed8fbf612`](https://github.com/nodejs/node/commit/6ed8fbf612)] - **test**: add WPT report test duration (Filip Skokan) [#50574](https://github.com/nodejs/node/pull/50574)
* \[[`7c7be517b4`](https://github.com/nodejs/node/commit/7c7be517b4)] - **test**: replace forEach() with for ... of loop in test-global.js (Kajol) [#49772](https://github.com/nodejs/node/pull/49772)
* \[[`de46a346ab`](https://github.com/nodejs/node/commit/de46a346ab)] - **test**: skip test-diagnostics-channel-memory-leak.js (Joyee Cheung) [#50327](https://github.com/nodejs/node/pull/50327)
* \[[`8487cac24c`](https://github.com/nodejs/node/commit/8487cac24c)] - **test**: improve `UV_THREADPOOL_SIZE` tests on `.env` (Yagiz Nizipli) [#49213](https://github.com/nodejs/node/pull/49213)
* \[[`ee751102a4`](https://github.com/nodejs/node/commit/ee751102a4)] - **test**: recognize wpt completion error (Chengzhong Wu) [#50429](https://github.com/nodejs/node/pull/50429)
* \[[`7e3eb02252`](https://github.com/nodejs/node/commit/7e3eb02252)] - **test**: report error wpt test results (Chengzhong Wu) [#50429](https://github.com/nodejs/node/pull/50429)
* \[[`90833a89a9`](https://github.com/nodejs/node/commit/90833a89a9)] - **test**: replace forEach() with for...of (Ram) [#49794](https://github.com/nodejs/node/pull/49794)
* \[[`f40435d143`](https://github.com/nodejs/node/commit/f40435d143)] - **test**: replace forEach() with for...of in test-trace-events-http (Chand) [#49795](https://github.com/nodejs/node/pull/49795)
* \[[`f70a2dd70d`](https://github.com/nodejs/node/commit/f70a2dd70d)] - **test**: fix testsuite against zlib version 1.3 (Dominique Leuenberger) [#50364](https://github.com/nodejs/node/pull/50364)
* \[[`d24de129a7`](https://github.com/nodejs/node/commit/d24de129a7)] - **test**: replace forEach with for...of in test-fs-realpath-buffer-encoding (Niya Shiyas) [#49804](https://github.com/nodejs/node/pull/49804)
* \[[`2b6d283265`](https://github.com/nodejs/node/commit/2b6d283265)] - **test**: fix timeout of test-cpu-prof-dir-worker.js in LoongArch devices (Shi Pujin) [#50363](https://github.com/nodejs/node/pull/50363)
* \[[`bd5b61fa6c`](https://github.com/nodejs/node/commit/bd5b61fa6c)] - **test**: fix crypto-dh error message for OpenSSL 3.x (Kerem Kat) [#50395](https://github.com/nodejs/node/pull/50395)
* \[[`aa86c78a9c`](https://github.com/nodejs/node/commit/aa86c78a9c)] - **test**: fix vm assertion actual and expected order (Chengzhong Wu) [#50371](https://github.com/nodejs/node/pull/50371)
* \[[`ab9cad8107`](https://github.com/nodejs/node/commit/ab9cad8107)] - **test**: v8: Add test-linux-perf-logger test suite (Luke Albao) [#50352](https://github.com/nodejs/node/pull/50352)
* \[[`31cd05c39f`](https://github.com/nodejs/node/commit/31cd05c39f)] - **test**: ensure never settling promises are detected (Antoine du Hamel) [#50318](https://github.com/nodejs/node/pull/50318)
* \[[`ad316419dd`](https://github.com/nodejs/node/commit/ad316419dd)] - **test**: avoid v8 deadcode on performance function (Vinícius Lourenço) [#50074](https://github.com/nodejs/node/pull/50074)
* \[[`01bed64cbb`](https://github.com/nodejs/node/commit/01bed64cbb)] - **test\_runner**: pass abortSignal to test files (Moshe Atlow) [#50630](https://github.com/nodejs/node/pull/50630)
* \[[`ae4a7ba991`](https://github.com/nodejs/node/commit/ae4a7ba991)] - **test\_runner**: replace forEach with for of (Tom Haddad) [#50595](https://github.com/nodejs/node/pull/50595)
* \[[`913e4b9173`](https://github.com/nodejs/node/commit/913e4b9173)] - **test\_runner**: output errors of suites (Moshe Atlow) [#50361](https://github.com/nodejs/node/pull/50361)
* \[[`c9b92bba58`](https://github.com/nodejs/node/commit/c9b92bba58)] - **(SEMVER-MINOR)** **test\_runner**: adds built in lcov reporter (Phil Nash) [#50018](https://github.com/nodejs/node/pull/50018)
* \[[`e2c3b015cd`](https://github.com/nodejs/node/commit/e2c3b015cd)] - **test\_runner**: test return value of mocked promisified timers (Mika Fischer) [#50331](https://github.com/nodejs/node/pull/50331)
* \[[`f6c496563e`](https://github.com/nodejs/node/commit/f6c496563e)] - **(SEMVER-MINOR)** **test\_runner**: add Date to the supported mock APIs (Lucas Santos) [#48638](https://github.com/nodejs/node/pull/48638)
* \[[`05e8b6ef20`](https://github.com/nodejs/node/commit/05e8b6ef20)] - **(SEMVER-MINOR)** **test\_runner, cli**: add --test-timeout flag (Shubham Pandey) [#50443](https://github.com/nodejs/node/pull/50443)
* \[[`b71c8c447e`](https://github.com/nodejs/node/commit/b71c8c447e)] - **tls**: use `validateFunction` for `options.SNICallback` (Deokjin Kim) [#50530](https://github.com/nodejs/node/pull/50530)
* \[[`5fcd67a8ea`](https://github.com/nodejs/node/commit/5fcd67a8ea)] - **tools**: add macOS notarization stapler (Ulises Gascón) [#50625](https://github.com/nodejs/node/pull/50625)
* \[[`253e206fe9`](https://github.com/nodejs/node/commit/253e206fe9)] - **tools**: update eslint to 8.53.0 (Node.js GitHub Bot) [#50559](https://github.com/nodejs/node/pull/50559)
* \[[`f5e1c95447`](https://github.com/nodejs/node/commit/f5e1c95447)] - **tools**: update lint-md-dependencies to rollup\@4.3.0 (Node.js GitHub Bot) [#50556](https://github.com/nodejs/node/pull/50556)
* \[[`257e22073e`](https://github.com/nodejs/node/commit/257e22073e)] - **tools**: compare ICU checksums before file changes (Michaël Zasso) [#50522](https://github.com/nodejs/node/pull/50522)
* \[[`aa8feea5f1`](https://github.com/nodejs/node/commit/aa8feea5f1)] - **tools**: improve update acorn-walk script (Marco Ippolito) [#50473](https://github.com/nodejs/node/pull/50473)
* \[[`c0206bf44c`](https://github.com/nodejs/node/commit/c0206bf44c)] - **tools**: update lint-md-dependencies to rollup\@4.2.0 (Node.js GitHub Bot) [#50496](https://github.com/nodejs/node/pull/50496)
* \[[`02dec645f3`](https://github.com/nodejs/node/commit/02dec645f3)] - **tools**: improve macOS notarization process output readability (Ulises Gascón) [#50389](https://github.com/nodejs/node/pull/50389)
* \[[`52e7b6d29a`](https://github.com/nodejs/node/commit/52e7b6d29a)] - **tools**: update gyp-next to v0.16.1 (Michaël Zasso) [#50380](https://github.com/nodejs/node/pull/50380)
* \[[`9fc29c909b`](https://github.com/nodejs/node/commit/9fc29c909b)] - **tools**: skip ruff on tools/gyp (Michaël Zasso) [#50380](https://github.com/nodejs/node/pull/50380)
* \[[`ec7005abff`](https://github.com/nodejs/node/commit/ec7005abff)] - **tools**: update lint-md-dependencies to rollup\@4.1.5 unified\@11.0.4 (Node.js GitHub Bot) [#50461](https://github.com/nodejs/node/pull/50461)
* \[[`aed590035f`](https://github.com/nodejs/node/commit/aed590035f)] - **tools**: remove unused `version` function (Ulises Gascón) [#50390](https://github.com/nodejs/node/pull/50390)
* \[[`f7590481f2`](https://github.com/nodejs/node/commit/f7590481f2)] - **tools**: avoid npm install in deps installation (Marco Ippolito) [#50413](https://github.com/nodejs/node/pull/50413)
* \[[`92d64035c6`](https://github.com/nodejs/node/commit/92d64035c6)] - _**Revert**_ "**tools**: update doc dependencies" (Richard Lau) [#50414](https://github.com/nodejs/node/pull/50414)
* \[[`90c9dd3e0e`](https://github.com/nodejs/node/commit/90c9dd3e0e)] - **tools**: update doc dependencies (Node.js GitHub Bot) [#49988](https://github.com/nodejs/node/pull/49988)
* \[[`f210915681`](https://github.com/nodejs/node/commit/f210915681)] - **tools**: run coverage CI only on relevant files (Antoine du Hamel) [#50349](https://github.com/nodejs/node/pull/50349)
* \[[`5ccdda4004`](https://github.com/nodejs/node/commit/5ccdda4004)] - **tools**: update eslint to 8.52.0 (Node.js GitHub Bot) [#50326](https://github.com/nodejs/node/pull/50326)
* \[[`bd4634874c`](https://github.com/nodejs/node/commit/bd4634874c)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#50190](https://github.com/nodejs/node/pull/50190)
* \[[`773cfa59bb`](https://github.com/nodejs/node/commit/773cfa59bb)] - **vm**: allow dynamic import with a referrer realm (Chengzhong Wu) [#50360](https://github.com/nodejs/node/pull/50360)
* \[[`2f86d50e70`](https://github.com/nodejs/node/commit/2f86d50e70)] - **wasi**: document security sandboxing status (Guy Bedford) [#50396](https://github.com/nodejs/node/pull/50396)

<a id="21.1.0"></a>

## 2023-10-24, Version 21.1.0 (Current), @targos

### Notable Changes

#### Automatically detect and run ESM syntax

The new flag `--experimental-detect-module` can be used to automatically run
ES modules when their syntax can be detected. For “ambiguous” files, which are
`.js` or extensionless files with no `package.json` with a `type` field, Node.js
will parse the file to detect ES module syntax; if found, it will run the file
as an ES module, otherwise it will run the file as a CommonJS module.
The same applies to string input via `--eval` or `STDIN`.

We hope to make detection enabled by default in a future version of Node.js.
Detection increases startup time, so we encourage everyone — especially package
authors — to add a `type` field to `package.json`, even for the default
`"type": "commonjs"`. The presence of a `type` field, or explicit extensions
such as `.mjs` or `.cjs`, will opt out of detection.

Contributed by Geoffrey Booth in [#50096](https://github.com/nodejs/node/pull/50096).

### vm: fix V8 compilation cache support for vm.Script

Previously repeated compilation of the same source code using `vm.Script`
stopped hitting the V8 compilation cache after v16.x when support for
`importModuleDynamically` was added to `vm.Script`, resulting in a performance
regression that blocked users (in particular Jest users) from upgrading from
v16.x.

The recent fixes landed in v21.1.0 allow the compilation cache to be hit again
for `vm.Script` when `--experimental-vm-modules` is not used even in the
presence of the `importModuleDynamically` option, so that users affected by the
performance regression can now upgrade. Ongoing work is also being done to
enable compilation cache support for `vm.CompileFunction`.

Contributed by Joyee Cheung in [#50137](https://github.com/nodejs/node/pull/50137).

#### Other Notable Changes

* \[[`3729e33358`](https://github.com/nodejs/node/commit/3729e33358)] - **doc**: add H4ad to collaborators (Vinícius Lourenço) [#50217](https://github.com/nodejs/node/pull/50217)
* \[[`18862e4d5d`](https://github.com/nodejs/node/commit/18862e4d5d)] - **(SEMVER-MINOR)** **fs**: add `flush` option to `appendFile()` functions (Colin Ihrig) [#50095](https://github.com/nodejs/node/pull/50095)
* \[[`5a52c518ef`](https://github.com/nodejs/node/commit/5a52c518ef)] - **(SEMVER-MINOR)** **lib**: add `navigator.userAgent` (Yagiz Nizipli) [#50200](https://github.com/nodejs/node/pull/50200)
* \[[`789372a072`](https://github.com/nodejs/node/commit/789372a072)] - **(SEMVER-MINOR)** **stream**: allow pass stream class to `stream.compose` (Alex Yang) [#50187](https://github.com/nodejs/node/pull/50187)
* \[[`f3a9ea0bc4`](https://github.com/nodejs/node/commit/f3a9ea0bc4)] - **stream**: improve performance of readable stream reads (Raz Luvaton) [#50173](https://github.com/nodejs/node/pull/50173)

### Commits

* \[[`9cd68b9083`](https://github.com/nodejs/node/commit/9cd68b9083)] - **buffer**: remove unnecessary assignment in fromString (Tobias Nießen) [#50199](https://github.com/nodejs/node/pull/50199)
* \[[`a362c276ec`](https://github.com/nodejs/node/commit/a362c276ec)] - **crypto**: ensure valid point on elliptic curve in SubtleCrypto.importKey (Filip Skokan) [#50234](https://github.com/nodejs/node/pull/50234)
* \[[`f4da308f8d`](https://github.com/nodejs/node/commit/f4da308f8d)] - **deps**: V8: cherry-pick f7d000a7ae7b (Luke Albao) [#50302](https://github.com/nodejs/node/pull/50302)
* \[[`269e268c38`](https://github.com/nodejs/node/commit/269e268c38)] - **deps**: update ada to 2.7.2 (Node.js GitHub Bot) [#50338](https://github.com/nodejs/node/pull/50338)
* \[[`03a31ce41e`](https://github.com/nodejs/node/commit/03a31ce41e)] - **deps**: update corepack to 0.22.0 (Node.js GitHub Bot) [#50325](https://github.com/nodejs/node/pull/50325)
* \[[`000531781b`](https://github.com/nodejs/node/commit/000531781b)] - **deps**: update undici to 5.26.4 (Node.js GitHub Bot) [#50274](https://github.com/nodejs/node/pull/50274)
* \[[`f050668c14`](https://github.com/nodejs/node/commit/f050668c14)] - **deps**: update c-ares to 1.20.1 (Node.js GitHub Bot) [#50082](https://github.com/nodejs/node/pull/50082)
* \[[`ba258b682b`](https://github.com/nodejs/node/commit/ba258b682b)] - **deps**: update c-ares to 1.20.0 (Node.js GitHub Bot) [#50082](https://github.com/nodejs/node/pull/50082)
* \[[`571f7ef1fa`](https://github.com/nodejs/node/commit/571f7ef1fa)] - **deps**: patch V8 to 11.8.172.15 (Michaël Zasso) [#50114](https://github.com/nodejs/node/pull/50114)
* \[[`943047e800`](https://github.com/nodejs/node/commit/943047e800)] - **deps**: V8: cherry-pick 25902244ad1a (Joyee Cheung) [#50156](https://github.com/nodejs/node/pull/50156)
* \[[`db2a1cf1cb`](https://github.com/nodejs/node/commit/db2a1cf1cb)] - **doc**: fix `navigator.hardwareConcurrency` example (Tobias Nießen) [#50278](https://github.com/nodejs/node/pull/50278)
* \[[`6e537aeb44`](https://github.com/nodejs/node/commit/6e537aeb44)] - **doc**: explain how to disable navigator (Geoffrey Booth) [#50310](https://github.com/nodejs/node/pull/50310)
* \[[`c40de82d62`](https://github.com/nodejs/node/commit/c40de82d62)] - **doc**: add loong64 info into platform list (Shi Pujin) [#50086](https://github.com/nodejs/node/pull/50086)
* \[[`1c21a1880b`](https://github.com/nodejs/node/commit/1c21a1880b)] - **doc**: update release process LTS step (Richard Lau) [#50299](https://github.com/nodejs/node/pull/50299)
* \[[`2473aa3672`](https://github.com/nodejs/node/commit/2473aa3672)] - **doc**: fix release process table of contents (Richard Lau) [#50216](https://github.com/nodejs/node/pull/50216)
* \[[`ce9d84eae3`](https://github.com/nodejs/node/commit/ce9d84eae3)] - **doc**: update api `stream.compose` (Alex Yang) [#50206](https://github.com/nodejs/node/pull/50206)
* \[[`dacee4d9b5`](https://github.com/nodejs/node/commit/dacee4d9b5)] - **doc**: add ReflectConstruct to known perf issues (Vinicius Lourenço) [#50111](https://github.com/nodejs/node/pull/50111)
* \[[`82363be2ac`](https://github.com/nodejs/node/commit/82363be2ac)] - **doc**: fix typo in dgram docs (Peter Johnson) [#50211](https://github.com/nodejs/node/pull/50211)
* \[[`8c1a46c751`](https://github.com/nodejs/node/commit/8c1a46c751)] - **doc**: fix H4ad collaborator sort (Vinicius Lourenço) [#50218](https://github.com/nodejs/node/pull/50218)
* \[[`3729e33358`](https://github.com/nodejs/node/commit/3729e33358)] - **doc**: add H4ad to collaborators (Vinícius Lourenço) [#50217](https://github.com/nodejs/node/pull/50217)
* \[[`bac872cbd0`](https://github.com/nodejs/node/commit/bac872cbd0)] - **doc**: update release-stewards with last sec-release (Rafael Gonzaga) [#50179](https://github.com/nodejs/node/pull/50179)
* \[[`06b7724f14`](https://github.com/nodejs/node/commit/06b7724f14)] - **doc**: add command to keep major branch sync (Rafael Gonzaga) [#50102](https://github.com/nodejs/node/pull/50102)
* \[[`47633ab086`](https://github.com/nodejs/node/commit/47633ab086)] - **doc**: add loong64 to list of architectures (Shi Pujin) [#50172](https://github.com/nodejs/node/pull/50172)
* \[[`1f40ca1b91`](https://github.com/nodejs/node/commit/1f40ca1b91)] - **doc**: update security release process (Michael Dawson) [#50166](https://github.com/nodejs/node/pull/50166)
* \[[`998feda118`](https://github.com/nodejs/node/commit/998feda118)] - **esm**: do not give wrong hints when detecting file format (Antoine du Hamel) [#50314](https://github.com/nodejs/node/pull/50314)
* \[[`e375063e01`](https://github.com/nodejs/node/commit/e375063e01)] - **(SEMVER-MINOR)** **esm**: detect ESM syntax in ambiguous JavaScript (Geoffrey Booth) [#50096](https://github.com/nodejs/node/pull/50096)
* \[[`c76eb27971`](https://github.com/nodejs/node/commit/c76eb27971)] - **esm**: improve check for ESM syntax (Geoffrey Booth) [#50127](https://github.com/nodejs/node/pull/50127)
* \[[`7740bf820c`](https://github.com/nodejs/node/commit/7740bf820c)] - **esm**: rename error code related to import attributes (Antoine du Hamel) [#50181](https://github.com/nodejs/node/pull/50181)
* \[[`0cc176ef25`](https://github.com/nodejs/node/commit/0cc176ef25)] - **fs**: improve error performance for `readSync` (Jungku Lee) [#50033](https://github.com/nodejs/node/pull/50033)
* \[[`5942edb774`](https://github.com/nodejs/node/commit/5942edb774)] - **fs**: improve error performance for `fsyncSync` (Jungku Lee) [#49880](https://github.com/nodejs/node/pull/49880)
* \[[`6ec5abadc0`](https://github.com/nodejs/node/commit/6ec5abadc0)] - **fs**: improve error performance for `mkdirSync` (CanadaHonk) [#49847](https://github.com/nodejs/node/pull/49847)
* \[[`c5ff000cb1`](https://github.com/nodejs/node/commit/c5ff000cb1)] - **fs**: improve error performance of `realpathSync` (Yagiz Nizipli) [#49962](https://github.com/nodejs/node/pull/49962)
* \[[`6eeaa02f5c`](https://github.com/nodejs/node/commit/6eeaa02f5c)] - **fs**: improve error performance of `lchownSync` (Yagiz Nizipli) [#49962](https://github.com/nodejs/node/pull/49962)
* \[[`dc9ac8d41c`](https://github.com/nodejs/node/commit/dc9ac8d41c)] - **fs**: improve error performance of `symlinkSync` (Yagiz Nizipli) [#49962](https://github.com/nodejs/node/pull/49962)
* \[[`bc6f279261`](https://github.com/nodejs/node/commit/bc6f279261)] - **fs**: improve error performance of `readlinkSync` (Yagiz Nizipli) [#49962](https://github.com/nodejs/node/pull/49962)
* \[[`275987841e`](https://github.com/nodejs/node/commit/275987841e)] - **fs**: improve error performance of `mkdtempSync` (Yagiz Nizipli) [#49962](https://github.com/nodejs/node/pull/49962)
* \[[`81f15274e2`](https://github.com/nodejs/node/commit/81f15274e2)] - **fs**: improve error performance of `linkSync` (Yagiz Nizipli) [#49962](https://github.com/nodejs/node/pull/49962)
* \[[`f766c04856`](https://github.com/nodejs/node/commit/f766c04856)] - **fs**: improve error performance of `chownSync` (Yagiz Nizipli) [#49962](https://github.com/nodejs/node/pull/49962)
* \[[`610036c67d`](https://github.com/nodejs/node/commit/610036c67d)] - **fs**: improve error performance of `renameSync` (Yagiz Nizipli) [#49962](https://github.com/nodejs/node/pull/49962)
* \[[`18862e4d5d`](https://github.com/nodejs/node/commit/18862e4d5d)] - **(SEMVER-MINOR)** **fs**: add flush option to appendFile() functions (Colin Ihrig) [#50095](https://github.com/nodejs/node/pull/50095)
* \[[`3f8cbb15cb`](https://github.com/nodejs/node/commit/3f8cbb15cb)] - **http2**: allow streams to complete gracefully after goaway (Michael Lumish) [#50202](https://github.com/nodejs/node/pull/50202)
* \[[`1464eba1a0`](https://github.com/nodejs/node/commit/1464eba1a0)] - **lib**: improve performance of validateStringArray and validateBooleanArray (Aras Abbasi) [#49756](https://github.com/nodejs/node/pull/49756)
* \[[`5a52c518ef`](https://github.com/nodejs/node/commit/5a52c518ef)] - **(SEMVER-MINOR)** **lib**: add `navigator.userAgent` (Yagiz Nizipli) [#50200](https://github.com/nodejs/node/pull/50200)
* \[[`b6021ab8f6`](https://github.com/nodejs/node/commit/b6021ab8f6)] - **lib**: reduce overhead of blob clone (Vinicius Lourenço) [#50110](https://github.com/nodejs/node/pull/50110)
* \[[`be19d9baa1`](https://github.com/nodejs/node/commit/be19d9baa1)] - **meta**: move Trott to TSC regular member (Rich Trott) [#50297](https://github.com/nodejs/node/pull/50297)
* \[[`91e373f8e9`](https://github.com/nodejs/node/commit/91e373f8e9)] - **node-api**: return napi\_exception\_pending on proxy handlers (Chengzhong Wu) [#48607](https://github.com/nodejs/node/pull/48607)
* \[[`531a3ae4b5`](https://github.com/nodejs/node/commit/531a3ae4b5)] - **stream**: simplify prefinish (Robert Nagy) [#50204](https://github.com/nodejs/node/pull/50204)
* \[[`514ac86579`](https://github.com/nodejs/node/commit/514ac86579)] - **stream**: reduce scope of readable bitmap details (Robert Nagy) [#49963](https://github.com/nodejs/node/pull/49963)
* \[[`789372a072`](https://github.com/nodejs/node/commit/789372a072)] - **(SEMVER-MINOR)** **stream**: allow pass stream class to `stream.compose` (Alex Yang) [#50187](https://github.com/nodejs/node/pull/50187)
* \[[`f3a9ea0bc4`](https://github.com/nodejs/node/commit/f3a9ea0bc4)] - **stream**: call helper function from push and unshift (Raz Luvaton) [#50173](https://github.com/nodejs/node/pull/50173)
* \[[`a9ca7b32e7`](https://github.com/nodejs/node/commit/a9ca7b32e7)] - **test**: improve watch mode test (Moshe Atlow) [#50319](https://github.com/nodejs/node/pull/50319)
* \[[`63b7059efd`](https://github.com/nodejs/node/commit/63b7059efd)] - **test**: set `test-watch-mode-inspect` as flaky (Yagiz Nizipli) [#50259](https://github.com/nodejs/node/pull/50259)
* \[[`7f87084b05`](https://github.com/nodejs/node/commit/7f87084b05)] - _**Revert**_ "**test**: set `test-esm-loader-resolve-type` as flaky" (Antoine du Hamel) [#50315](https://github.com/nodejs/node/pull/50315)
* \[[`4d390e2de4`](https://github.com/nodejs/node/commit/4d390e2de4)] - **test**: replace forEach with for..of in test-http-perf\_hooks.js (Niya Shiyas) [#49818](https://github.com/nodejs/node/pull/49818)
* \[[`67c599ec39`](https://github.com/nodejs/node/commit/67c599ec39)] - **test**: replace forEach with for..of in test-net-isipv4.js (Niya Shiyas) [#49822](https://github.com/nodejs/node/pull/49822)
* \[[`19d3ce2494`](https://github.com/nodejs/node/commit/19d3ce2494)] - **test**: deflake `test-esm-loader-resolve-type` (Antoine du Hamel) [#50273](https://github.com/nodejs/node/pull/50273)
* \[[`2d8d6c5701`](https://github.com/nodejs/node/commit/2d8d6c5701)] - **test**: replace forEach with for..of in test-http2-server (Niya Shiyas) [#49819](https://github.com/nodejs/node/pull/49819)
* \[[`af31d51e5a`](https://github.com/nodejs/node/commit/af31d51e5a)] - **test**: replace forEach with for..of in test-http2-client-destroy.js (Niya Shiyas) [#49820](https://github.com/nodejs/node/pull/49820)
* \[[`465ad2a5ce`](https://github.com/nodejs/node/commit/465ad2a5ce)] - **test**: update `url` web platform tests (Yagiz Nizipli) [#50264](https://github.com/nodejs/node/pull/50264)
* \[[`3b80a6894c`](https://github.com/nodejs/node/commit/3b80a6894c)] - **test**: set `test-emit-after-on-destroyed` as flaky (Yagiz Nizipli) [#50246](https://github.com/nodejs/node/pull/50246)
* \[[`57adbdd156`](https://github.com/nodejs/node/commit/57adbdd156)] - **test**: set inspector async stack test as flaky (Yagiz Nizipli) [#50244](https://github.com/nodejs/node/pull/50244)
* \[[`6507f66404`](https://github.com/nodejs/node/commit/6507f66404)] - **test**: set test-worker-nearheaplimit-deadlock flaky (StefanStojanovic) [#50277](https://github.com/nodejs/node/pull/50277)
* \[[`21a6ba548d`](https://github.com/nodejs/node/commit/21a6ba548d)] - **test**: set `test-cli-node-options` as flaky (Yagiz Nizipli) [#50296](https://github.com/nodejs/node/pull/50296)
* \[[`c55f8f30cb`](https://github.com/nodejs/node/commit/c55f8f30cb)] - **test**: reduce the number of requests and parsers (Luigi Pinca) [#50240](https://github.com/nodejs/node/pull/50240)
* \[[`5129bedfa2`](https://github.com/nodejs/node/commit/5129bedfa2)] - **test**: set crypto-timing test as flaky (Yagiz Nizipli) [#50232](https://github.com/nodejs/node/pull/50232)
* \[[`9bc5ab5e07`](https://github.com/nodejs/node/commit/9bc5ab5e07)] - **test**: set `test-structuredclone-*` as flaky (Yagiz Nizipli) [#50261](https://github.com/nodejs/node/pull/50261)
* \[[`317e447ddc`](https://github.com/nodejs/node/commit/317e447ddc)] - **test**: deflake `test-loaders-workers-spawned` (Antoine du Hamel) [#50251](https://github.com/nodejs/node/pull/50251)
* \[[`0c710daae2`](https://github.com/nodejs/node/commit/0c710daae2)] - **test**: improve code coverage of diagnostics\_channel (Jithil P Ponnan) [#50053](https://github.com/nodejs/node/pull/50053)
* \[[`7c6e4d7ec3`](https://github.com/nodejs/node/commit/7c6e4d7ec3)] - **test**: set `test-esm-loader-resolve-type` as flaky (Yagiz Nizipli) [#50226](https://github.com/nodejs/node/pull/50226)
* \[[`c8744909b0`](https://github.com/nodejs/node/commit/c8744909b0)] - **test**: set inspector async hook test as flaky (Yagiz Nizipli) [#50252](https://github.com/nodejs/node/pull/50252)
* \[[`3e38001739`](https://github.com/nodejs/node/commit/3e38001739)] - **test**: skip test-benchmark-os.js on IBM i (Abdirahim Musse) [#50208](https://github.com/nodejs/node/pull/50208)
* \[[`dd66fdfb7b`](https://github.com/nodejs/node/commit/dd66fdfb7b)] - **test**: set parallel http server test as flaky (Yagiz Nizipli) [#50227](https://github.com/nodejs/node/pull/50227)
* \[[`a38d1311bf`](https://github.com/nodejs/node/commit/a38d1311bf)] - **test**: set test-worker-nearheaplimit-deadlock flaky (Stefan Stojanovic) [#50238](https://github.com/nodejs/node/pull/50238)
* \[[`8efb75fd80`](https://github.com/nodejs/node/commit/8efb75fd80)] - **test**: set `test-runner-watch-mode` as flaky (Yagiz Nizipli) [#50221](https://github.com/nodejs/node/pull/50221)
* \[[`143ddded74`](https://github.com/nodejs/node/commit/143ddded74)] - **test**: set sea snapshot tests as flaky (Yagiz Nizipli) [#50223](https://github.com/nodejs/node/pull/50223)
* \[[`ae905a8f35`](https://github.com/nodejs/node/commit/ae905a8f35)] - **test**: fix defect path traversal tests (Tobias Nießen) [#50124](https://github.com/nodejs/node/pull/50124)
* \[[`ce27ee701b`](https://github.com/nodejs/node/commit/ce27ee701b)] - **tls**: reduce TLS 'close' event listener warnings (Tim Perry) [#50136](https://github.com/nodejs/node/pull/50136)
* \[[`ab4bae8e1f`](https://github.com/nodejs/node/commit/ab4bae8e1f)] - **tools**: drop support for osx notarization with gon (Ulises Gascón) [#50291](https://github.com/nodejs/node/pull/50291)
* \[[`5df3d5abcc`](https://github.com/nodejs/node/commit/5df3d5abcc)] - **tools**: update comment in `update-uncidi.sh` and `acorn_version.h` (Jungku Lee) [#50175](https://github.com/nodejs/node/pull/50175)
* \[[`bf7b94f0b3`](https://github.com/nodejs/node/commit/bf7b94f0b3)] - **tools**: refactor checkimports.py (Mohammed Keyvanzadeh) [#50011](https://github.com/nodejs/node/pull/50011)
* \[[`5dc454a837`](https://github.com/nodejs/node/commit/5dc454a837)] - **util**: remove internal mime fns from benchmarks (Aras Abbasi) [#50201](https://github.com/nodejs/node/pull/50201)
* \[[`8f7eb15603`](https://github.com/nodejs/node/commit/8f7eb15603)] - **vm**: use import attributes instead of import assertions (Antoine du Hamel) [#50141](https://github.com/nodejs/node/pull/50141)
* \[[`dda33c2bf1`](https://github.com/nodejs/node/commit/dda33c2bf1)] - **vm**: reject in importModuleDynamically without --experimental-vm-modules (Joyee Cheung) [#50137](https://github.com/nodejs/node/pull/50137)
* \[[`3999362c59`](https://github.com/nodejs/node/commit/3999362c59)] - **vm**: use internal versions of compileFunction and Script (Joyee Cheung) [#50137](https://github.com/nodejs/node/pull/50137)
* \[[`a54179f0e0`](https://github.com/nodejs/node/commit/a54179f0e0)] - **vm**: unify host-defined option generation in vm.compileFunction (Joyee Cheung) [#50137](https://github.com/nodejs/node/pull/50137)
* \[[`87be790fa9`](https://github.com/nodejs/node/commit/87be790fa9)] - **worker**: handle detached `MessagePort` from a different context (Juan José) [#49150](https://github.com/nodejs/node/pull/49150)

<a id="21.0.0"></a>

## 2023-10-17, Version 21.0.0 (Current), @RafaelGSS and @targos

We're excited to announce the release of Node.js 21! Highlights include updates of the V8 JavaScript engine to 11.8,
stable `fetch` and `WebStreams`, a new experimental flag to change the interpretation of ambiguous code
from CommonJS to ES modules (`--experimental-default-type`), many updates to our test runner, and more!

Node.js 21 will replace Node.js 20 as our ‘Current’ release line when Node.js 20 enters long-term support (LTS) later this month.
As per the release schedule, Node.js 21 will be ‘Current' release for the next 6 months, until April 2024.

### Other Notable Changes

* \[[`740ca5423a`](https://github.com/nodejs/node/commit/740ca5423a)] - **doc**: promote fetch/webstreams from experimental to stable (Steven) [#45684](https://github.com/nodejs/node/pull/45684)
* \[[`85301803e1`](https://github.com/nodejs/node/commit/85301803e1)] - **esm**: --experimental-default-type flag to flip module defaults (Geoffrey Booth) [#49869](https://github.com/nodejs/node/pull/49869)
* \[[`705e623ac4`](https://github.com/nodejs/node/commit/705e623ac4)] - **esm**: remove `globalPreload` hook (superseded by `initialize`) (Jacob Smith) [#49144](https://github.com/nodejs/node/pull/49144)
* \[[`e01c1d700d`](https://github.com/nodejs/node/commit/e01c1d700d)] - **fs**: add flush option to writeFile() functions (Colin Ihrig) [#50009](https://github.com/nodejs/node/pull/50009)
* \[[`1948dce707`](https://github.com/nodejs/node/commit/1948dce707)] - **(SEMVER-MAJOR)** **fs**: add globSync implementation (Moshe Atlow) [#47653](https://github.com/nodejs/node/pull/47653)
* \[[`e28dbe1c2b`](https://github.com/nodejs/node/commit/e28dbe1c2b)] - **(SEMVER-MINOR)** **lib**: add WebSocket client (Matthew Aitken) [#49830](https://github.com/nodejs/node/pull/49830)
* \[[`95b8f5dcab`](https://github.com/nodejs/node/commit/95b8f5dcab)] - **stream**: optimize Writable (Robert Nagy) [#50012](https://github.com/nodejs/node/pull/50012)
* \[[`7cd4e70948`](https://github.com/nodejs/node/commit/7cd4e70948)] - **(SEMVER-MAJOR)** **test\_runner**: support passing globs (Moshe Atlow) [#47653](https://github.com/nodejs/node/pull/47653)
* \[[`1d220b55ac`](https://github.com/nodejs/node/commit/1d220b55ac)] - **vm**: use default HDO when importModuleDynamically is not set (Joyee Cheung) [#49950](https://github.com/nodejs/node/pull/49950)

### Semver-Major Commits

* \[[`ac2a68c76b`](https://github.com/nodejs/node/commit/ac2a68c76b)] - **(SEMVER-MAJOR)** **build**: drop support for Visual Studio 2019 (Michaël Zasso) [#49051](https://github.com/nodejs/node/pull/49051)
* \[[`4e3983031a`](https://github.com/nodejs/node/commit/4e3983031a)] - **(SEMVER-MAJOR)** **build**: bump supported macOS and Xcode versions (Michaël Zasso) [#49164](https://github.com/nodejs/node/pull/49164)
* \[[`5a0777776d`](https://github.com/nodejs/node/commit/5a0777776d)] - **(SEMVER-MAJOR)** **crypto**: do not overwrite \_writableState.defaultEncoding (Tobias Nießen) [#49140](https://github.com/nodejs/node/pull/49140)
* \[[`162a0652ab`](https://github.com/nodejs/node/commit/162a0652ab)] - **(SEMVER-MAJOR)** **deps**: bump minimum ICU version to 73 (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`17a74ddd3d`](https://github.com/nodejs/node/commit/17a74ddd3d)] - **(SEMVER-MAJOR)** **deps**: update V8 to 11.8.172.13 (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`e9ff81016d`](https://github.com/nodejs/node/commit/e9ff81016d)] - **(SEMVER-MAJOR)** **deps**: update llhttp to 9.1.2 (Paolo Insogna) [#48981](https://github.com/nodejs/node/pull/48981)
* \[[`7ace5aba75`](https://github.com/nodejs/node/commit/7ace5aba75)] - **(SEMVER-MAJOR)** **events**: validate options of `on` and `once` (Deokjin Kim) [#46018](https://github.com/nodejs/node/pull/46018)
* \[[`b3ec13d449`](https://github.com/nodejs/node/commit/b3ec13d449)] - **(SEMVER-MAJOR)** **fs**: adjust `position` validation in reading methods (Livia Medeiros) [#42835](https://github.com/nodejs/node/pull/42835)
* \[[`1948dce707`](https://github.com/nodejs/node/commit/1948dce707)] - **(SEMVER-MAJOR)** **fs**: add globSync implementation (Moshe Atlow) [#47653](https://github.com/nodejs/node/pull/47653)
* \[[`d68d0eacaa`](https://github.com/nodejs/node/commit/d68d0eacaa)] - **(SEMVER-MAJOR)** **http**: reduce parts in chunked response when corking (Robert Nagy) [#50167](https://github.com/nodejs/node/pull/50167)
* \[[`c5b0b894ed`](https://github.com/nodejs/node/commit/c5b0b894ed)] - **(SEMVER-MAJOR)** **lib**: mark URL/URLSearchParams as uncloneable and untransferable (Chengzhong Wu) [#47497](https://github.com/nodejs/node/pull/47497)
* \[[`3205b1936a`](https://github.com/nodejs/node/commit/3205b1936a)] - **(SEMVER-MAJOR)** **lib**: remove aix directory case for package reader (Yagiz Nizipli) [#48605](https://github.com/nodejs/node/pull/48605)
* \[[`b40f0c3074`](https://github.com/nodejs/node/commit/b40f0c3074)] - **(SEMVER-MAJOR)** **lib**: add `navigator.hardwareConcurrency` (Yagiz Nizipli) [#47769](https://github.com/nodejs/node/pull/47769)
* \[[`4b08c4c047`](https://github.com/nodejs/node/commit/4b08c4c047)] - **(SEMVER-MAJOR)** **lib**: runtime deprecate punycode (Yagiz Nizipli) [#47202](https://github.com/nodejs/node/pull/47202)
* \[[`3ce51ae9c0`](https://github.com/nodejs/node/commit/3ce51ae9c0)] - **(SEMVER-MAJOR)** **module**: harmonize error code between ESM and CJS (Antoine du Hamel) [#48606](https://github.com/nodejs/node/pull/48606)
* \[[`7202859402`](https://github.com/nodejs/node/commit/7202859402)] - **(SEMVER-MAJOR)** **net**: do not treat `server.maxConnections=0` as `Infinity` (ignoramous) [#48276](https://github.com/nodejs/node/pull/48276)
* \[[`c15bafdaf4`](https://github.com/nodejs/node/commit/c15bafdaf4)] - **(SEMVER-MAJOR)** **net**: only defer \_final call when connecting (Jason Zhang) [#47385](https://github.com/nodejs/node/pull/47385)
* \[[`6ffacbf0f9`](https://github.com/nodejs/node/commit/6ffacbf0f9)] - **(SEMVER-MAJOR)** **node-api**: rename internal NAPI\_VERSION definition (Chengzhong Wu) [#48501](https://github.com/nodejs/node/pull/48501)
* \[[`11af089b14`](https://github.com/nodejs/node/commit/11af089b14)] - **(SEMVER-MAJOR)** **src**: update NODE\_MODULE\_VERSION to 120 (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`d920b7c94b`](https://github.com/nodejs/node/commit/d920b7c94b)] - **(SEMVER-MAJOR)** **src**: throw DOMException on cloning non-serializable objects (Chengzhong Wu) [#47839](https://github.com/nodejs/node/pull/47839)
* \[[`64549731b6`](https://github.com/nodejs/node/commit/64549731b6)] - **(SEMVER-MAJOR)** **src**: throw DataCloneError on transfering untransferable objects (Chengzhong Wu) [#47604](https://github.com/nodejs/node/pull/47604)
* \[[`dac8de689b`](https://github.com/nodejs/node/commit/dac8de689b)] - **(SEMVER-MAJOR)** **stream**: use private properties for strategies (Yagiz Nizipli) [#47218](https://github.com/nodejs/node/pull/47218)
* \[[`1fa084ecdf`](https://github.com/nodejs/node/commit/1fa084ecdf)] - **(SEMVER-MAJOR)** **stream**: use private properties for encoding (Yagiz Nizipli) [#47218](https://github.com/nodejs/node/pull/47218)
* \[[`4e93247079`](https://github.com/nodejs/node/commit/4e93247079)] - **(SEMVER-MAJOR)** **stream**: use private properties for compression (Yagiz Nizipli) [#47218](https://github.com/nodejs/node/pull/47218)
* \[[`527589b755`](https://github.com/nodejs/node/commit/527589b755)] - **(SEMVER-MAJOR)** **test\_runner**: disallow array in `run` options (Raz Luvaton) [#49935](https://github.com/nodejs/node/pull/49935)
* \[[`7cd4e70948`](https://github.com/nodejs/node/commit/7cd4e70948)] - **(SEMVER-MAJOR)** **test\_runner**: support passing globs (Moshe Atlow) [#47653](https://github.com/nodejs/node/pull/47653)
* \[[`2ef170254b`](https://github.com/nodejs/node/commit/2ef170254b)] - **(SEMVER-MAJOR)** **tls**: use `validateNumber` for `options.minDHSize` (Deokjin Kim) [#49973](https://github.com/nodejs/node/pull/49973)
* \[[`092fb9f541`](https://github.com/nodejs/node/commit/092fb9f541)] - **(SEMVER-MAJOR)** **tls**: use validateFunction for `options.checkServerIdentity` (Deokjin Kim) [#49896](https://github.com/nodejs/node/pull/49896)
* \[[`ccca547e28`](https://github.com/nodejs/node/commit/ccca547e28)] - **(SEMVER-MAJOR)** **util**: runtime deprecate `promisify`-ing a function returning a `Promise` (Antoine du Hamel) [#49609](https://github.com/nodejs/node/pull/49609)
* \[[`4038cf0513`](https://github.com/nodejs/node/commit/4038cf0513)] - **(SEMVER-MAJOR)** **vm**: freeze `dependencySpecifiers` array (Antoine du Hamel) [#49720](https://github.com/nodejs/node/pull/49720)

### Semver-Minor Commits

* \[[`3227d7327c`](https://github.com/nodejs/node/commit/3227d7327c)] - **(SEMVER-MINOR)** **deps**: update uvwasi to 0.0.19 (Node.js GitHub Bot) [#49908](https://github.com/nodejs/node/pull/49908)
* \[[`e28dbe1c2b`](https://github.com/nodejs/node/commit/e28dbe1c2b)] - **(SEMVER-MINOR)** **lib**: add WebSocket client (Matthew Aitken) [#49830](https://github.com/nodejs/node/pull/49830)
* \[[`9f9c58212e`](https://github.com/nodejs/node/commit/9f9c58212e)] - **(SEMVER-MINOR)** **test\_runner, cli**: add --test-concurrency flag (Colin Ihrig) [#49996](https://github.com/nodejs/node/pull/49996)
* \[[`d37b0d267f`](https://github.com/nodejs/node/commit/d37b0d267f)] - **(SEMVER-MINOR)** **wasi**: updates required for latest uvwasi version (Michael Dawson) [#49908](https://github.com/nodejs/node/pull/49908)

### Semver-Patch Commits

* \[[`33c87ec096`](https://github.com/nodejs/node/commit/33c87ec096)] - **benchmark**: fix race condition on fs benchs (Vinicius Lourenço) [#50035](https://github.com/nodejs/node/pull/50035)
* \[[`3c0ec61c4b`](https://github.com/nodejs/node/commit/3c0ec61c4b)] - **benchmark**: add warmup to accessSync bench (Rafael Gonzaga) [#50073](https://github.com/nodejs/node/pull/50073)
* \[[`1a839f388e`](https://github.com/nodejs/node/commit/1a839f388e)] - **benchmark**: improved config for blob,file benchmark (Vinícius Lourenço) [#49730](https://github.com/nodejs/node/pull/49730)
* \[[`86fe5a80f3`](https://github.com/nodejs/node/commit/86fe5a80f3)] - **benchmark**: added new benchmarks for blob (Vinícius Lourenço) [#49730](https://github.com/nodejs/node/pull/49730)
* \[[`6322d4f587`](https://github.com/nodejs/node/commit/6322d4f587)] - **build**: fix IBM i build with Python 3.9 (Richard Lau) [#48056](https://github.com/nodejs/node/pull/48056)
* \[[`17c55d176b`](https://github.com/nodejs/node/commit/17c55d176b)] - **build**: reset embedder string to "-node.0" (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`f10928f926`](https://github.com/nodejs/node/commit/f10928f926)] - **crypto**: use X509\_ALGOR accessors instead of reaching into X509\_ALGOR (David Benjamin) [#50057](https://github.com/nodejs/node/pull/50057)
* \[[`136a96722a`](https://github.com/nodejs/node/commit/136a96722a)] - **crypto**: account for disabled SharedArrayBuffer (Shelley Vohr) [#50034](https://github.com/nodejs/node/pull/50034)
* \[[`17b9925393`](https://github.com/nodejs/node/commit/17b9925393)] - **crypto**: return clear errors when loading invalid PFX data (Tim Perry) [#49566](https://github.com/nodejs/node/pull/49566)
* \[[`ca25d564c6`](https://github.com/nodejs/node/commit/ca25d564c6)] - **deps**: upgrade npm to 10.2.0 (npm team) [#50027](https://github.com/nodejs/node/pull/50027)
* \[[`f23a9353ae`](https://github.com/nodejs/node/commit/f23a9353ae)] - **deps**: update corepack to 0.21.0 (Node.js GitHub Bot) [#50088](https://github.com/nodejs/node/pull/50088)
* \[[`ceedb3a509`](https://github.com/nodejs/node/commit/ceedb3a509)] - **deps**: update simdutf to 3.2.18 (Node.js GitHub Bot) [#50091](https://github.com/nodejs/node/pull/50091)
* \[[`0522ac086c`](https://github.com/nodejs/node/commit/0522ac086c)] - **deps**: update zlib to 1.2.13.1-motley-fef5869 (Node.js GitHub Bot) [#50085](https://github.com/nodejs/node/pull/50085)
* \[[`4f8c5829da`](https://github.com/nodejs/node/commit/4f8c5829da)] - **deps**: update googletest to 2dd1c13 (Node.js GitHub Bot) [#50081](https://github.com/nodejs/node/pull/50081)
* \[[`588784ea30`](https://github.com/nodejs/node/commit/588784ea30)] - **deps**: update undici to 5.25.4 (Node.js GitHub Bot) [#50025](https://github.com/nodejs/node/pull/50025)
* \[[`c9eef0c3c4`](https://github.com/nodejs/node/commit/c9eef0c3c4)] - **deps**: update googletest to e47544a (Node.js GitHub Bot) [#49982](https://github.com/nodejs/node/pull/49982)
* \[[`23cb478398`](https://github.com/nodejs/node/commit/23cb478398)] - **deps**: update ada to 2.6.10 (Node.js GitHub Bot) [#49984](https://github.com/nodejs/node/pull/49984)
* \[[`61411bb323`](https://github.com/nodejs/node/commit/61411bb323)] - **deps**: fix call to undeclared functions 'ntohl' and 'htons' (MatteoBax) [#49979](https://github.com/nodejs/node/pull/49979)
* \[[`49cf182e30`](https://github.com/nodejs/node/commit/49cf182e30)] - **deps**: update ada to 2.6.9 (Node.js GitHub Bot) [#49340](https://github.com/nodejs/node/pull/49340)
* \[[`ceb6df0f22`](https://github.com/nodejs/node/commit/ceb6df0f22)] - **deps**: update ada to 2.6.8 (Node.js GitHub Bot) [#49340](https://github.com/nodejs/node/pull/49340)
* \[[`b73e18b5dc`](https://github.com/nodejs/node/commit/b73e18b5dc)] - **deps**: update ada to 2.6.7 (Node.js GitHub Bot) [#49340](https://github.com/nodejs/node/pull/49340)
* \[[`baf2256617`](https://github.com/nodejs/node/commit/baf2256617)] - **deps**: update ada to 2.6.5 (Node.js GitHub Bot) [#49340](https://github.com/nodejs/node/pull/49340)
* \[[`a20a328a9b`](https://github.com/nodejs/node/commit/a20a328a9b)] - **deps**: update ada to 2.6.3 (Node.js GitHub Bot) [#49340](https://github.com/nodejs/node/pull/49340)
* \[[`3838b579e4`](https://github.com/nodejs/node/commit/3838b579e4)] - **deps**: V8: cherry-pick 8ec2651fbdd8 (Abdirahim Musse) [#49862](https://github.com/nodejs/node/pull/49862)
* \[[`668437ccad`](https://github.com/nodejs/node/commit/668437ccad)] - **deps**: V8: cherry-pick b60a03df4ceb (Joyee Cheung) [#49491](https://github.com/nodejs/node/pull/49491)
* \[[`f970087147`](https://github.com/nodejs/node/commit/f970087147)] - **deps**: V8: backport 93b1a74cbc9b (Joyee Cheung) [#49419](https://github.com/nodejs/node/pull/49419)
* \[[`4531c154e5`](https://github.com/nodejs/node/commit/4531c154e5)] - **deps**: V8: cherry-pick 8ec2651fbdd8 (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`9ad0e2cacc`](https://github.com/nodejs/node/commit/9ad0e2cacc)] - **deps**: V8: cherry-pick 89b3702c92b0 (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`dfc9c86868`](https://github.com/nodejs/node/commit/dfc9c86868)] - **deps**: V8: cherry-pick de9a5de2274f (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`186b36efba`](https://github.com/nodejs/node/commit/186b36efba)] - **deps**: V8: cherry-pick b5b5d6c31bb0 (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`867586ce95`](https://github.com/nodejs/node/commit/867586ce95)] - **deps**: V8: cherry-pick 93b1a74cbc9b (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`4ad3479ba7`](https://github.com/nodejs/node/commit/4ad3479ba7)] - **deps**: V8: cherry-pick 1a3ecc2483b2 (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`660f902f16`](https://github.com/nodejs/node/commit/660f902f16)] - **deps**: patch V8 to avoid duplicated zlib symbol (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`f7c1d410ad`](https://github.com/nodejs/node/commit/f7c1d410ad)] - **deps**: remove usage of a C++20 feature from V8 (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`9c4030bfb9`](https://github.com/nodejs/node/commit/9c4030bfb9)] - **deps**: avoid compilation error with ASan (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`5f05cc15e6`](https://github.com/nodejs/node/commit/5f05cc15e6)] - **deps**: disable V8 concurrent sparkplug compilation (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`42cd952dbd`](https://github.com/nodejs/node/commit/42cd952dbd)] - **deps**: silence irrelevant V8 warning (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`88cf90f9c4`](https://github.com/nodejs/node/commit/88cf90f9c4)] - **deps**: always define V8\_EXPORT\_PRIVATE as no-op (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`8609915951`](https://github.com/nodejs/node/commit/8609915951)] - **doc**: improve ccache explanation (Chengzhong Wu) [#50133](https://github.com/nodejs/node/pull/50133)
* \[[`91d21324a9`](https://github.com/nodejs/node/commit/91d21324a9)] - **doc**: move danielleadams to TSC non-voting member (Danielle Adams) [#50142](https://github.com/nodejs/node/pull/50142)
* \[[`34fa7043a2`](https://github.com/nodejs/node/commit/34fa7043a2)] - **doc**: fix description of `fs.readdir` `recursive` option (RamdohokarAngha) [#48902](https://github.com/nodejs/node/pull/48902)
* \[[`81e4d2ec2f`](https://github.com/nodejs/node/commit/81e4d2ec2f)] - **doc**: mention files read before env setup (Rafael Gonzaga) [#50072](https://github.com/nodejs/node/pull/50072)
* \[[`0ce37ed8e9`](https://github.com/nodejs/node/commit/0ce37ed8e9)] - **doc**: move permission model to Active Development (Rafael Gonzaga) [#50068](https://github.com/nodejs/node/pull/50068)
* \[[`3c430212c3`](https://github.com/nodejs/node/commit/3c430212c3)] - **doc**: add command to get patch minors and majors (Rafael Gonzaga) [#50067](https://github.com/nodejs/node/pull/50067)
* \[[`e43bf4c31d`](https://github.com/nodejs/node/commit/e43bf4c31d)] - **doc**: use precise promise terminology in fs (Benjamin Gruenbaum) [#50029](https://github.com/nodejs/node/pull/50029)
* \[[`d3a5f1fb5f`](https://github.com/nodejs/node/commit/d3a5f1fb5f)] - **doc**: use precise terminology in test runner (Benjamin Gruenbaum) [#50028](https://github.com/nodejs/node/pull/50028)
* \[[`24dea2348d`](https://github.com/nodejs/node/commit/24dea2348d)] - **doc**: clarify explaination text on how to run the example (Anshul Sinha) [#39020](https://github.com/nodejs/node/pull/39020)
* \[[`f3ed57bd8b`](https://github.com/nodejs/node/commit/f3ed57bd8b)] - **doc**: reserve 119 for Electron 28 (David Sanders) [#50020](https://github.com/nodejs/node/pull/50020)
* \[[`85c09f178c`](https://github.com/nodejs/node/commit/85c09f178c)] - **doc**: update Collaborator pronouns (Tierney Cyren) [#50005](https://github.com/nodejs/node/pull/50005)
* \[[`099e2f7bce`](https://github.com/nodejs/node/commit/099e2f7bce)] - **doc**: update link to Abstract Modules Records spec (Rich Trott) [#49961](https://github.com/nodejs/node/pull/49961)
* \[[`47b2883673`](https://github.com/nodejs/node/commit/47b2883673)] - **doc**: updated building docs for windows (Claudio W) [#49767](https://github.com/nodejs/node/pull/49767)
* \[[`7b624c30b2`](https://github.com/nodejs/node/commit/7b624c30b2)] - **doc**: update CHANGELOG\_V20 about vm fixes (Joyee Cheung) [#49951](https://github.com/nodejs/node/pull/49951)
* \[[`1dc0667aa6`](https://github.com/nodejs/node/commit/1dc0667aa6)] - **doc**: document dangerous symlink behavior (Tobias Nießen) [#49154](https://github.com/nodejs/node/pull/49154)
* \[[`bc056c2426`](https://github.com/nodejs/node/commit/bc056c2426)] - **doc**: add main ARIA landmark to API docs (Rich Trott) [#49882](https://github.com/nodejs/node/pull/49882)
* \[[`f416a0f555`](https://github.com/nodejs/node/commit/f416a0f555)] - **doc**: add navigation ARIA landmark to doc ToC (Rich Trott) [#49882](https://github.com/nodejs/node/pull/49882)
* \[[`740ca5423a`](https://github.com/nodejs/node/commit/740ca5423a)] - **doc**: promote fetch/webstreams from experimental to stable (Steven) [#45684](https://github.com/nodejs/node/pull/45684)
* \[[`f802aa0645`](https://github.com/nodejs/node/commit/f802aa0645)] - **doc**: fix 'partial' typo (Colin Ihrig) [#48657](https://github.com/nodejs/node/pull/48657)
* \[[`6fda81d4f5`](https://github.com/nodejs/node/commit/6fda81d4f5)] - **doc**: mention `Navigator` is a partial implementation (Moshe Atlow) [#48656](https://github.com/nodejs/node/pull/48656)
* \[[`6aa2aeedcb`](https://github.com/nodejs/node/commit/6aa2aeedcb)] - **doc**: mark Node.js 19 as End-of-Life (Richard Lau) [#48283](https://github.com/nodejs/node/pull/48283)
* \[[`0ee9c83ffc`](https://github.com/nodejs/node/commit/0ee9c83ffc)] - **errors**: improve performance of determine-specific-type (Aras Abbasi) [#49696](https://github.com/nodejs/node/pull/49696)
* \[[`4f84a3d200`](https://github.com/nodejs/node/commit/4f84a3d200)] - **errors**: improve formatList in errors.js (Aras Abbasi) [#49642](https://github.com/nodejs/node/pull/49642)
* \[[`cc725a653a`](https://github.com/nodejs/node/commit/cc725a653a)] - **errors**: improve performance of instantiation (Aras Abbasi) [#49654](https://github.com/nodejs/node/pull/49654)
* \[[`d1ef6aa2db`](https://github.com/nodejs/node/commit/d1ef6aa2db)] - **esm**: use import attributes instead of import assertions (Antoine du Hamel) [#50140](https://github.com/nodejs/node/pull/50140)
* \[[`19b470f866`](https://github.com/nodejs/node/commit/19b470f866)] - **esm**: bypass CommonJS loader under --default-type (Geoffrey Booth) [#49986](https://github.com/nodejs/node/pull/49986)
* \[[`9c683204db`](https://github.com/nodejs/node/commit/9c683204db)] - **esm**: unflag extensionless javascript and wasm in module scope (Geoffrey Booth) [#49974](https://github.com/nodejs/node/pull/49974)
* \[[`05be31d5de`](https://github.com/nodejs/node/commit/05be31d5de)] - **esm**: improve `getFormatOfExtensionlessFile` speed (Yagiz Nizipli) [#49965](https://github.com/nodejs/node/pull/49965)
* \[[`aadfea4979`](https://github.com/nodejs/node/commit/aadfea4979)] - **esm**: improve JSDoc annotation of internal functions (Antoine du Hamel) [#49959](https://github.com/nodejs/node/pull/49959)
* \[[`7f0e36af52`](https://github.com/nodejs/node/commit/7f0e36af52)] - **esm**: fix cache collision on JSON files using file: URL (Antoine du Hamel) [#49887](https://github.com/nodejs/node/pull/49887)
* \[[`85301803e1`](https://github.com/nodejs/node/commit/85301803e1)] - **esm**: --experimental-default-type flag to flip module defaults (Geoffrey Booth) [#49869](https://github.com/nodejs/node/pull/49869)
* \[[`f42a103991`](https://github.com/nodejs/node/commit/f42a103991)] - **esm**: require braces for modules code (Geoffrey Booth) [#49657](https://github.com/nodejs/node/pull/49657)
* \[[`705e623ac4`](https://github.com/nodejs/node/commit/705e623ac4)] - **esm**: remove `globalPreload` hook (superseded by `initialize`) (Jacob Smith) [#49144](https://github.com/nodejs/node/pull/49144)
* \[[`18a818744f`](https://github.com/nodejs/node/commit/18a818744f)] - **fs**: improve error performance of `readdirSync` (Yagiz Nizipli) [#50131](https://github.com/nodejs/node/pull/50131)
* \[[`d3985296a9`](https://github.com/nodejs/node/commit/d3985296a9)] - **fs**: fix `unlinkSync` typings (Yagiz Nizipli) [#49859](https://github.com/nodejs/node/pull/49859)
* \[[`6bc7fa7906`](https://github.com/nodejs/node/commit/6bc7fa7906)] - **fs**: improve error perf of sync `chmod`+`fchmod` (CanadaHonk) [#49859](https://github.com/nodejs/node/pull/49859)
* \[[`6bd77db41f`](https://github.com/nodejs/node/commit/6bd77db41f)] - **fs**: improve error perf of sync `*times` (CanadaHonk) [#49864](https://github.com/nodejs/node/pull/49864)
* \[[`bf0f0789da`](https://github.com/nodejs/node/commit/bf0f0789da)] - **fs**: improve error performance of writevSync (IlyasShabi) [#50038](https://github.com/nodejs/node/pull/50038)
* \[[`8a49735bae`](https://github.com/nodejs/node/commit/8a49735bae)] - **fs**: add flush option to createWriteStream() (Colin Ihrig) [#50093](https://github.com/nodejs/node/pull/50093)
* \[[`ed49722a8a`](https://github.com/nodejs/node/commit/ed49722a8a)] - **fs**: improve error performance for `ftruncateSync` (André Alves) [#50032](https://github.com/nodejs/node/pull/50032)
* \[[`e01c1d700d`](https://github.com/nodejs/node/commit/e01c1d700d)] - **fs**: add flush option to writeFile() functions (Colin Ihrig) [#50009](https://github.com/nodejs/node/pull/50009)
* \[[`f7a160d5b4`](https://github.com/nodejs/node/commit/f7a160d5b4)] - **fs**: improve error performance for `fdatasyncSync` (Jungku Lee) [#49898](https://github.com/nodejs/node/pull/49898)
* \[[`813713f211`](https://github.com/nodejs/node/commit/813713f211)] - **fs**: throw errors from sync branches instead of separate implementations (Joyee Cheung) [#49913](https://github.com/nodejs/node/pull/49913)
* \[[`b866e38192`](https://github.com/nodejs/node/commit/b866e38192)] - **http**: refactor to make servername option normalization testable (Rongjian Zhang) [#38733](https://github.com/nodejs/node/pull/38733)
* \[[`2990390359`](https://github.com/nodejs/node/commit/2990390359)] - **inspector**: simplify dispatchProtocolMessage (Daniel Lemire) [#49780](https://github.com/nodejs/node/pull/49780)
* \[[`d4c5fe488e`](https://github.com/nodejs/node/commit/d4c5fe488e)] - **lib**: fix compileFunction throws range error for negative numbers (Jithil P Ponnan) [#49855](https://github.com/nodejs/node/pull/49855)
* \[[`589ac5004c`](https://github.com/nodejs/node/commit/589ac5004c)] - **lib**: faster internal createBlob (Vinícius Lourenço) [#49730](https://github.com/nodejs/node/pull/49730)
* \[[`952cf0d17a`](https://github.com/nodejs/node/commit/952cf0d17a)] - **lib**: reduce overhead of validateObject (Vinicius Lourenço) [#49928](https://github.com/nodejs/node/pull/49928)
* \[[`fa250fdec1`](https://github.com/nodejs/node/commit/fa250fdec1)] - **lib**: make fetch sync and return a Promise (Matthew Aitken) [#49936](https://github.com/nodejs/node/pull/49936)
* \[[`1b96975f27`](https://github.com/nodejs/node/commit/1b96975f27)] - **lib**: fix `primordials` typings (Sam Verschueren) [#49895](https://github.com/nodejs/node/pull/49895)
* \[[`6aa7101960`](https://github.com/nodejs/node/commit/6aa7101960)] - **lib**: update params in jsdoc for `HTTPRequestOptions` (Jungku Lee) [#49872](https://github.com/nodejs/node/pull/49872)
* \[[`a4fdb1abe0`](https://github.com/nodejs/node/commit/a4fdb1abe0)] - **lib,test**: do not hardcode Buffer.kMaxLength (Michaël Zasso) [#49876](https://github.com/nodejs/node/pull/49876)
* \[[`fd21429ef5`](https://github.com/nodejs/node/commit/fd21429ef5)] - **lib**: update usage of always on Atomics API (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`bac85be22d`](https://github.com/nodejs/node/commit/bac85be22d)] - **meta**: ping TSC for offboarding (Tobias Nießen) [#50147](https://github.com/nodejs/node/pull/50147)
* \[[`609b13e6c2`](https://github.com/nodejs/node/commit/609b13e6c2)] - **meta**: bump actions/upload-artifact from 3.1.2 to 3.1.3 (dependabot\[bot]) [#50000](https://github.com/nodejs/node/pull/50000)
* \[[`3825464ef4`](https://github.com/nodejs/node/commit/3825464ef4)] - **meta**: bump actions/cache from 3.3.1 to 3.3.2 (dependabot\[bot]) [#50003](https://github.com/nodejs/node/pull/50003)
* \[[`49f0f9ca11`](https://github.com/nodejs/node/commit/49f0f9ca11)] - **meta**: bump github/codeql-action from 2.21.5 to 2.21.9 (dependabot\[bot]) [#50002](https://github.com/nodejs/node/pull/50002)
* \[[`f156427244`](https://github.com/nodejs/node/commit/f156427244)] - **meta**: bump actions/checkout from 3.6.0 to 4.1.0 (dependabot\[bot]) [#50001](https://github.com/nodejs/node/pull/50001)
* \[[`0fe673c7e6`](https://github.com/nodejs/node/commit/0fe673c7e6)] - **meta**: update website team with new name (Rich Trott) [#49883](https://github.com/nodejs/node/pull/49883)
* \[[`51f4ff2450`](https://github.com/nodejs/node/commit/51f4ff2450)] - **module**: move helpers out of cjs loader (Geoffrey Booth) [#49912](https://github.com/nodejs/node/pull/49912)
* \[[`7517c9f95b`](https://github.com/nodejs/node/commit/7517c9f95b)] - **module, esm**: jsdoc for modules files (Geoffrey Booth) [#49523](https://github.com/nodejs/node/pull/49523)
* \[[`b55adfb4f1`](https://github.com/nodejs/node/commit/b55adfb4f1)] - **node-api**: update headers for better wasm support (Toyo Li) [#49037](https://github.com/nodejs/node/pull/49037)
* \[[`b38e312486`](https://github.com/nodejs/node/commit/b38e312486)] - **node-api**: run finalizers directly from GC (Vladimir Morozov) [#42651](https://github.com/nodejs/node/pull/42651)
* \[[`0f0dd1a493`](https://github.com/nodejs/node/commit/0f0dd1a493)] - **os**: cache homedir, remove getCheckedFunction (Aras Abbasi) [#50037](https://github.com/nodejs/node/pull/50037)
* \[[`0e507d30ac`](https://github.com/nodejs/node/commit/0e507d30ac)] - **perf\_hooks**: reduce overhead of new user timings (Vinicius Lourenço) [#49914](https://github.com/nodejs/node/pull/49914)
* \[[`328bdac7f0`](https://github.com/nodejs/node/commit/328bdac7f0)] - **perf\_hooks**: reducing overhead of performance observer entry list (Vinicius Lourenço) [#50008](https://github.com/nodejs/node/pull/50008)
* \[[`e6e320ecc7`](https://github.com/nodejs/node/commit/e6e320ecc7)] - **perf\_hooks**: reduce overhead of new resource timings (Vinicius Lourenço) [#49837](https://github.com/nodejs/node/pull/49837)
* \[[`971af4b211`](https://github.com/nodejs/node/commit/971af4b211)] - **quic**: fix up coverity warning in quic/session.cc (Michael Dawson) [#49865](https://github.com/nodejs/node/pull/49865)
* \[[`546797f2b1`](https://github.com/nodejs/node/commit/546797f2b1)] - **quic**: prevent copying ngtcp2\_cid (Tobias Nießen) [#48561](https://github.com/nodejs/node/pull/48561)
* \[[`ac6f594c97`](https://github.com/nodejs/node/commit/ac6f594c97)] - **quic**: address new coverity warning (Michael Dawson) [#48384](https://github.com/nodejs/node/pull/48384)
* \[[`4ee8ef269b`](https://github.com/nodejs/node/commit/4ee8ef269b)] - **quic**: prevent copying ngtcp2\_cid\_token (Tobias Nießen) [#48370](https://github.com/nodejs/node/pull/48370)
* \[[`6d2811fbf2`](https://github.com/nodejs/node/commit/6d2811fbf2)] - **quic**: add additional implementation (James M Snell) [#47927](https://github.com/nodejs/node/pull/47927)
* \[[`0b3fcfcf35`](https://github.com/nodejs/node/commit/0b3fcfcf35)] - **quic**: fix typo in endpoint.h (Tobias Nießen) [#47911](https://github.com/nodejs/node/pull/47911)
* \[[`76044c4e2b`](https://github.com/nodejs/node/commit/76044c4e2b)] - **quic**: add additional QUIC implementation (James M Snell) [#47603](https://github.com/nodejs/node/pull/47603)
* \[[`78a15702dd`](https://github.com/nodejs/node/commit/78a15702dd)] - **src**: avoid making JSTransferable wrapper object weak (Chengzhong Wu) [#50026](https://github.com/nodejs/node/pull/50026)
* \[[`387e2929fe`](https://github.com/nodejs/node/commit/387e2929fe)] - **src**: generate default snapshot with --predictable (Joyee Cheung) [#48749](https://github.com/nodejs/node/pull/48749)
* \[[`1643adf771`](https://github.com/nodejs/node/commit/1643adf771)] - **src**: fix TLSWrap lifetime bug in ALPN callback (Ben Noordhuis) [#49635](https://github.com/nodejs/node/pull/49635)
* \[[`66776d8665`](https://github.com/nodejs/node/commit/66776d8665)] - **src**: set port in node\_options to uint16\_t (Yagiz Nizipli) [#49151](https://github.com/nodejs/node/pull/49151)
* \[[`55ff64001a`](https://github.com/nodejs/node/commit/55ff64001a)] - **src**: name scoped lock (Mohammed Keyvanzadeh) [#50010](https://github.com/nodejs/node/pull/50010)
* \[[`b903a710f4`](https://github.com/nodejs/node/commit/b903a710f4)] - **src**: use exact return value for `uv_os_getenv` (Yagiz Nizipli) [#49149](https://github.com/nodejs/node/pull/49149)
* \[[`43500fa646`](https://github.com/nodejs/node/commit/43500fa646)] - **src**: move const variable in `node_file.h` to `node_file.cc` (Jungku Lee) [#49688](https://github.com/nodejs/node/pull/49688)
* \[[`36ab510da7`](https://github.com/nodejs/node/commit/36ab510da7)] - **src**: remove unused variable (Michaël Zasso) [#49665](https://github.com/nodejs/node/pull/49665)
* \[[`23d65e7281`](https://github.com/nodejs/node/commit/23d65e7281)] - **src**: revert `IS_RELEASE` to 0 (Rafael Gonzaga) [#49084](https://github.com/nodejs/node/pull/49084)
* \[[`38dee8a1c0`](https://github.com/nodejs/node/commit/38dee8a1c0)] - **src**: distinguish HTML transferable and cloneable (Chengzhong Wu) [#47956](https://github.com/nodejs/node/pull/47956)
* \[[`586fcff061`](https://github.com/nodejs/node/commit/586fcff061)] - **src**: fix logically dead code reported by Coverity (Mohammed Keyvanzadeh) [#48589](https://github.com/nodejs/node/pull/48589)
* \[[`7f2c810814`](https://github.com/nodejs/node/commit/7f2c810814)] - **src,tools**: initialize cppgc (Daryl Haresign) [#45704](https://github.com/nodejs/node/pull/45704)
* \[[`aad8002b88`](https://github.com/nodejs/node/commit/aad8002b88)] - **stream**: use private symbol for bitmap state (Robert Nagy) [#49993](https://github.com/nodejs/node/pull/49993)
* \[[`a85e4186e5`](https://github.com/nodejs/node/commit/a85e4186e5)] - **stream**: reduce overhead of transfer (Vinicius Lourenço) [#50107](https://github.com/nodejs/node/pull/50107)
* \[[`e9bda11761`](https://github.com/nodejs/node/commit/e9bda11761)] - **stream**: lazy allocate back pressure buffer (Robert Nagy) [#50013](https://github.com/nodejs/node/pull/50013)
* \[[`557044af40`](https://github.com/nodejs/node/commit/557044af40)] - **stream**: avoid unnecessary drain for sync stream (Robert Nagy) [#50014](https://github.com/nodejs/node/pull/50014)
* \[[`95b8f5dcab`](https://github.com/nodejs/node/commit/95b8f5dcab)] - **stream**: optimize Writable (Robert Nagy) [#50012](https://github.com/nodejs/node/pull/50012)
* \[[`5de25deeb9`](https://github.com/nodejs/node/commit/5de25deeb9)] - **stream**: avoid tick in writable hot path (Robert Nagy) [#49966](https://github.com/nodejs/node/pull/49966)
* \[[`53b5545672`](https://github.com/nodejs/node/commit/53b5545672)] - **stream**: writable state bitmap (Robert Nagy) [#49899](https://github.com/nodejs/node/pull/49899)
* \[[`d4e99b1a66`](https://github.com/nodejs/node/commit/d4e99b1a66)] - **stream**: remove asIndexedPairs (Chemi Atlow) [#48150](https://github.com/nodejs/node/pull/48150)
* \[[`41e4174945`](https://github.com/nodejs/node/commit/41e4174945)] - **test**: replace forEach with for..of in test-net-isipv6.js (Niya Shiyas) [#49823](https://github.com/nodejs/node/pull/49823)
* \[[`f0e720a7fa`](https://github.com/nodejs/node/commit/f0e720a7fa)] - **test**: add EOVERFLOW as an allowed error (Abdirahim Musse) [#50128](https://github.com/nodejs/node/pull/50128)
* \[[`224f3ae974`](https://github.com/nodejs/node/commit/224f3ae974)] - **test**: reduce number of repetition in test-heapdump-shadowrealm.js (Chengzhong Wu) [#50104](https://github.com/nodejs/node/pull/50104)
* \[[`76004f3e56`](https://github.com/nodejs/node/commit/76004f3e56)] - **test**: replace forEach with for..of in test-parse-args.mjs (Niya Shiyas) [#49824](https://github.com/nodejs/node/pull/49824)
* \[[`fce8fbadcd`](https://github.com/nodejs/node/commit/fce8fbadcd)] - **test**: replace forEach with for..of in test-process-env (Niya Shiyas) [#49825](https://github.com/nodejs/node/pull/49825)
* \[[`24492476a7`](https://github.com/nodejs/node/commit/24492476a7)] - **test**: replace forEach with for..of in test-http-url (Niya Shiyas) [#49840](https://github.com/nodejs/node/pull/49840)
* \[[`2fe511ba23`](https://github.com/nodejs/node/commit/2fe511ba23)] - **test**: replace forEach() in test-net-perf\_hooks with for of (Narcisa Codreanu) [#49831](https://github.com/nodejs/node/pull/49831)
* \[[`42c37f28e6`](https://github.com/nodejs/node/commit/42c37f28e6)] - **test**: change forEach to for...of (Tiffany Lastimosa) [#49799](https://github.com/nodejs/node/pull/49799)
* \[[`6c9625dca4`](https://github.com/nodejs/node/commit/6c9625dca4)] - **test**: update skip for moved `test-wasm-web-api` (Richard Lau) [#49958](https://github.com/nodejs/node/pull/49958)
* \[[`f05d6d090c`](https://github.com/nodejs/node/commit/f05d6d090c)] - _**Revert**_ "**test**: mark test-runner-output as flaky" (Luigi Pinca) [#49905](https://github.com/nodejs/node/pull/49905)
* \[[`035e06317a`](https://github.com/nodejs/node/commit/035e06317a)] - **test**: disambiguate AIX and IBM i (Richard Lau) [#48056](https://github.com/nodejs/node/pull/48056)
* \[[`4d0aeed4a6`](https://github.com/nodejs/node/commit/4d0aeed4a6)] - **test**: deflake test-perf-hooks.js (Joyee Cheung) [#49892](https://github.com/nodejs/node/pull/49892)
* \[[`853f57239c`](https://github.com/nodejs/node/commit/853f57239c)] - **test**: migrate message error tests from Python to JS (Yiyun Lei) [#49721](https://github.com/nodejs/node/pull/49721)
* \[[`a71e3a65bb`](https://github.com/nodejs/node/commit/a71e3a65bb)] - **test**: fix edge snapshot stack traces (Geoffrey Booth) [#49659](https://github.com/nodejs/node/pull/49659)
* \[[`6b76b7782c`](https://github.com/nodejs/node/commit/6b76b7782c)] - **test**: skip v8-updates/test-linux-perf (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`c13c98dd38`](https://github.com/nodejs/node/commit/c13c98dd38)] - **test**: skip test-tick-processor-arguments on SmartOS (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`738aa304b3`](https://github.com/nodejs/node/commit/738aa304b3)] - **test**: adapt REPL test to V8 changes (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`de5c009252`](https://github.com/nodejs/node/commit/de5c009252)] - **test**: adapt test-fs-write to V8 internal changes (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`8c36168b42`](https://github.com/nodejs/node/commit/8c36168b42)] - **test**: update flag to disable SharedArrayBuffer (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`6ccb15f7ef`](https://github.com/nodejs/node/commit/6ccb15f7ef)] - **test**: adapt debugger tests to V8 11.4 (Philip Pfaffe) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`c5de3b49e8`](https://github.com/nodejs/node/commit/c5de3b49e8)] - **test,crypto**: update WebCryptoAPI WPT (Filip Skokan) [#50039](https://github.com/nodejs/node/pull/50039)
* \[[`4b35a9cfda`](https://github.com/nodejs/node/commit/4b35a9cfda)] - **test\_runner**: add test location for FileTests (Colin Ihrig) [#49999](https://github.com/nodejs/node/pull/49999)
* \[[`c935d4c8fa`](https://github.com/nodejs/node/commit/c935d4c8fa)] - **test\_runner**: replace spurious if with else (Colin Ihrig) [#49943](https://github.com/nodejs/node/pull/49943)
* \[[`a4c7f81241`](https://github.com/nodejs/node/commit/a4c7f81241)] - **test\_runner**: catch reporter errors (Moshe Atlow) [#49646](https://github.com/nodejs/node/pull/49646)
* \[[`bb52656fc6`](https://github.com/nodejs/node/commit/bb52656fc6)] - _**Revert**_ "**test\_runner**: run global after() hook earlier" (Joyee Cheung) [#49110](https://github.com/nodejs/node/pull/49110)
* \[[`6346bdc526`](https://github.com/nodejs/node/commit/6346bdc526)] - **test\_runner**: run global after() hook earlier (Colin Ihrig) [#49059](https://github.com/nodejs/node/pull/49059)
* \[[`0d8faf2952`](https://github.com/nodejs/node/commit/0d8faf2952)] - **test\_runner,test**: fix flaky test-runner-cli-concurrency.js (Colin Ihrig) [#50108](https://github.com/nodejs/node/pull/50108)
* \[[`b1ada0ad55`](https://github.com/nodejs/node/commit/b1ada0ad55)] - **tls**: handle cases where the raw socket is destroyed (Luigi Pinca) [#49980](https://github.com/nodejs/node/pull/49980)
* \[[`fae1af0a75`](https://github.com/nodejs/node/commit/fae1af0a75)] - **tls**: ciphers allow bang syntax (Chemi Atlow) [#49712](https://github.com/nodejs/node/pull/49712)
* \[[`766198b9e1`](https://github.com/nodejs/node/commit/766198b9e1)] - **tools**: fix comments referencing dep\_updaters scripts (Keksonoid) [#50165](https://github.com/nodejs/node/pull/50165)
* \[[`760b5dd259`](https://github.com/nodejs/node/commit/760b5dd259)] - **tools**: remove no-return-await lint rule (翠 / green) [#50118](https://github.com/nodejs/node/pull/50118)
* \[[`a0a5b751fb`](https://github.com/nodejs/node/commit/a0a5b751fb)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#50083](https://github.com/nodejs/node/pull/50083)
* \[[`69fb55e6b9`](https://github.com/nodejs/node/commit/69fb55e6b9)] - **tools**: update eslint to 8.51.0 (Node.js GitHub Bot) [#50084](https://github.com/nodejs/node/pull/50084)
* \[[`f73650ea52`](https://github.com/nodejs/node/commit/f73650ea52)] - **tools**: remove genv8constants.py (Ben Noordhuis) [#50023](https://github.com/nodejs/node/pull/50023)
* \[[`581434e54f`](https://github.com/nodejs/node/commit/581434e54f)] - **tools**: update eslint to 8.50.0 (Node.js GitHub Bot) [#49989](https://github.com/nodejs/node/pull/49989)
* \[[`344d3c4b7c`](https://github.com/nodejs/node/commit/344d3c4b7c)] - **tools**: update lint-md-dependencies (Node.js GitHub Bot) [#49983](https://github.com/nodejs/node/pull/49983)
* \[[`7f06c270c6`](https://github.com/nodejs/node/commit/7f06c270c6)] - **tools**: add navigation ARIA landmark to generated API ToC (Rich Trott) [#49882](https://github.com/nodejs/node/pull/49882)
* \[[`e97d25687b`](https://github.com/nodejs/node/commit/e97d25687b)] - **tools**: use osx notarytool for future releases (Ulises Gascon) [#48701](https://github.com/nodejs/node/pull/48701)
* \[[`3f1936f698`](https://github.com/nodejs/node/commit/3f1936f698)] - **tools**: update github\_reporter to 1.5.3 (Node.js GitHub Bot) [#49877](https://github.com/nodejs/node/pull/49877)
* \[[`8568de3da6`](https://github.com/nodejs/node/commit/8568de3da6)] - **tools**: add new V8 headers to distribution (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`86cb23d09f`](https://github.com/nodejs/node/commit/86cb23d09f)] - **tools**: update V8 gypfiles for 11.8 (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`9c6219c7e2`](https://github.com/nodejs/node/commit/9c6219c7e2)] - **tools**: update V8 gypfiles for 11.7 (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`73ddf50163`](https://github.com/nodejs/node/commit/73ddf50163)] - **tools**: update V8 gypfiles for 11.6 (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`817ef255ea`](https://github.com/nodejs/node/commit/817ef255ea)] - **tools**: update V8 gypfiles for 11.5 (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`f34a3a9861`](https://github.com/nodejs/node/commit/f34a3a9861)] - **tools**: update V8 gypfiles for 11.4 (Michaël Zasso) [#49639](https://github.com/nodejs/node/pull/49639)
* \[[`9df864ddeb`](https://github.com/nodejs/node/commit/9df864ddeb)] - **typings**: use `Symbol.dispose` and `Symbol.asyncDispose` in types (Niklas Mollenhauer) [#50123](https://github.com/nodejs/node/pull/50123)
* \[[`54bb691c0b`](https://github.com/nodejs/node/commit/54bb691c0b)] - **util**: lazy parse mime parameters (Aras Abbasi) [#49889](https://github.com/nodejs/node/pull/49889)
* \[[`1d220b55ac`](https://github.com/nodejs/node/commit/1d220b55ac)] - **vm**: use default HDO when importModuleDynamically is not set (Joyee Cheung) [#49950](https://github.com/nodejs/node/pull/49950)
* \[[`c1a3a98560`](https://github.com/nodejs/node/commit/c1a3a98560)] - **wasi**: address coverity warning (Michael Dawson) [#49866](https://github.com/nodejs/node/pull/49866)
* \[[`9cb8eb7177`](https://github.com/nodejs/node/commit/9cb8eb7177)] - **wasi**: fix up wasi tests for ibmi (Michael Dawson) [#49953](https://github.com/nodejs/node/pull/49953)
* \[[`16ac5e1ca8`](https://github.com/nodejs/node/commit/16ac5e1ca8)] - **zlib**: fix discovery of cpu-features.h for android (MatteoBax) [#49828](https://github.com/nodejs/node/pull/49828)
