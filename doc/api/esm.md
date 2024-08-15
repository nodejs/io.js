# Modules: ECMAScript modules

<!--introduced_in=v8.5.0-->

<!-- type=misc -->

<!-- YAML
added: v8.5.0
changes:
  - version:
    - v21.0.0
    - v20.10.0
    - v18.20.0
    pr-url: https://github.com/nodejs/node/pull/50140
    description: Add experimental support for import attributes.
  - version:
    - v20.0.0
    - v18.19.0
    pr-url: https://github.com/nodejs/node/pull/44710
    description: Module customization hooks are executed off the main thread.
  - version:
    - v18.6.0
    - v16.17.0
    pr-url: https://github.com/nodejs/node/pull/42623
    description: Add support for chaining module customization hooks.
  - version:
    - v17.1.0
    - v16.14.0
    pr-url: https://github.com/nodejs/node/pull/40250
    description: Add experimental support for import assertions.
  - version:
    - v17.0.0
    - v16.12.0
    pr-url: https://github.com/nodejs/node/pull/37468
    description:
      Consolidate customization hooks, removed `getFormat`, `getSource`,
      `transformSource`, and `getGlobalPreloadCode` hooks
      added `load` and `globalPreload` hooks
      allowed returning `format` from either `resolve` or `load` hooks.
  - version:
    - v15.3.0
    - v14.17.0
    - v12.22.0
    pr-url: https://github.com/nodejs/node/pull/35781
    description: Stabilize modules implementation.
  - version:
    - v14.13.0
    - v12.20.0
    pr-url: https://github.com/nodejs/node/pull/35249
    description: Support for detection of CommonJS named exports.
  - version: v14.8.0
    pr-url: https://github.com/nodejs/node/pull/34558
    description: Unflag Top-Level Await.
  - version:
    - v14.0.0
    - v13.14.0
    - v12.20.0
    pr-url: https://github.com/nodejs/node/pull/31974
    description: Remove experimental modules warning.
  - version:
    - v13.2.0
    - v12.17.0
    pr-url: https://github.com/nodejs/node/pull/29866
    description: Loading ECMAScript modules no longer requires a command-line flag.
  - version: v12.0.0
    pr-url: https://github.com/nodejs/node/pull/26745
    description:
      Add support for ES modules using `.js` file extension via `package.json`
      `"type"` field.
-->

> Stability: 2 - Stable

## Introduction

<!--name=esm-->

ECMAScript modules are [the official standard format][] to package JavaScript
code for reuse. Modules are defined using a variety of [`import`][] and
[`export`][] statements.

The following example of an ES module exports a function:

```js
// addTwo.mjs
function addTwo(num) {
  return num + 2;
}

export { addTwo };
```

The following example of an ES module imports the function from `addTwo.mjs`:

```js
// app.mjs
import { addTwo } from './addTwo.mjs';

// Prints: 6
console.log(addTwo(4));
```

Node.js fully supports ECMAScript modules as they are currently specified and
provides interoperability between them and its original module format,
[CommonJS][].

<!-- Anchors to make sure old links find a target -->

<i id="esm_package_json_type_field"></i><i id="esm_package_scope_and_file_extensions"></i><i id="esm_input_type_flag"></i>

## Enabling

<!-- type=misc -->

Node.js has two module systems: [CommonJS][] modules and ECMAScript modules.

Authors can tell Node.js to interpret JavaScript as an ES module via the `.mjs`
file extension, the `package.json` [`"type"`][] field with a value `"module"`,
the [`--input-type`][] flag with a value of `"module"`, or the
[`--experimental-default-type`][] flag with a value of `"module"`. These are
explicit markers of code being intended to run as an ES module.

Inversely, authors can tell Node.js to interpret JavaScript as CommonJS via the
`.cjs` file extension, the `package.json` [`"type"`][] field with a value
`"commonjs"`, the [`--input-type`][] flag with a value of `"commonjs"`, or the
[`--experimental-default-type`][] flag with a value of `"commonjs"`.

When code lacks explicit markers for either module system, Node.js will inspect
the source code of a module to look for ES module syntax. If such syntax is
found, Node.js will run the code as an ES module; otherwise it will run the
module as CommonJS. See [Determining module system][] for more details.

<!-- Anchors to make sure old links find a target -->

<i id="esm_package_entry_points"></i><i id="esm_main_entry_point_export"></i><i id="esm_subpath_exports"></i><i id="esm_package_exports_fallbacks"></i><i id="esm_exports_sugar"></i><i id="esm_conditional_exports"></i><i id="esm_nested_conditions"></i><i id="esm_self_referencing_a_package_using_its_name"></i><i id="esm_internal_package_imports"></i><i id="esm_dual_commonjs_es_module_packages"></i><i id="esm_dual_package_hazard"></i><i id="esm_writing_dual_packages_while_avoiding_or_minimizing_hazards"></i><i id="esm_approach_1_use_an_es_module_wrapper"></i><i id="esm_approach_2_isolate_state"></i>

## Packages

This section was moved to [Modules: Packages](packages.md).

## `import` Specifiers

### Terminology

The _specifier_ of an `import` statement is the string after the `from` keyword,
e.g. `'node:path'` in `import { sep } from 'node:path'`. Specifiers are also
used in `export from` statements, and as the argument to an `import()`
expression.

There are three types of specifiers:

* _Relative specifiers_ like `'./startup.js'` or `'../config.mjs'`. They refer
  to a path relative to the location of the importing file. _The file extension
  is always necessary for these._

* _Bare specifiers_ like `'some-package'` or `'some-package/shuffle'`. They can
  refer to the main entry point of a package by the package name, or a
  specific feature module within a package prefixed by the package name as per
  the examples respectively. _Including the file extension is only necessary
  for packages without an [`"exports"`][] field._

* _Absolute specifiers_ like `'file:///opt/nodejs/config.js'`. They refer
  directly and explicitly to a full path.

Bare specifier resolutions are handled by the [Node.js module
resolution and loading algorithm][].
All other specifier resolutions are always only resolved with
the standard relative [URL][] resolution semantics.

Like in CommonJS, module files within packages can be accessed by appending a
path to the package name unless the package's [`package.json`][] contains an
[`"exports"`][] field, in which case files within packages can only be accessed
via the paths defined in [`"exports"`][].

For details on these package resolution rules that apply to bare specifiers in
the Node.js module resolution, see the [packages documentation](packages.md).

### Mandatory file extensions

A file extension must be provided when using the `import` keyword to resolve
relative or absolute specifiers. Directory indexes (e.g. `'./startup/index.js'`)
must also be fully specified.

This behavior matches how `import` behaves in browser environments, assuming a
typically configured server.

### URLs

ES modules are resolved and cached as URLs. This means that special characters
must be [percent-encoded][], such as `#` with `%23` and `?` with `%3F`.

`file:`, `node:`, and `data:` URL schemes are supported. A specifier like
`'https://example.com/app.js'` is not supported natively in Node.js unless using
a [custom HTTPS loader][].

#### `file:` URLs

Modules are loaded multiple times if the `import` specifier used to resolve
them has a different query or fragment.

```js
import './foo.mjs?query=1'; // loads ./foo.mjs with query of "?query=1"
import './foo.mjs?query=2'; // loads ./foo.mjs with query of "?query=2"
```

The volume root may be referenced via `/`, `//`, or `file:///`. Given the
differences between [URL][] and path resolution (such as percent encoding
details), it is recommended to use [url.pathToFileURL][] when importing a path.

#### `data:` imports

<!-- YAML
added: v12.10.0
-->

[`data:` URLs][] are supported for importing with the following MIME types:

* `text/javascript` for ES modules
* `application/json` for JSON
* `application/wasm` for Wasm

```js
import 'data:text/javascript,console.log("hello!");';
import _ from 'data:application/json,"world!"' with { type: 'json' };
```

`data:` URLs only resolve [bare specifiers][Terminology] for builtin modules
and [absolute specifiers][Terminology]. Resolving
[relative specifiers][Terminology] does not work because `data:` is not a
[special scheme][]. For example, attempting to load `./foo`
from `data:text/javascript,import "./foo";` fails to resolve because there
is no concept of relative resolution for `data:` URLs.

#### `node:` imports

<!-- YAML
added:
  - v14.13.1
  - v12.20.0
changes:
  - version:
      - v16.0.0
      - v14.18.0
    pr-url: https://github.com/nodejs/node/pull/37246
    description: Added `node:` import support to `require(...)`.
-->

`node:` URLs are supported as an alternative means to load Node.js builtin
modules. This URL scheme allows for builtin modules to be referenced by valid
absolute URL strings.

```js
import fs from 'node:fs/promises';
```

<a id="import-assertions"></a>

## Import attributes

<!-- YAML
added:
  - v17.1.0
  - v16.14.0
changes:
  - version:
    - v21.0.0
    - v20.10.0
    - v18.20.0
    pr-url: https://github.com/nodejs/node/pull/50140
    description: Switch from Import Assertions to Import Attributes.
-->

> Stability: 1.1 - Active development

> This feature was previously named "Import assertions", and using the `assert`
> keyword instead of `with`. Any uses in code of the prior `assert` keyword
> should be updated to use `with` instead.

The [Import Attributes proposal][] adds an inline syntax for module import
statements to pass on more information alongside the module specifier.

```js
import fooData from './foo.json' with { type: 'json' };

const { default: barData } =
  await import('./bar.json', { with: { type: 'json' } });
```

Node.js supports the following `type` values, for which the attribute is
mandatory:

| Attribute `type` | Needed for       |
| ---------------- | ---------------- |
| `'json'`         | [JSON modules][] |

## Built-in modules

[Built-in modules][] provide named exports of their public API. A
default export is also provided which is the value of the CommonJS exports.
The default export can be used for, among other things, modifying the named
exports. Named exports of built-in modules are updated only by calling
[`module.syncBuiltinESMExports()`][].

```js
import EventEmitter from 'node:events';
const e = new EventEmitter();
```

```js
import { readFile } from 'node:fs';
readFile('./foo.txt', (err, source) => {
  if (err) {
    console.error(err);
  } else {
    console.log(source);
  }
});
```

```js
import fs, { readFileSync } from 'node:fs';
import { syncBuiltinESMExports } from 'node:module';
import { Buffer } from 'node:buffer';

fs.readFileSync = () => Buffer.from('Hello, ESM');
syncBuiltinESMExports();

fs.readFileSync === readFileSync;
```

## `import()` expressions

[Dynamic `import()`][] is supported in both CommonJS and ES modules. In CommonJS
modules it can be used to load ES modules.

## `import.meta`

* {Object}

The `import.meta` meta property is an `Object` that contains the following
properties.

### `import.meta.dirname`

<!-- YAML
added:
  - v21.2.0
  - v20.11.0
-->

> Stability: 1.2 - Release candidate

* {string} The directory name of the current module. This is the same as the
  [`path.dirname()`][] of the [`import.meta.filename`][].

> **Caveat**: only present on `file:` modules.

### `import.meta.filename`

<!-- YAML
added:
  - v21.2.0
  - v20.11.0
-->

> Stability: 1.2 - Release candidate

* {string} The full absolute path and filename of the current module, with
  symlinks resolved.
* This is the same as the [`url.fileURLToPath()`][] of the
  [`import.meta.url`][].

> **Caveat** only local modules support this property. Modules not using the
> `file:` protocol will not provide it.

### `import.meta.url`

* {string} The absolute `file:` URL of the module.

This is defined exactly the same as it is in browsers providing the URL of the
current module file.

This enables useful patterns such as relative file loading:

```js
import { readFileSync } from 'node:fs';
const buffer = readFileSync(new URL('./data.proto', import.meta.url));
```

### `import.meta.resolve(specifier)`

<!-- YAML
added:
  - v13.9.0
  - v12.16.2
changes:
  - version:
    - v20.6.0
    - v18.19.0
    pr-url: https://github.com/nodejs/node/pull/49028
    description: Unflag `import.meta.resolve`, with `parentURL` parameter still
                 flagged.
  - version:
    - v20.6.0
    - v18.19.0
    pr-url: https://github.com/nodejs/node/pull/49038
    description: This API no longer throws when targeting `file:` URLs that do
                 not map to an existing file on the local FS.
  - version:
    - v20.0.0
    - v18.19.0
    pr-url: https://github.com/nodejs/node/pull/44710
    description: This API now returns a string synchronously instead of a Promise.
  - version:
      - v16.2.0
      - v14.18.0
    pr-url: https://github.com/nodejs/node/pull/38587
    description: Add support for WHATWG `URL` object to `parentURL` parameter.
-->

> Stability: 1.2 - Release candidate

* `specifier` {string} The module specifier to resolve relative to the
  current module.
* Returns: {string} The absolute URL string that the specifier would resolve to.

[`import.meta.resolve`][] is a module-relative resolution function scoped to
each module, returning the URL string.

```js
const dependencyAsset = import.meta.resolve('component-lib/asset.css');
// file:///app/node_modules/component-lib/asset.css
import.meta.resolve('./dep.js');
// file:///app/dep.js
```

All features of the Node.js module resolution are supported. Dependency
resolutions are subject to the permitted exports resolutions within the package.

**Caveats**:

* This can result in synchronous file-system operations, which
  can impact performance similarly to `require.resolve`.
* This feature is not available within custom loaders (it would
  create a deadlock).

**Non-standard API**:

When using the `--experimental-import-meta-resolve` flag, that function accepts
a second argument:

* `parent` {string|URL} An optional absolute parent module URL to resolve from.
  **Default:** `import.meta.url`

## Interoperability with CommonJS

### `import` statements

An `import` statement can reference an ES module or a CommonJS module.
`import` statements are permitted only in ES modules, but dynamic [`import()`][]
expressions are supported in CommonJS for loading ES modules.

When importing [CommonJS modules](#commonjs-namespaces), the
`module.exports` object is provided as the default export. Named exports may be
available, provided by static analysis as a convenience for better ecosystem
compatibility.

### `require`

The CommonJS module `require` currently only supports loading synchronous ES
modules when `--experimental-require-module` is enabled.

See [Loading ECMAScript modules using `require()`][] for details.

### CommonJS Namespaces

CommonJS modules consist of a `module.exports` object which can be of any type.

When importing a CommonJS module, it can be reliably imported using the ES
module default import or its corresponding sugar syntax:

<!-- eslint-disable no-duplicate-imports -->

```js
import { default as cjs } from 'cjs';

// The following import statement is "syntax sugar" (equivalent but sweeter)
// for `{ default as cjsSugar }` in the above import statement:
import cjsSugar from 'cjs';

console.log(cjs);
console.log(cjs === cjsSugar);
// Prints:
//   <module.exports>
//   true
```

The ECMAScript Module Namespace representation of a CommonJS module is always
a namespace with a `default` export key pointing to the CommonJS
`module.exports` value.

This Module Namespace Exotic Object can be directly observed either when using
`import * as m from 'cjs'` or a dynamic import:

<!-- eslint-skip -->

```js
import * as m from 'cjs';
console.log(m);
console.log(m === await import('cjs'));
// Prints:
//   [Module] { default: <module.exports> }
//   true
```

For better compatibility with existing usage in the JS ecosystem, Node.js
in addition attempts to determine the CommonJS named exports of every imported
CommonJS module to provide them as separate ES module exports using a static
analysis process.

For example, consider a CommonJS module written:

```cjs
// cjs.cjs
exports.name = 'exported';
```

The preceding module supports named imports in ES modules:

<!-- eslint-disable no-duplicate-imports -->

```js
import { name } from './cjs.cjs';
console.log(name);
// Prints: 'exported'

import cjs from './cjs.cjs';
console.log(cjs);
// Prints: { name: 'exported' }

import * as m from './cjs.cjs';
console.log(m);
// Prints: [Module] { default: { name: 'exported' }, name: 'exported' }
```

As can be seen from the last example of the Module Namespace Exotic Object being
logged, the `name` export is copied off of the `module.exports` object and set
directly on the ES module namespace when the module is imported.

Live binding updates or new exports added to `module.exports` are not detected
for these named exports.

The detection of named exports is based on common syntax patterns but does not
always correctly detect named exports. In these cases, using the default
import form described above can be a better option.

Named exports detection covers many common export patterns, reexport patterns
and build tool and transpiler outputs. See [cjs-module-lexer][] for the exact
semantics implemented.

### Differences between ES modules and CommonJS

#### No `require`, `exports`, or `module.exports`

In most cases, the ES module `import` can be used to load CommonJS modules.

If needed, a `require` function can be constructed within an ES module using
[`module.createRequire()`][].

#### No `__filename` or `__dirname`

These CommonJS variables are not available in ES modules.

`__filename` and `__dirname` use cases can be replicated via
[`import.meta.filename`][] and [`import.meta.dirname`][].

#### No Addon Loading

[Addons][] are not currently supported with ES module imports.

They can instead be loaded with [`module.createRequire()`][] or
[`process.dlopen`][].

#### No `require.resolve`

Relative resolution can be handled via `new URL('./local', import.meta.url)`.

For a complete `require.resolve` replacement, there is the
[import.meta.resolve][] API.

Alternatively `module.createRequire()` can be used.

#### No `NODE_PATH`

`NODE_PATH` is not part of resolving `import` specifiers. Please use symlinks
if this behavior is desired.

#### No `require.extensions`

`require.extensions` is not used by `import`. Module customization hooks can
provide a replacement.

#### No `require.cache`

`require.cache` is not used by `import` as the ES module loader has its own
separate cache.

<i id="esm_experimental_json_modules"></i>

## JSON modules

> Stability: 1 - Experimental

JSON files can be referenced by `import`:

```js
import packageConfig from './package.json' with { type: 'json' };
```

The `with { type: 'json' }` syntax is mandatory; see [Import Attributes][].

The imported JSON only exposes a `default` export. There is no support for named
exports. A cache entry is created in the CommonJS cache to avoid duplication.
The same object is returned in CommonJS if the JSON module has already been
imported from the same path.

<i id="esm_experimental_wasm_modules"></i>

## Wasm modules

> Stability: 1 - Experimental

Importing WebAssembly modules is supported under the
`--experimental-wasm-modules` flag, allowing any `.wasm` files to be
imported as normal modules while also supporting their module imports.

This integration is in line with the
[ES Module Integration Proposal for WebAssembly][].

For example, an `index.mjs` containing:

```js
import * as M from './module.wasm';
console.log(M);
```

executed under:

```bash
node --experimental-wasm-modules index.mjs
```

would provide the exports interface for the instantiation of `module.wasm`.

<i id="esm_experimental_top_level_await"></i>

## Top-level `await`

<!-- YAML
added: v14.8.0
-->

The `await` keyword may be used in the top level body of an ECMAScript module.

Assuming an `a.mjs` with

```js
export const five = await Promise.resolve(5);
```

And a `b.mjs` with

```js
import { five } from './a.mjs';

console.log(five); // Logs `5`
```

```bash
node b.mjs # works
```

If a top level `await` expression never resolves, the `node` process will exit
with a `13` [status code][].

```js
import { spawn } from 'node:child_process';
import { execPath } from 'node:process';

spawn(execPath, [
  '--input-type=module',
  '--eval',
  // Never-resolving Promise:
  'await new Promise(() => {})',
]).once('exit', (code) => {
  console.log(code); // Logs `13`
});
```

<i id="esm_experimental_loaders"></i>

## Loaders

The former Loaders documentation is now at
[Modules: Customization hooks][Module customization hooks].

## Resolution and loading algorithm

### Features

The default resolver has the following properties:

* FileURL-based resolution as is used by ES modules
* Relative and absolute URL resolution
* No default extensions
* No folder mains
* Bare specifier package resolution lookup through node\_modules
* Does not fail on unknown extensions or protocols
* Can optionally provide a hint of the format to the loading phase

The default loader has the following properties

* Support for builtin module loading via `node:` URLs
* Support for "inline" module loading via `data:` URLs
* Support for `file:` module loading
* Fails on any other URL protocol
* Fails on unknown extensions for `file:` loading
  (supports only `.cjs`, `.js`, and `.mjs`)

### Resolution algorithm

The algorithm to load an ES module specifier is given through the
**ESM\_RESOLVE** method below. It returns the resolved URL for a
module specifier relative to a parentURL.

The resolution algorithm determines the full resolved URL for a module
load, along with its suggested module format. The resolution algorithm
does not determine whether the resolved URL protocol can be loaded,
or whether the file extensions are permitted, instead these validations
are applied by Node.js during the load phase
(for example, if it was asked to load a URL that has a protocol that is
not `file:`, `data:` or `node:`.

The algorithm also tries to determine the format of the file based
on the extension (see `ESM_FILE_FORMAT` algorithm below). If it does
not recognize the file extension (eg if it is not `.mjs`, `.cjs`, or
`.json`), then a format of `undefined` is returned,
which will throw during the load phase.

The algorithm to determine the module format of a resolved URL is
provided by **ESM\_FILE\_FORMAT**, which returns the unique module
format for any file. The _"module"_ format is returned for an ECMAScript
Module, while the _"commonjs"_ format is used to indicate loading through the
legacy CommonJS loader. Additional formats such as _"addon"_ can be extended in
future updates.

In the following algorithms, all subroutine errors are propagated as errors
of these top-level routines unless stated otherwise.

_defaultConditions_ is the conditional environment name array,
`["node", "import"]`.

The resolver can throw the following errors:

* _Invalid Module Specifier_: Module specifier is an invalid URL, package name
  or package subpath specifier.
* _Invalid Package Configuration_: package.json configuration is invalid or
  contains an invalid configuration.
* _Invalid Package Target_: Package exports or imports define a target module
  for the package that is an invalid type or string target.
* _Package Path Not Exported_: Package exports do not define or permit a target
  subpath in the package for the given module.
* _Package Import Not Defined_: Package imports do not define the specifier.
* _Module Not Found_: The package or module requested does not exist.
* _Unsupported Directory Import_: The resolved path corresponds to a directory,
  which is not a supported target for module imports.

### Resolution Algorithm Specification

**ESM\_RESOLVE**(_specifier_, _parentURL_)

> 1. Let _resolved_ be **undefined**.
> 2. If _specifier_ is a valid URL, then
>    1. Set _resolved_ to the result of parsing and reserializing
>       _specifier_ as a URL.
> 3. Otherwise, if _specifier_ starts with _"/"_, _"./"_, or _"../"_, then
>    1. Set _resolved_ to the URL resolution of _specifier_ relative to
>       _parentURL_.
> 4. Otherwise, if _specifier_ starts with _"#"_, then
>    1. Set _resolved_ to the result of
>       **PACKAGE\_IMPORTS\_RESOLVE**(_specifier_,
>       _parentURL_, _defaultConditions_).
> 5. Otherwise,
>    1. Note: _specifier_ is now a bare specifier.
>    2. Set _resolved_ the result of
>       **PACKAGE\_RESOLVE**(_specifier_, _parentURL_).
> 6. Let _format_ be **undefined**.
> 7. If _resolved_ is a _"file:"_ URL, then
>    1. If _resolved_ contains any percent encodings of _"/"_ or _"\\"_ (_"%2F"_
>       and _"%5C"_ respectively), then
>       1. Throw an _Invalid Module Specifier_ error.
>    2. If the file at _resolved_ is a directory, then
>       1. Throw an _Unsupported Directory Import_ error.
>    3. If the file at _resolved_ does not exist, then
>       1. Throw a _Module Not Found_ error.
>    4. Set _resolved_ to the real path of _resolved_, maintaining the
>       same URL querystring and fragment components.
>    5. Set _format_ to the result of **ESM\_FILE\_FORMAT**(_resolved_).
> 8. Otherwise,
>    1. Set _format_ the module format of the content type associated with the
>       URL _resolved_.
> 9. Return _format_ and _resolved_ to the loading phase

**PACKAGE\_RESOLVE**(_packageSpecifier_, _parentURL_)

> 1. Let _packageName_ be **undefined**.
> 2. If _packageSpecifier_ is an empty string, then
>    1. Throw an _Invalid Module Specifier_ error.
> 3. If _packageSpecifier_ is a Node.js builtin module name, then
>    1. Return the string _"node:"_ concatenated with _packageSpecifier_.
> 4. If _packageSpecifier_ does not start with _"@"_, then
>    1. Set _packageName_ to the substring of _packageSpecifier_ until the first
>       _"/"_ separator or the end of the string.
> 5. Otherwise,
>    1. If _packageSpecifier_ does not contain a _"/"_ separator, then
>       1. Throw an _Invalid Module Specifier_ error.
>    2. Set _packageName_ to the substring of _packageSpecifier_
>       until the second _"/"_ separator or the end of the string.
> 6. If _packageName_ starts with _"."_ or contains _"\\"_ or _"%"_, then
>    1. Throw an _Invalid Module Specifier_ error.
> 7. Let _packageSubpath_ be _"."_ concatenated with the substring of
>    _packageSpecifier_ from the position at the length of _packageName_.
> 8. If _packageSubpath_ ends in _"/"_, then
>    1. Throw an _Invalid Module Specifier_ error.
> 9. Let _selfUrl_ be the result of
>    **PACKAGE\_SELF\_RESOLVE**(_packageName_, _packageSubpath_, _parentURL_).
> 10. If _selfUrl_ is not **undefined**, return _selfUrl_.
> 11. While _parentURL_ is not the file system root,
>     1. Let _packageURL_ be the URL resolution of _"node\_modules/"_
>        concatenated with _packageSpecifier_, relative to _parentURL_.
>     2. Set _parentURL_ to the parent folder URL of _parentURL_.
>     3. If the folder at _packageURL_ does not exist, then
>        1. Continue the next loop iteration.
>     4. Let _pjson_ be the result of **READ\_PACKAGE\_JSON**(_packageURL_).
>     5. If _pjson_ is not **null** and _pjson_._exports_ is not **null** or
>        **undefined**, then
>        1. Return the result of **PACKAGE\_EXPORTS\_RESOLVE**(_packageURL_,
>           _packageSubpath_, _pjson.exports_, _defaultConditions_).
>     6. Otherwise, if _packageSubpath_ is equal to _"."_, then
>        1. If _pjson.main_ is a string, then
>           1. Return the URL resolution of _main_ in _packageURL_.
>     7. Otherwise,
>        1. Return the URL resolution of _packageSubpath_ in _packageURL_.
> 12. Throw a _Module Not Found_ error.

**PACKAGE\_SELF\_RESOLVE**(_packageName_, _packageSubpath_, _parentURL_)

> 1. Let _packageURL_ be the result of **LOOKUP\_PACKAGE\_SCOPE**(_parentURL_).
> 2. If _packageURL_ is **null**, then
>    1. Return **undefined**.
> 3. Let _pjson_ be the result of **READ\_PACKAGE\_JSON**(_packageURL_).
> 4. If _pjson_ is **null** or if _pjson_._exports_ is **null** or
>    **undefined**, then
>    1. Return **undefined**.
> 5. If _pjson.name_ is equal to _packageName_, then
>    1. Return the result of **PACKAGE\_EXPORTS\_RESOLVE**(_packageURL_,
>       _packageSubpath_, _pjson.exports_, _defaultConditions_).
> 6. Otherwise, return **undefined**.

**PACKAGE\_EXPORTS\_RESOLVE**(_packageURL_, _subpath_, _exports_, _conditions_)

> 1. If _exports_ is an Object with both a key starting with _"."_ and a key not
>    starting with _"."_, throw an _Invalid Package Configuration_ error.
> 2. If _subpath_ is equal to _"."_, then
>    1. Let _mainExport_ be **undefined**.
>    2. If _exports_ is a String or Array, or an Object containing no keys
>       starting with _"."_, then
>       1. Set _mainExport_ to _exports_.
>    3. Otherwise if _exports_ is an Object containing a _"."_ property, then
>       1. Set _mainExport_ to _exports_\[_"."_].
>    4. If _mainExport_ is not **undefined**, then
>       1. Let _resolved_ be the result of **PACKAGE\_TARGET\_RESOLVE**(
>          _packageURL_, _mainExport_, **null**, **false**, _conditions_).
>       2. If _resolved_ is not **null** or **undefined**, return _resolved_.
> 3. Otherwise, if _exports_ is an Object and all keys of _exports_ start with
>    _"."_, then
>    1. Assert: _subpath_ begins with _"./"_.
>    2. Let _resolved_ be the result of **PACKAGE\_IMPORTS\_EXPORTS\_RESOLVE**(
>       _subpath_, _exports_, _packageURL_, **false**, _conditions_).
>    3. If _resolved_ is not **null** or **undefined**, return _resolved_.
> 4. Throw a _Package Path Not Exported_ error.

**PACKAGE\_IMPORTS\_RESOLVE**(_specifier_, _parentURL_, _conditions_)

> 1. Assert: _specifier_ begins with _"#"_.
> 2. If _specifier_ is exactly equal to _"#"_ or starts with _"#/"_, then
>    1. Throw an _Invalid Module Specifier_ error.
> 3. Let _packageURL_ be the result of **LOOKUP\_PACKAGE\_SCOPE**(_parentURL_).
> 4. If _packageURL_ is not **null**, then
>    1. Let _pjson_ be the result of **READ\_PACKAGE\_JSON**(_packageURL_).
>    2. If _pjson.imports_ is a non-null Object, then
>       1. Let _resolved_ be the result of
>          **PACKAGE\_IMPORTS\_EXPORTS\_RESOLVE**(
>          _specifier_, _pjson.imports_, _packageURL_, **true**, _conditions_).
>       2. If _resolved_ is not **null** or **undefined**, return _resolved_.
> 5. Throw a _Package Import Not Defined_ error.

**PACKAGE\_IMPORTS\_EXPORTS\_RESOLVE**(_matchKey_, _matchObj_, _packageURL_,
_isImports_, _conditions_)

> 1. If _matchKey_ is a key of _matchObj_ and does not contain _"\*"_, then
>    1. Let _target_ be the value of _matchObj_\[_matchKey_].
>    2. Return the result of **PACKAGE\_TARGET\_RESOLVE**(_packageURL_,
>       _target_, **null**, _isImports_, _conditions_).
> 2. Let _expansionKeys_ be the list of keys of _matchObj_ containing only a
>    single _"\*"_, sorted by the sorting function **PATTERN\_KEY\_COMPARE**
>    which orders in descending order of specificity.
> 3. For each key _expansionKey_ in _expansionKeys_, do
>    1. Let _patternBase_ be the substring of _expansionKey_ up to but excluding
>       the first _"\*"_ character.
>    2. If _matchKey_ starts with but is not equal to _patternBase_, then
>       1. Let _patternTrailer_ be the substring of _expansionKey_ from the
>          index after the first _"\*"_ character.
>       2. If _patternTrailer_ has zero length, or if _matchKey_ ends with
>          _patternTrailer_ and the length of _matchKey_ is greater than or
>          equal to the length of _expansionKey_, then
>          1. Let _target_ be the value of _matchObj_\[_expansionKey_].
>          2. Let _patternMatch_ be the substring of _matchKey_ starting at the
>             index of the length of _patternBase_ up to the length of
>             _matchKey_ minus the length of _patternTrailer_.
>          3. Return the result of **PACKAGE\_TARGET\_RESOLVE**(_packageURL_,
>             _target_, _patternMatch_, _isImports_, _conditions_).
> 4. Return **null**.

**PATTERN\_KEY\_COMPARE**(_keyA_, _keyB_)

> 1. Assert: _keyA_ contains only a single _"\*"_.
> 2. Assert: _keyB_ contains only a single _"\*"_.
> 3. Let _baseLengthA_ be the index of _"\*"_ in _keyA_.
> 4. Let _baseLengthB_ be the index of _"\*"_ in _keyB_.
> 5. If _baseLengthA_ is greater than _baseLengthB_, return -1.
> 6. If _baseLengthB_ is greater than _baseLengthA_, return 1.
> 7. If the length of _keyA_ is greater than the length of _keyB_, return -1.
> 8. If the length of _keyB_ is greater than the length of _keyA_, return 1.
> 9. Return 0.

**PACKAGE\_TARGET\_RESOLVE**(_packageURL_, _target_, _patternMatch_,
_isImports_, _conditions_)

> 1. If _target_ is a String, then
>    1. If _target_ does not start with _"./"_, then
>       1. If _isImports_ is **false**, or if _target_ starts with _"../"_ or
>          _"/"_, or if _target_ is a valid URL, then
>          1. Throw an _Invalid Package Target_ error.
>       2. If _patternMatch_ is a String, then
>          1. Return **PACKAGE\_RESOLVE**(_target_ with every instance of _"\*"_
>             replaced by _patternMatch_, _packageURL_ + _"/"_).
>       3. Return **PACKAGE\_RESOLVE**(_target_, _packageURL_ + _"/"_).
>    2. If _target_ split on _"/"_ or _"\\"_ contains any _""_, _"."_, _".."_,
>       or _"node\_modules"_ segments after the first _"."_ segment, case
>       insensitive and including percent encoded variants, throw an _Invalid
>       Package Target_ error.
>    3. Let _resolvedTarget_ be the URL resolution of the concatenation of
>       _packageURL_ and _target_.
>    4. Assert: _packageURL_ is contained in _resolvedTarget_.
>    5. If _patternMatch_ is **null**, then
>       1. Return _resolvedTarget_.
>    6. If _patternMatch_ split on _"/"_ or _"\\"_ contains any _""_, _"."_,
>       _".."_, or _"node\_modules"_ segments, case insensitive and including
>       percent encoded variants, throw an _Invalid Module Specifier_ error.
>    7. Return the URL resolution of _resolvedTarget_ with every instance of
>       _"\*"_ replaced with _patternMatch_.
> 2. Otherwise, if _target_ is a non-null Object, then
>    1. If _target_ contains any index property keys, as defined in ECMA-262
>       [6.1.7 Array Index][], throw an _Invalid Package Configuration_ error.
>    2. For each property _p_ of _target_, in object insertion order as,
>       1. If _p_ equals _"default"_ or _conditions_ contains an entry for _p_,
>          then
>          1. Let _targetValue_ be the value of the _p_ property in _target_.
>          2. Let _resolved_ be the result of **PACKAGE\_TARGET\_RESOLVE**(
>             _packageURL_, _targetValue_, _patternMatch_, _isImports_,
>             _conditions_).
>          3. If _resolved_ is equal to **undefined**, continue the loop.
>          4. Return _resolved_.
>    3. Return **undefined**.
> 3. Otherwise, if _target_ is an Array, then
>    1. If \_target.length is zero, return **null**.
>    2. For each item _targetValue_ in _target_, do
>       1. Let _resolved_ be the result of **PACKAGE\_TARGET\_RESOLVE**(
>          _packageURL_, _targetValue_, _patternMatch_, _isImports_,
>          _conditions_), continuing the loop on any _Invalid Package Target_
>          error.
>       2. If _resolved_ is **undefined**, continue the loop.
>       3. Return _resolved_.
>    3. Return or throw the last fallback resolution **null** return or error.
> 4. Otherwise, if _target_ is _null_, return **null**.
> 5. Otherwise throw an _Invalid Package Target_ error.

**ESM\_FILE\_FORMAT**(_url_)

> 1. Assert: _url_ corresponds to an existing file.
> 2. If _url_ ends in _".mjs"_, then
>    1. Return _"module"_.
> 3. If _url_ ends in _".cjs"_, then
>    1. Return _"commonjs"_.
> 4. If _url_ ends in _".json"_, then
>    1. Return _"json"_.
> 5. If `--experimental-wasm-modules` is enabled and _url_ ends in
>    _".wasm"_, then
>    1. Return _"wasm"_.
> 6. Let _packageURL_ be the result of **LOOKUP\_PACKAGE\_SCOPE**(_url_).
> 7. Let _pjson_ be the result of **READ\_PACKAGE\_JSON**(_packageURL_).
> 8. Let _packageType_ be **null**.
> 9. If _pjson?.type_ is _"module"_ or _"commonjs"_, then
>    1. Set _packageType_ to _pjson.type_.
> 10. If _url_ ends in _".js"_, then
>     1. If _packageType_ is not **null**, then
>        1. Return _packageType_.
>     2. If the result of **DETECT\_MODULE\_SYNTAX**(_source_) is true, then
>        1. Return _"module"_.
>     3. Return _"commonjs"_.
> 11. If _url_ does not have any extension, then
>     1. If _packageType_ is _"module"_ and `--experimental-wasm-modules` is
>        enabled and the file at _url_ contains the header for a WebAssembly
>        module, then
>        1. Return _"wasm"_.
>     2. If _packageType_ is not **null**, then
>        1. Return _packageType_.
>     3. If the result of **DETECT\_MODULE\_SYNTAX**(_source_) is true, then
>        1. Return _"module"_.
>     4. Return _"commonjs"_.
> 12. Return **undefined** (will throw during load phase).

**LOOKUP\_PACKAGE\_SCOPE**(_url_)

> 1. Let _scopeURL_ be _url_.
> 2. While _scopeURL_ is not the file system root,
>    1. Set _scopeURL_ to the parent URL of _scopeURL_.
>    2. If _scopeURL_ ends in a _"node\_modules"_ path segment, return **null**.
>    3. Let _pjsonURL_ be the resolution of _"package.json"_ within
>       _scopeURL_.
>    4. if the file at _pjsonURL_ exists, then
>       1. Return _scopeURL_.
> 3. Return **null**.

**READ\_PACKAGE\_JSON**(_packageURL_)

> 1. Let _pjsonURL_ be the resolution of _"package.json"_ within _packageURL_.
> 2. If the file at _pjsonURL_ does not exist, then
>    1. Return **null**.
> 3. If the file at _packageURL_ does not parse as valid JSON, then
>    1. Throw an _Invalid Package Configuration_ error.
> 4. Return the parsed JSON source of the file at _pjsonURL_.

**DETECT\_MODULE\_SYNTAX**(_source_)

> 1. Parse _source_ as an ECMAScript module.
> 2. If the parse is successful, then
>    1. If _source_ contains top-level `await`, static `import` or `export`
>       statements, or `import.meta`, return **true**.
>    2. If _source_ contains a top-level lexical declaration (`const`, `let`,
>       or `class`) of any of the CommonJS wrapper variables (`require`,
>       `exports`, `module`, `__filename`, or `__dirname`) then return **true**.
> 3. Else return **false**.

### Customizing ESM specifier resolution algorithm

[Module customization hooks][] provide a mechanism for customizing the ESM
specifier resolution algorithm. An example that provides CommonJS-style
resolution for ESM specifiers is [commonjs-extension-resolution-loader][].

<!-- Note: The cjs-module-lexer link should be kept in-sync with the deps version -->

[6.1.7 Array Index]: https://tc39.es/ecma262/#integer-index
[Addons]: addons.md
[Built-in modules]: modules.md#built-in-modules
[CommonJS]: modules.md
[Determining module system]: packages.md#determining-module-system
[Dynamic `import()`]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Operators/import
[ES Module Integration Proposal for WebAssembly]: https://github.com/webassembly/esm-integration
[Import Attributes]: #import-attributes
[Import Attributes proposal]: https://github.com/tc39/proposal-import-attributes
[JSON modules]: #json-modules
[Loading ECMAScript modules using `require()`]: modules.md#loading-ecmascript-modules-using-require
[Module customization hooks]: module.md#customization-hooks
[Node.js Module Resolution And Loading Algorithm]: #resolution-algorithm-specification
[Terminology]: #terminology
[URL]: https://url.spec.whatwg.org/
[`"exports"`]: packages.md#exports
[`"type"`]: packages.md#type
[`--experimental-default-type`]: cli.md#--experimental-default-typetype
[`--input-type`]: cli.md#--input-typetype
[`data:` URLs]: https://developer.mozilla.org/en-US/docs/Web/HTTP/Basics_of_HTTP/Data_URIs
[`export`]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Statements/export
[`import()`]: #import-expressions
[`import.meta.dirname`]: #importmetadirname
[`import.meta.filename`]: #importmetafilename
[`import.meta.resolve`]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Operators/import.meta/resolve
[`import.meta.url`]: #importmetaurl
[`import`]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Statements/import
[`module.createRequire()`]: module.md#modulecreaterequirefilename
[`module.syncBuiltinESMExports()`]: module.md#modulesyncbuiltinesmexports
[`package.json`]: packages.md#nodejs-packagejson-field-definitions
[`path.dirname()`]: path.md#pathdirnamepath
[`process.dlopen`]: process.md#processdlopenmodule-filename-flags
[`url.fileURLToPath()`]: url.md#urlfileurltopathurl-options
[cjs-module-lexer]: https://github.com/nodejs/cjs-module-lexer/tree/1.2.2
[commonjs-extension-resolution-loader]: https://github.com/nodejs/loaders-test/tree/main/commonjs-extension-resolution-loader
[custom https loader]: module.md#import-from-https
[import.meta.resolve]: #importmetaresolvespecifier
[percent-encoded]: url.md#percent-encoding-in-urls
[special scheme]: https://url.spec.whatwg.org/#special-scheme
[status code]: process.md#exit-codes
[the official standard format]: https://tc39.github.io/ecma262/#sec-modules
[url.pathToFileURL]: url.md#urlpathtofileurlpath-options
