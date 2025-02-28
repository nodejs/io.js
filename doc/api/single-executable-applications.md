# Single executable applications

<!--introduced_in=v19.7.0-->

<!-- YAML
added:
  - v19.7.0
  - v18.16.0
changes:
  - version: v20.6.0
    pr-url: https://github.com/nodejs/node/pull/46824
    description: Added support for "useSnapshot".
  - version: v20.6.0
    pr-url: https://github.com/nodejs/node/pull/48191
    description: Added support for "useCodeCache".
-->

> Stability: 1.1 - Active development

<!-- source_link=src/node_sea.cc -->

This feature allows the distribution of a Node.js application conveniently to a
system that does not have Node.js installed.

Node.js supports the creation of [single executable applications][] by allowing
the injection of a blob prepared by Node.js, which can contain a bundled script,
into the `node` binary. During start up, the program checks if anything has been
injected. If the blob is found, it executes the script in the blob. Otherwise
Node.js operates as it normally does.

The single executable application feature currently only supports running a
single embedded script using the [CommonJS][] module system.

Users can create a single executable application from their bundled script
with the `node` binary itself and any tool which can inject resources into the
binary.

Here are the steps for creating a single executable application using one such
tool, [postject][]:

1. Create a JavaScript file:
   ```bash
   echo 'console.log(`Hello, ${process.argv[2]}!`);' > hello.js
   ```

2. Create a configuration file building a blob that can be injected into the
   single executable application (see
   [Generating single executable preparation blobs][] for details):
   ```bash
   echo '{ "main": "hello.js", "output": "sea-prep.blob" }' > sea-config.json
   ```

3. Generate the blob to be injected:
   ```bash
   node --experimental-sea-config sea-config.json
   ```

4. Create a copy of the `node` executable and name it according to your needs:

   * On systems other than Windows:

   ```bash
   cp $(command -v node) hello
   ```

   * On Windows:

   ```text
   node -e "require('fs').copyFileSync(process.execPath, 'hello.exe')"
   ```

   The `.exe` extension is necessary.

5. Remove the signature of the binary (macOS and Windows only):

   * On macOS:

   ```bash
   codesign --remove-signature hello
   ```

   * On Windows (optional):

   [signtool][] can be used from the installed [Windows SDK][]. If this step is
   skipped, ignore any signature-related warning from postject.

   ```powershell
   signtool remove /s hello.exe
   ```

6. Inject the blob into the copied binary by running `postject` with
   the following options:

   * `hello` / `hello.exe` - The name of the copy of the `node` executable
     created in step 4.
   * `NODE_SEA_BLOB` - The name of the resource / note / section in the binary
     where the contents of the blob will be stored.
   * `sea-prep.blob` - The name of the blob created in step 1.
   * `--sentinel-fuse NODE_SEA_FUSE_fce680ab2cc467b6e072b8b5df1996b2` - The
     [fuse][] used by the Node.js project to detect if a file has been injected.
   * `--macho-segment-name NODE_SEA` (only needed on macOS) - The name of the
     segment in the binary where the contents of the blob will be
     stored.

   To summarize, here is the required command for each platform:

   * On Linux:
     ```bash
     npx postject hello NODE_SEA_BLOB sea-prep.blob \
         --sentinel-fuse NODE_SEA_FUSE_fce680ab2cc467b6e072b8b5df1996b2
     ```

   * On Windows - PowerShell:
     ```powershell
     npx postject hello.exe NODE_SEA_BLOB sea-prep.blob `
         --sentinel-fuse NODE_SEA_FUSE_fce680ab2cc467b6e072b8b5df1996b2
     ```

   * On Windows - Command Prompt:
     ```text
     npx postject hello.exe NODE_SEA_BLOB sea-prep.blob ^
         --sentinel-fuse NODE_SEA_FUSE_fce680ab2cc467b6e072b8b5df1996b2
     ```

   * On macOS:
     ```bash
     npx postject hello NODE_SEA_BLOB sea-prep.blob \
         --sentinel-fuse NODE_SEA_FUSE_fce680ab2cc467b6e072b8b5df1996b2 \
         --macho-segment-name NODE_SEA
     ```

7. Sign the binary (macOS and Windows only):

   * On macOS:

   ```bash
   codesign --sign - hello
   ```

   * On Windows (optional):

   A certificate needs to be present for this to work. However, the unsigned
   binary would still be runnable.

   ```powershell
   signtool sign /fd SHA256 hello.exe
   ```

8. Run the binary:

   * On systems other than Windows

   ```console
   $ ./hello world
   Hello, world!
   ```

   * On Windows

   ```console
   $ .\hello.exe world
   Hello, world!
   ```

## Generating single executable preparation blobs

Single executable preparation blobs that are injected into the application can
be generated using the `--experimental-sea-config` flag of the Node.js binary
that will be used to build the single executable. It takes a path to a
configuration file in JSON format. If the path passed to it isn't absolute,
Node.js will use the path relative to the current working directory.

The configuration currently reads the following top-level fields:

```json
{
  "main": "/path/to/bundled/script.js",
  "output": "/path/to/write/the/generated/blob.blob",
  "disableExperimentalSEAWarning": true, // Default: false
  "useSnapshot": false,  // Default: false
  "useCodeCache": true, // Default: false
  "assets": {  // Optional
    "a.dat": "/path/to/a.dat",
    "b.txt": "/path/to/b.txt"
  }
}
```

If the paths are not absolute, Node.js will use the path relative to the
current working directory. The version of the Node.js binary used to produce
the blob must be the same as the one to which the blob will be injected.

Note: When generating cross-platform SEAs (e.g., generating a SEA
for `linux-x64` on `darwin-arm64`), `useCodeCache` and `useSnapshot`
must be set to false to avoid generating incompatible executables.
Since code cache and snapshots can only be loaded on the same platform
where they are compiled, the generated executable might crash on startup when
trying to load code cache or snapshots built on a different platform.

### Assets

Users can include assets by adding a key-path dictionary to the configuration
as the `assets` field. At build time, Node.js would read the assets from the
specified paths and bundle them into the preparation blob. In the generated
executable, users can retrieve the assets using the [`sea.getAsset()`][] and
[`sea.getAssetAsBlob()`][] APIs.

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
const { getAsset, getAssetAsBlob, getRawAsset } = require('node:sea');
// Returns a copy of the data in an ArrayBuffer.
const image = getAsset('a.jpg');
// Returns a string decoded from the asset as UTF8.
const text = getAsset('b.txt', 'utf8');
// Returns a Blob containing the asset.
const blob = getAssetAsBlob('a.jpg');
// Returns an ArrayBuffer containing the raw asset without copying.
const raw = getRawAsset('a.jpg');
```

See documentation of the [`sea.getAsset()`][], [`sea.getAssetAsBlob()`][] and [`sea.getRawAsset()`][]
APIs for more information.

### Startup snapshot support

The `useSnapshot` field can be used to enable startup snapshot support. In this
case, the `main` script would not be executed when the final executable is launched.
Instead, it would be run when the single executable application preparation
blob is generated on the building machine. The generated preparation blob would
then include a snapshot capturing the states initialized by the `main` script.
The final executable, with the preparation blob injected, would deserialize
the snapshot at run time.

When `useSnapshot` is true, the main script must invoke the
[`v8.startupSnapshot.setDeserializeMainFunction()`][] API to configure code
that needs to be run when the final executable is launched by the users.

The typical pattern for an application to use snapshot in a single executable
application is:

1. At build time, on the building machine, the main script is run to
   initialize the heap to a state that's ready to take user input. The script
   should also configure a main function with
   [`v8.startupSnapshot.setDeserializeMainFunction()`][]. This function will be
   compiled and serialized into the snapshot, but not invoked at build time.
2. At run time, the main function will be run on top of the deserialized heap
   on the user machine to process user input and generate output.

The general constraints of the startup snapshot scripts also apply to the main
script when it's used to build snapshot for the single executable application,
and the main script can use the [`v8.startupSnapshot` API][] to adapt to
these constraints. See
[documentation about startup snapshot support in Node.js][].

### V8 code cache support

When `useCodeCache` is set to `true` in the configuration, during the generation
of the single executable preparation blob, Node.js will compile the `main`
script to generate the V8 code cache. The generated code cache would be part of
the preparation blob and get injected into the final executable. When the single
executable application is launched, instead of compiling the `main` script from
scratch, Node.js would use the code cache to speed up the compilation, then
execute the script, which would improve the startup performance.

**Note:** `import()` does not work when `useCodeCache` is `true`.

## In the injected main script

### Single-executable application API

The `node:sea` builtin allows interaction with the single-executable application
from the JavaScript main script embedded into the executable.

#### `sea.isSea()`

<!-- YAML
added:
  - v21.7.0
  - v20.12.0
-->

* Returns: {boolean} Whether this script is running inside a single-executable
  application.

### `sea.getAsset(key[, encoding])`

<!-- YAML
added:
  - v21.7.0
  - v20.12.0
-->

This method can be used to retrieve the assets configured to be bundled into the
single-executable application at build time.
An error is thrown when no matching asset can be found.

* `key`  {string} the key for the asset in the dictionary specified by the
  `assets` field in the single-executable application configuration.
* `encoding` {string} If specified, the asset will be decoded as
  a string. Any encoding supported by the `TextDecoder` is accepted.
  If unspecified, an `ArrayBuffer` containing a copy of the asset would be
  returned instead.
* Returns: {string|ArrayBuffer}

### `sea.getAssetAsBlob(key[, options])`

<!-- YAML
added:
  - v21.7.0
  - v20.12.0
-->

Similar to [`sea.getAsset()`][], but returns the result in a {Blob}.
An error is thrown when no matching asset can be found.

* `key`  {string} the key for the asset in the dictionary specified by the
  `assets` field in the single-executable application configuration.
* `options` {Object}
  * `type` {string} An optional mime type for the blob.
* Returns: {Blob}

### `sea.getRawAsset(key)`

<!-- YAML
added:
  - v21.7.0
  - v20.12.0
-->

This method can be used to retrieve the assets configured to be bundled into the
single-executable application at build time.
An error is thrown when no matching asset can be found.

Unlike `sea.getAsset()` or `sea.getAssetAsBlob()`, this method does not
return a copy. Instead, it returns the raw asset bundled inside the executable.

For now, users should avoid writing to the returned array buffer. If the
injected section is not marked as writable or not aligned properly,
writes to the returned array buffer is likely to result in a crash.

* `key`  {string} the key for the asset in the dictionary specified by the
  `assets` field in the single-executable application configuration.
* Returns: {ArrayBuffer}

### `require(id)` in the injected main script is not file based

`require()` in the injected main script is not the same as the [`require()`][]
available to modules that are not injected. It also does not have any of the
properties that non-injected [`require()`][] has except [`require.main`][]. It
can only be used to load built-in modules. Attempting to load a module that can
only be found in the file system will throw an error.

Instead of relying on a file based `require()`, users can bundle their
application into a standalone JavaScript file to inject into the executable.
This also ensures a more deterministic dependency graph.

However, if a file based `require()` is still needed, that can also be achieved:

```js
const { createRequire } = require('node:module');
require = createRequire(__filename);
```

### `__filename` and `module.filename` in the injected main script

The values of `__filename` and `module.filename` in the injected main script
are equal to [`process.execPath`][].

### `__dirname` in the injected main script

The value of `__dirname` in the injected main script is equal to the directory
name of [`process.execPath`][].

## Notes

### Single executable application creation process

A tool aiming to create a single executable Node.js application must
inject the contents of the blob prepared with `--experimental-sea-config"`
into:

* a resource named `NODE_SEA_BLOB` if the `node` binary is a [PE][] file
* a section named `NODE_SEA_BLOB` in the `NODE_SEA` segment if the `node` binary
  is a [Mach-O][] file
* a note named `NODE_SEA_BLOB` if the `node` binary is an [ELF][] file

Search the binary for the
`NODE_SEA_FUSE_fce680ab2cc467b6e072b8b5df1996b2:0` [fuse][] string and flip the
last character to `1` to indicate that a resource has been injected.

### Platform support

Single-executable support is tested regularly on CI only on the following
platforms:

* Windows
* macOS
* Linux (all distributions [supported by Node.js][] except Alpine and all
  architectures [supported by Node.js][] except s390x)

This is due to a lack of better tools to generate single-executables that can be
used to test this feature on other platforms.

Suggestions for other resource injection tools/workflows are welcomed. Please
start a discussion at <https://github.com/nodejs/single-executable/discussions>
to help us document them.

[CommonJS]: modules.md#modules-commonjs-modules
[ELF]: https://en.wikipedia.org/wiki/Executable_and_Linkable_Format
[Generating single executable preparation blobs]: #generating-single-executable-preparation-blobs
[Mach-O]: https://en.wikipedia.org/wiki/Mach-O
[PE]: https://en.wikipedia.org/wiki/Portable_Executable
[Windows SDK]: https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/
[`process.execPath`]: process.md#processexecpath
[`require()`]: modules.md#requireid
[`require.main`]: modules.md#accessing-the-main-module
[`sea.getAsset()`]: #seagetassetkey-encoding
[`sea.getAssetAsBlob()`]: #seagetassetasblobkey-options
[`sea.getRawAsset()`]: #seagetrawassetkey
[`v8.startupSnapshot.setDeserializeMainFunction()`]: v8.md#v8startupsnapshotsetdeserializemainfunctioncallback-data
[`v8.startupSnapshot` API]: v8.md#startup-snapshot-api
[documentation about startup snapshot support in Node.js]: cli.md#--build-snapshot
[fuse]: https://www.electronjs.org/docs/latest/tutorial/fuses
[postject]: https://github.com/nodejs/postject
[signtool]: https://learn.microsoft.com/en-us/windows/win32/seccrypto/signtool
[single executable applications]: https://github.com/nodejs/single-executable
[supported by Node.js]: https://github.com/nodejs/node/blob/main/BUILDING.md#platform-list
