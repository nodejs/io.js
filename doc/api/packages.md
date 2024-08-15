# Modules: Packages

<!--introduced_in=v12.20.0-->

<!-- type=misc -->

<!-- YAML
changes:
  - version:
    - v14.13.0
    - v12.20.0
    pr-url: https://github.com/nodejs/node/pull/34718
    description: Add support for `"exports"` patterns.
  - version:
    - v14.6.0
    - v12.19.0
    pr-url: https://github.com/nodejs/node/pull/34117
    description: Add package `"imports"` field.
  - version:
    - v13.7.0
    - v12.17.0
    pr-url: https://github.com/nodejs/node/pull/29866
    description: Unflag conditional exports.
  - version:
    - v13.7.0
    - v12.16.0
    pr-url: https://github.com/nodejs/node/pull/31001
    description: Remove the `--experimental-conditional-exports` option. In 12.16.0, conditional exports are still behind `--experimental-modules`.
  - version:
    - v13.6.0
    - v12.16.0
    pr-url: https://github.com/nodejs/node/pull/31002
    description: Unflag self-referencing a package using its name.
  - version: v12.7.0
    pr-url: https://github.com/nodejs/node/pull/28568
    description:
      Introduce `"exports"` `package.json` field as a more powerful alternative
      to the classic `"main"` field.
  - version: v12.0.0
    pr-url: https://github.com/nodejs/node/pull/26745
    description:
      Add support for ES modules using `.js` file extension via `package.json`
      `"type"` field.
-->

## Introduction

A package is a folder tree described by a `package.json` file. The package
consists of the folder containing the `package.json` file and all subfolders
until the next folder containing another `package.json` file, or a folder
named `node_modules`.

This page provides guidance for package authors writing `package.json` files
along with a reference for the [`package.json`][] fields defined by Node.js.

## Determining module system

### Introduction

Node.js will treat the following as [ES modules][] when passed to `node` as the
initial input, or when referenced by `import` statements or `import()`
expressions:

* Files with an `.mjs` extension.

* Files with a `.js` extension when the nearest parent `package.json` file
  contains a top-level [`"type"`][] field with a value of `"module"`.

* Strings passed in as an argument to `--eval`, or piped to `node` via `STDIN`,
  with the flag `--input-type=module`.

* Code containing syntax only successfully parsed as [ES modules][], such as
  `import` or `export` statements or `import.meta`, with no explicit marker of
  how it should be interpreted. Explicit markers are `.mjs` or `.cjs`
  extensions, `package.json` `"type"` fields with either `"module"` or
  `"commonjs"` values, or `--input-type` or `--experimental-default-type` flags.
  Dynamic `import()` expressions are supported in either CommonJS or ES modules
  and would not force a file to be treated as an ES module. See
  [Syntax detection][].

Node.js will treat the following as [CommonJS][] when passed to `node` as the
initial input, or when referenced by `import` statements or `import()`
expressions:

* Files with a `.cjs` extension.

* Files with a `.js` extension when the nearest parent `package.json` file
  contains a top-level field [`"type"`][] with a value of `"commonjs"`.

* Strings passed in as an argument to `--eval` or `--print`, or piped to `node`
  via `STDIN`, with the flag `--input-type=commonjs`.

Aside from these explicit cases, there are other cases where Node.js defaults to
one module system or the other based on the value of the
[`--experimental-default-type`][] flag:

* Files ending in `.js` or with no extension, if there is no `package.json` file
  present in the same folder or any parent folder.

* Files ending in `.js` or with no extension, if the nearest parent
  `package.json` field lacks a `"type"` field; unless the folder is inside a
  `node_modules` folder. (Package scopes under `node_modules` are always treated
  as CommonJS when the `package.json` file lacks a `"type"` field, regardless
  of `--experimental-default-type`, for backward compatibility.)

* Strings passed in as an argument to `--eval` or piped to `node` via `STDIN`,
  when `--input-type` is unspecified.

This flag currently defaults to `"commonjs"`, but it may change in the future to
default to `"module"`. For this reason it is best to be explicit wherever
possible; in particular, package authors should always include the [`"type"`][]
field in their `package.json` files, even in packages where all sources are
CommonJS. Being explicit about the `type` of the package will future-proof the
package in case the default type of Node.js ever changes, and it will also make
things easier for build tools and loaders to determine how the files in the
package should be interpreted.

### Syntax detection

<!-- YAML
added:
  - v21.1.0
  - v20.10.0
changes:
  - version:
    - REPLACEME
    pr-url: https://github.com/nodejs/node/pull/53619
    description: Syntax detection is enabled by default.
-->

> Stability: 1.2 - Release candidate

Node.js will inspect the source code of ambiguous input to determine whether it
contains ES module syntax; if such syntax is detected, the input will be treated
as an ES module.

Ambiguous input is defined as:

* Files with a `.js` extension or no extension; and either no controlling
  `package.json` file or one that lacks a `type` field; and
  `--experimental-default-type` is not specified.
* String input (`--eval` or STDIN) when neither `--input-type` nor
  `--experimental-default-type` are specified.

ES module syntax is defined as syntax that would throw when evaluated as
CommonJS. This includes the following:

* `import` statements (but _not_ `import()` expressions, which are valid in
  CommonJS).
* `export` statements.
* `import.meta` references.
* `await` at the top level of a module.
* Lexical redeclarations of the CommonJS wrapper variables (`require`, `module`,
  `exports`, `__dirname`, `__filename`).

### Modules loaders

Node.js has two systems for resolving a specifier and loading modules.

There is the CommonJS module loader:

* It is fully synchronous.
* It is responsible for handling `require()` calls.
* It is monkey patchable.
* It supports [folders as modules][].
* When resolving a specifier, if no exact match is found, it will try to add
  extensions (`.js`, `.json`, and finally `.node`) and then attempt to resolve
  [folders as modules][].
* It treats `.json` as JSON text files.
* `.node` files are interpreted as compiled addon modules loaded with
  `process.dlopen()`.
* It treats all files that lack `.json` or `.node` extensions as JavaScript
  text files.
* It can only be used to [load ECMASCript modules from CommonJS modules][] if
  the module graph is synchronous (that contains no top-level `await`) when
  `--experimental-require-module` is enabled.
  When used to load a JavaScript text file that is not an ECMAScript module,
  the file will be loaded as a CommonJS module.

There is the ECMAScript module loader:

* It is asynchronous, unless it's being used to load modules for `require()`.
* It is responsible for handling `import` statements and `import()` expressions.
* It is not monkey patchable, can be customized using [loader hooks][].
* It does not support folders as modules, directory indexes (e.g.
  `'./startup/index.js'`) must be fully specified.
* It does no extension searching. A file extension must be provided
  when the specifier is a relative or absolute file URL.
* It can load JSON modules, but an import type attribute is required.
* It accepts only `.js`, `.mjs`, and `.cjs` extensions for JavaScript text
  files.
* It can be used to load JavaScript CommonJS modules. Such modules
  are passed through the `cjs-module-lexer` to try to identify named exports,
  which are available if they can be determined through static analysis.
  Imported CommonJS modules have their URLs converted to absolute
  paths and are then loaded via the CommonJS module loader.

### `package.json` and file extensions

Within a package, the [`package.json`][] [`"type"`][] field defines how
Node.js should interpret `.js` files. If a `package.json` file does not have a
`"type"` field, `.js` files are treated as [CommonJS][].

A `package.json` `"type"` value of `"module"` tells Node.js to interpret `.js`
files within that package as using [ES module][] syntax.

The `"type"` field applies not only to initial entry points (`node my-app.js`)
but also to files referenced by `import` statements and `import()` expressions.

```js
// my-app.js, treated as an ES module because there is a package.json
// file in the same folder with "type": "module".

import './startup/init.js';
// Loaded as ES module since ./startup contains no package.json file,
// and therefore inherits the "type" value from one level up.

import 'commonjs-package';
// Loaded as CommonJS since ./node_modules/commonjs-package/package.json
// lacks a "type" field or contains "type": "commonjs".

import './node_modules/commonjs-package/index.js';
// Loaded as CommonJS since ./node_modules/commonjs-package/package.json
// lacks a "type" field or contains "type": "commonjs".
```

Files ending with `.mjs` are always loaded as [ES modules][] regardless of
the nearest parent `package.json`.

Files ending with `.cjs` are always loaded as [CommonJS][] regardless of the
nearest parent `package.json`.

```js
import './legacy-file.cjs';
// Loaded as CommonJS since .cjs is always loaded as CommonJS.

import 'commonjs-package/src/index.mjs';
// Loaded as ES module since .mjs is always loaded as ES module.
```

The `.mjs` and `.cjs` extensions can be used to mix types within the same
package:

* Within a `"type": "module"` package, Node.js can be instructed to
  interpret a particular file as [CommonJS][] by naming it with a `.cjs`
  extension (since both `.js` and `.mjs` files are treated as ES modules within
  a `"module"` package).

* Within a `"type": "commonjs"` package, Node.js can be instructed to
  interpret a particular file as an [ES module][] by naming it with an `.mjs`
  extension (since both `.js` and `.cjs` files are treated as CommonJS within a
  `"commonjs"` package).

### `--input-type` flag

<!-- YAML
added: v12.0.0
-->

Strings passed in as an argument to `--eval` (or `-e`), or piped to `node` via
`STDIN`, are treated as [ES modules][] when the `--input-type=module` flag
is set.

```bash
node --input-type=module --eval "import { sep } from 'node:path'; console.log(sep);"

echo "import { sep } from 'node:path'; console.log(sep);" | node --input-type=module
```

For completeness there is also `--input-type=commonjs`, for explicitly running
string input as CommonJS. This is the default behavior if `--input-type` is
unspecified.

## Determining package manager

> Stability: 1 - Experimental

While all Node.js projects are expected to be installable by all package
managers once published, their development teams are often required to use one
specific package manager. To make this process easier, Node.js ships with a
tool called [Corepack][] that aims to make all package managers transparently
available in your environment - provided you have Node.js installed.

By default Corepack won't enforce any specific package manager and will use
the generic "Last Known Good" versions associated with each Node.js release,
but you can improve this experience by setting the [`"packageManager"`][] field
in your project's `package.json`.

## Package entry points

In a package's `package.json` file, two fields can define entry points for a
package: [`"main"`][] and [`"exports"`][]. Both fields apply to both ES module
and CommonJS module entry points.

The [`"main"`][] field is supported in all versions of Node.js, but its
capabilities are limited: it only defines the main entry point of the package.

The [`"exports"`][] provides a modern alternative to [`"main"`][] allowing
multiple entry points to be defined, conditional entry resolution support
between environments, and **preventing any other entry points besides those
defined in [`"exports"`][]**. This encapsulation allows module authors to
clearly define the public interface for their package.

For new packages targeting the currently supported versions of Node.js, the
[`"exports"`][] field is recommended. For packages supporting Node.js 10 and
below, the [`"main"`][] field is required. If both [`"exports"`][] and
[`"main"`][] are defined, the [`"exports"`][] field takes precedence over
[`"main"`][] in supported versions of Node.js.

[Conditional exports][] can be used within [`"exports"`][] to define different
package entry points per environment, including whether the package is
referenced via `require` or via `import`. For more information about supporting
both CommonJS and ES modules in a single package please consult
[the dual CommonJS/ES module packages section][].

Existing packages introducing the [`"exports"`][] field will prevent consumers
of the package from using any entry points that are not defined, including the
[`package.json`][] (e.g. `require('your-package/package.json')`). **This will
likely be a breaking change.**

To make the introduction of [`"exports"`][] non-breaking, ensure that every
previously supported entry point is exported. It is best to explicitly specify
entry points so that the package's public API is well-defined. For example,
a project that previously exported `main`, `lib`,
`feature`, and the `package.json` could use the following `package.exports`:

```json
{
  "name": "my-package",
  "exports": {
    ".": "./lib/index.js",
    "./lib": "./lib/index.js",
    "./lib/index": "./lib/index.js",
    "./lib/index.js": "./lib/index.js",
    "./feature": "./feature/index.js",
    "./feature/index": "./feature/index.js",
    "./feature/index.js": "./feature/index.js",
    "./package.json": "./package.json"
  }
}
```

Alternatively a project could choose to export entire folders both with and
without extensioned subpaths using export patterns:

```json
{
  "name": "my-package",
  "exports": {
    ".": "./lib/index.js",
    "./lib": "./lib/index.js",
    "./lib/*": "./lib/*.js",
    "./lib/*.js": "./lib/*.js",
    "./feature": "./feature/index.js",
    "./feature/*": "./feature/*.js",
    "./feature/*.js": "./feature/*.js",
    "./package.json": "./package.json"
  }
}
```

With the above providing backwards-compatibility for any minor package versions,
a future major change for the package can then properly restrict the exports
to only the specific feature exports exposed:

```json
{
  "name": "my-package",
  "exports": {
    ".": "./lib/index.js",
    "./feature/*.js": "./feature/*.js",
    "./feature/internal/*": null
  }
}
```

### Main entry point export

When writing a new package, it is recommended to use the [`"exports"`][] field:

```json
{
  "exports": "./index.js"
}
```

When the [`"exports"`][] field is defined, all subpaths of the package are
encapsulated and no longer available to importers. For example,
`require('pkg/subpath.js')` throws an [`ERR_PACKAGE_PATH_NOT_EXPORTED`][]
error.

This encapsulation of exports provides more reliable guarantees
about package interfaces for tools and when handling semver upgrades for a
package. It is not a strong encapsulation since a direct require of any
absolute subpath of the package such as
`require('/path/to/node_modules/pkg/subpath.js')` will still load `subpath.js`.

All currently supported versions of Node.js and modern build tools support the
`"exports"` field. For projects using an older version of Node.js or a related
build tool, compatibility can be achieved by including the `"main"` field
alongside `"exports"` pointing to the same module:

```json
{
  "main": "./index.js",
  "exports": "./index.js"
}
```

### Subpath exports

<!-- YAML
added: v12.7.0
-->

When using the [`"exports"`][] field, custom subpaths can be defined along
with the main entry point by treating the main entry point as the
`"."` subpath:

```json
{
  "exports": {
    ".": "./index.js",
    "./submodule.js": "./src/submodule.js"
  }
}
```

Now only the defined subpath in [`"exports"`][] can be imported by a consumer:

```js
import submodule from 'es-module-package/submodule.js';
// Loads ./node_modules/es-module-package/src/submodule.js
```

While other subpaths will error:

```js
import submodule from 'es-module-package/private-module.js';
// Throws ERR_PACKAGE_PATH_NOT_EXPORTED
```

#### Extensions in subpaths

Package authors should provide either extensioned (`import 'pkg/subpath.js'`) or
extensionless (`import 'pkg/subpath'`) subpaths in their exports. This ensures
that there is only one subpath for each exported module so that all dependents
import the same consistent specifier, keeping the package contract clear for
consumers and simplifying package subpath completions.

Traditionally, packages tended to use the extensionless style, which has the
benefits of readability and of masking the true path of the file within the
package.

With [import maps][] now providing a standard for package resolution in browsers
and other JavaScript runtimes, using the extensionless style can result in
bloated import map definitions. Explicit file extensions can avoid this issue by
enabling the import map to utilize a [packages folder mapping][] to map multiple
subpaths where possible instead of a separate map entry per package subpath
export. This also mirrors the requirement of using [the full specifier path][]
in relative and absolute import specifiers.

### Exports sugar

<!-- YAML
added: v12.11.0
-->

If the `"."` export is the only export, the [`"exports"`][] field provides sugar
for this case being the direct [`"exports"`][] field value.

```json
{
  "exports": {
    ".": "./index.js"
  }
}
```

can be written:

```json
{
  "exports": "./index.js"
}
```

### Subpath imports

<!-- YAML
added:
  - v14.6.0
  - v12.19.0
-->

In addition to the [`"exports"`][] field, there is a package `"imports"` field
to create private mappings that only apply to import specifiers from within the
package itself.

Entries in the `"imports"` field must always start with `#` to ensure they are
disambiguated from external package specifiers.

For example, the imports field can be used to gain the benefits of conditional
exports for internal modules:

```json
// package.json
{
  "imports": {
    "#dep": {
      "node": "dep-node-native",
      "default": "./dep-polyfill.js"
    }
  },
  "dependencies": {
    "dep-node-native": "^1.0.0"
  }
}
```

where `import '#dep'` does not get the resolution of the external package
`dep-node-native` (including its exports in turn), and instead gets the local
file `./dep-polyfill.js` relative to the package in other environments.

Unlike the `"exports"` field, the `"imports"` field permits mapping to external
packages.

The resolution rules for the imports field are otherwise analogous to the
exports field.

### Subpath patterns

<!-- YAML
added:
  - v14.13.0
  - v12.20.0
changes:
  - version:
    - v16.10.0
    - v14.19.0
    pr-url: https://github.com/nodejs/node/pull/40041
    description: Support pattern trailers in "imports" field.
  - version:
    - v16.9.0
    - v14.19.0
    pr-url: https://github.com/nodejs/node/pull/39635
    description: Support pattern trailers.
-->

For packages with a small number of exports or imports, we recommend
explicitly listing each exports subpath entry. But for packages that have
large numbers of subpaths, this might cause `package.json` bloat and
maintenance issues.

For these use cases, subpath export patterns can be used instead:

```json
// ./node_modules/es-module-package/package.json
{
  "exports": {
    "./features/*.js": "./src/features/*.js"
  },
  "imports": {
    "#internal/*.js": "./src/internal/*.js"
  }
}
```

**`*` maps expose nested subpaths as it is a string replacement syntax
only.**

All instances of `*` on the right hand side will then be replaced with this
value, including if it contains any `/` separators.

```js
import featureX from 'es-module-package/features/x.js';
// Loads ./node_modules/es-module-package/src/features/x.js

import featureY from 'es-module-package/features/y/y.js';
// Loads ./node_modules/es-module-package/src/features/y/y.js

import internalZ from '#internal/z.js';
// Loads ./node_modules/es-module-package/src/internal/z.js
```

This is a direct static matching and replacement without any special handling
for file extensions. Including the `"*.js"` on both sides of the mapping
restricts the exposed package exports to only JS files.

The property of exports being statically enumerable is maintained with exports
patterns since the individual exports for a package can be determined by
treating the right hand side target pattern as a `**` glob against the list of
files within the package. Because `node_modules` paths are forbidden in exports
targets, this expansion is dependent on only the files of the package itself.

To exclude private subfolders from patterns, `null` targets can be used:

```json
// ./node_modules/es-module-package/package.json
{
  "exports": {
    "./features/*.js": "./src/features/*.js",
    "./features/private-internal/*": null
  }
}
```

```js
import featureInternal from 'es-module-package/features/private-internal/m.js';
// Throws: ERR_PACKAGE_PATH_NOT_EXPORTED

import featureX from 'es-module-package/features/x.js';
// Loads ./node_modules/es-module-package/src/features/x.js
```

### Conditional exports

<!-- YAML
added:
  - v13.2.0
  - v12.16.0
changes:
  - version:
    - v13.7.0
    - v12.16.0
    pr-url: https://github.com/nodejs/node/pull/31001
    description: Unflag conditional exports.
-->

Conditional exports provide a way to map to different paths depending on
certain conditions. They are supported for both CommonJS and ES module imports.

For example, a package that wants to provide different ES module exports for
`require()` and `import` can be written:

```json
// package.json
{
  "exports": {
    "import": "./index-module.js",
    "require": "./index-require.cjs"
  },
  "type": "module"
}
```

Node.js implements the following conditions, listed in order from most
specific to least specific as conditions should be defined:

* `"node-addons"` - similar to `"node"` and matches for any Node.js environment.
  This condition can be used to provide an entry point which uses native C++
  addons as opposed to an entry point which is more universal and doesn't rely
  on native addons. This condition can be disabled via the
  [`--no-addons` flag][].
* `"node"` - matches for any Node.js environment. Can be a CommonJS or ES
  module file. _In most cases explicitly calling out the Node.js platform is
  not necessary._
* `"import"` - matches when the package is loaded via `import` or
  `import()`, or via any top-level import or resolve operation by the
  ECMAScript module loader. Applies regardless of the module format of the
  target file. _Always mutually exclusive with `"require"`._
* `"require"` - matches when the package is loaded via `require()`. The
  referenced file should be loadable with `require()` although the condition
  matches regardless of the module format of the target file. Expected
  formats include CommonJS, JSON, native addons, and ES modules
  if `--experimental-require-module` is enabled. _Always mutually
  exclusive with `"import"`._
* `"default"` - the generic fallback that always matches. Can be a CommonJS
  or ES module file. _This condition should always come last._

Within the [`"exports"`][] object, key order is significant. During condition
matching, earlier entries have higher priority and take precedence over later
entries. _The general rule is that conditions should be from most specific to
least specific in object order_.

Using the `"import"` and `"require"` conditions can lead to some hazards,
which are further explained in [the dual CommonJS/ES module packages section][].

The `"node-addons"` condition can be used to provide an entry point which
uses native C++ addons. However, this condition can be disabled via the
[`--no-addons` flag][]. When using `"node-addons"`, it's recommended to treat
`"default"` as an enhancement that provides a more universal entry point, e.g.
using WebAssembly instead of a native addon.

Conditional exports can also be extended to exports subpaths, for example:

```json
{
  "exports": {
    ".": "./index.js",
    "./feature.js": {
      "node": "./feature-node.js",
      "default": "./feature.js"
    }
  }
}
```

Defines a package where `require('pkg/feature.js')` and
`import 'pkg/feature.js'` could provide different implementations between
Node.js and other JS environments.

When using environment branches, always include a `"default"` condition where
possible. Providing a `"default"` condition ensures that any unknown JS
environments are able to use this universal implementation, which helps avoid
these JS environments from having to pretend to be existing environments in
order to support packages with conditional exports. For this reason, using
`"node"` and `"default"` condition branches is usually preferable to using
`"node"` and `"browser"` condition branches.

### Nested conditions

In addition to direct mappings, Node.js also supports nested condition objects.

For example, to define a package that only has dual mode entry points for
use in Node.js but not the browser:

```json
{
  "exports": {
    "node": {
      "import": "./feature-node.mjs",
      "require": "./feature-node.cjs"
    },
    "default": "./feature.mjs"
  }
}
```

Conditions continue to be matched in order as with flat conditions. If
a nested condition does not have any mapping it will continue checking
the remaining conditions of the parent condition. In this way nested
conditions behave analogously to nested JavaScript `if` statements.

### Resolving user conditions

<!-- YAML
added:
  - v14.9.0
  - v12.19.0
-->

When running Node.js, custom user conditions can be added with the
`--conditions` flag:

```bash
node --conditions=development index.js
```

which would then resolve the `"development"` condition in package imports and
exports, while resolving the existing `"node"`, `"node-addons"`, `"default"`,
`"import"`, and `"require"` conditions as appropriate.

Any number of custom conditions can be set with repeat flags.

### Community Conditions Definitions

Condition strings other than the `"import"`, `"require"`, `"node"`,
`"node-addons"` and `"default"` conditions
[implemented in Node.js core](#conditional-exports) are ignored by default.

Other platforms may implement other conditions and user conditions can be
enabled in Node.js via the [`--conditions` / `-C` flag][].

Since custom package conditions require clear definitions to ensure correct
usage, a list of common known package conditions and their strict definitions
is provided below to assist with ecosystem coordination.

* `"types"` - can be used by typing systems to resolve the typing file for
  the given export. _This condition should always be included first._
* `"browser"` - any web browser environment.
* `"development"` - can be used to define a development-only environment
  entry point, for example to provide additional debugging context such as
  better error messages when running in a development mode. _Must always be
  mutually exclusive with `"production"`._
* `"production"` - can be used to define a production environment entry
  point. _Must always be mutually exclusive with `"development"`._

For other runtimes, platform-specific condition key definitions are maintained
by the [WinterCG][] in the [Runtime Keys][] proposal specification.

New conditions definitions may be added to this list by creating a pull request
to the [Node.js documentation for this section][]. The requirements for listing
a new condition definition here are that:

* The definition should be clear and unambiguous for all implementers.
* The use case for why the condition is needed should be clearly justified.
* There should exist sufficient existing implementation usage.
* The condition name should not conflict with another condition definition or
  condition in wide usage.
* The listing of the condition definition should provide a coordination
  benefit to the ecosystem that wouldn't otherwise be possible. For example,
  this would not necessarily be the case for company-specific or
  application-specific conditions.
* The condition should be such that a Node.js user would expect it to be in
  Node.js core documentation. The `"types"` condition is a good example: It
  doesn't really belong in the [Runtime Keys][] proposal but is a good fit
  here in the Node.js docs.

The above definitions may be moved to a dedicated conditions registry in due
course.

### Self-referencing a package using its name

<!-- YAML
added:
  - v13.1.0
  - v12.16.0
changes:
  - version:
    - v13.6.0
    - v12.16.0
    pr-url: https://github.com/nodejs/node/pull/31002
    description: Unflag self-referencing a package using its name.
-->

Within a package, the values defined in the package's
`package.json` [`"exports"`][] field can be referenced via the package's name.
For example, assuming the `package.json` is:

```json
// package.json
{
  "name": "a-package",
  "exports": {
    ".": "./index.mjs",
    "./foo.js": "./foo.js"
  }
}
```

Then any module _in that package_ can reference an export in the package itself:

```js
// ./a-module.mjs
import { something } from 'a-package'; // Imports "something" from ./index.mjs.
```

Self-referencing is available only if `package.json` has [`"exports"`][], and
will allow importing only what that [`"exports"`][] (in the `package.json`)
allows. So the code below, given the previous package, will generate a runtime
error:

```js
// ./another-module.mjs

// Imports "another" from ./m.mjs. Fails because
// the "package.json" "exports" field
// does not provide an export named "./m.mjs".
import { another } from 'a-package/m.mjs';
```

Self-referencing is also available when using `require`, both in an ES module,
and in a CommonJS one. For example, this code will also work:

```cjs
// ./a-module.js
const { something } = require('a-package/foo.js'); // Loads from ./foo.js.
```

Finally, self-referencing also works with scoped packages. For example, this
code will also work:

```json
// package.json
{
  "name": "@my/package",
  "exports": "./index.js"
}
```

```cjs
// ./index.js
module.exports = 42;
```

```cjs
// ./other.js
console.log(require('@my/package'));
```

```console
$ node other.js
42
```

## Dual CommonJS/ES module packages

Prior to the introduction of support for ES modules in Node.js, it was a common
pattern for package authors to include both CommonJS and ES module JavaScript
sources in their package, with `package.json` [`"main"`][] specifying the
CommonJS entry point and `package.json` `"module"` specifying the ES module
entry point.
This enabled Node.js to run the CommonJS entry point while build tools such as
bundlers used the ES module entry point, since Node.js ignored (and still
ignores) the top-level `"module"` field.

Node.js can now run ES module entry points, and a package can contain both
CommonJS and ES module entry points (either via separate specifiers such as
`'pkg'` and `'pkg/es-module'`, or both at the same specifier via [Conditional
exports][]). Unlike in the scenario where `"module"` is only used by bundlers,
or ES module files are transpiled into CommonJS on the fly before evaluation by
Node.js, the files referenced by the ES module entry point are evaluated as ES
modules.

### Dual package hazard

When an application is using a package that provides both CommonJS and ES module
sources, there is a risk of certain bugs if both versions of the package get
loaded. This potential comes from the fact that the `pkgInstance` created by
`const pkgInstance = require('pkg')` is not the same as the `pkgInstance`
created by `import pkgInstance from 'pkg'` (or an alternative main path like
`'pkg/module'`). This is the “dual package hazard,” where two versions of the
same package can be loaded within the same runtime environment. While it is
unlikely that an application or package would intentionally load both versions
directly, it is common for an application to load one version while a dependency
of the application loads the other version. This hazard can happen because
Node.js supports intermixing CommonJS and ES modules, and can lead to unexpected
behavior.

If the package main export is a constructor, an `instanceof` comparison of
instances created by the two versions returns `false`, and if the export is an
object, properties added to one (like `pkgInstance.foo = 3`) are not present on
the other. This differs from how `import` and `require` statements work in
all-CommonJS or all-ES module environments, respectively, and therefore is
surprising to users. It also differs from the behavior users are familiar with
when using transpilation via tools like [Babel][] or [`esm`][].

### Writing dual packages while avoiding or minimizing hazards

First, the hazard described in the previous section occurs when a package
contains both CommonJS and ES module sources and both sources are provided for
use in Node.js, either via separate main entry points or exported paths. A
package might instead be written where any version of Node.js receives only
CommonJS sources, and any separate ES module sources the package might contain
are intended only for other environments such as browsers. Such a package
would be usable by any version of Node.js, since `import` can refer to CommonJS
files; but it would not provide any of the advantages of using ES module syntax.

A package might also switch from CommonJS to ES module syntax in a [breaking
change](https://semver.org/) version bump. This has the disadvantage that the
newest version of the package would only be usable in ES module-supporting
versions of Node.js.

Every pattern has tradeoffs, but there are two broad approaches that satisfy the
following conditions:

1. The package is usable via both `require` and `import`.
2. The package is usable in both current Node.js and older versions of Node.js
   that lack support for ES modules.
3. The package main entry point, e.g. `'pkg'` can be used by both `require` to
   resolve to a CommonJS file and by `import` to resolve to an ES module file.
   (And likewise for exported paths, e.g. `'pkg/feature'`.)
4. The package provides named exports, e.g. `import { name } from 'pkg'` rather
   than `import pkg from 'pkg'; pkg.name`.
5. The package is potentially usable in other ES module environments such as
   browsers.
6. The hazards described in the previous section are avoided or minimized.

#### Approach #1: Use an ES module wrapper

Write the package in CommonJS or transpile ES module sources into CommonJS, and
create an ES module wrapper file that defines the named exports. Using
[Conditional exports][], the ES module wrapper is used for `import` and the
CommonJS entry point for `require`.

```json
// ./node_modules/pkg/package.json
{
  "type": "module",
  "exports": {
    "import": "./wrapper.mjs",
    "require": "./index.cjs"
  }
}
```

The preceding example uses explicit extensions `.mjs` and `.cjs`.
If your files use the `.js` extension, `"type": "module"` will cause such files
to be treated as ES modules, just as `"type": "commonjs"` would cause them
to be treated as CommonJS.
See [Enabling](esm.md#enabling).

```cjs
// ./node_modules/pkg/index.cjs
exports.name = 'value';
```

```js
// ./node_modules/pkg/wrapper.mjs
import cjsModule from './index.cjs';
export const name = cjsModule.name;
```

In this example, the `name` from `import { name } from 'pkg'` is the same
singleton as the `name` from `const { name } = require('pkg')`. Therefore `===`
returns `true` when comparing the two `name`s and the divergent specifier hazard
is avoided.

If the module is not simply a list of named exports, but rather contains a
unique function or object export like `module.exports = function () { ... }`,
or if support in the wrapper for the `import pkg from 'pkg'` pattern is desired,
then the wrapper would instead be written to export the default optionally
along with any named exports as well:

```js
import cjsModule from './index.cjs';
export const name = cjsModule.name;
export default cjsModule;
```

This approach is appropriate for any of the following use cases:

* The package is currently written in CommonJS and the author would prefer not
  to refactor it into ES module syntax, but wishes to provide named exports for
  ES module consumers.
* The package has other packages that depend on it, and the end user might
  install both this package and those other packages. For example a `utilities`
  package is used directly in an application, and a `utilities-plus` package
  adds a few more functions to `utilities`. Because the wrapper exports
  underlying CommonJS files, it doesn't matter if `utilities-plus` is written in
  CommonJS or ES module syntax; it will work either way.
* The package stores internal state, and the package author would prefer not to
  refactor the package to isolate its state management. See the next section.

A variant of this approach not requiring conditional exports for consumers could
be to add an export, e.g. `"./module"`, to point to an all-ES module-syntax
version of the package. This could be used via `import 'pkg/module'` by users
who are certain that the CommonJS version will not be loaded anywhere in the
application, such as by dependencies; or if the CommonJS version can be loaded
but doesn't affect the ES module version (for example, because the package is
stateless):

```json
// ./node_modules/pkg/package.json
{
  "type": "module",
  "exports": {
    ".": "./index.cjs",
    "./module": "./wrapper.mjs"
  }
}
```

#### Approach #2: Isolate state

A [`package.json`][] file can define the separate CommonJS and ES module entry
points directly:

```json
// ./node_modules/pkg/package.json
{
  "type": "module",
  "exports": {
    "import": "./index.mjs",
    "require": "./index.cjs"
  }
}
```

This can be done if both the CommonJS and ES module versions of the package are
equivalent, for example because one is the transpiled output of the other; and
the package's management of state is carefully isolated (or the package is
stateless).

The reason that state is an issue is because both the CommonJS and ES module
versions of the package might get used within an application; for example, the
user's application code could `import` the ES module version while a dependency
`require`s the CommonJS version. If that were to occur, two copies of the
package would be loaded in memory and therefore two separate states would be
present. This would likely cause hard-to-troubleshoot bugs.

Aside from writing a stateless package (if JavaScript's `Math` were a package,
for example, it would be stateless as all of its methods are static), there are
some ways to isolate state so that it's shared between the potentially loaded
CommonJS and ES module instances of the package:

1. If possible, contain all state within an instantiated object. JavaScript's
   `Date`, for example, needs to be instantiated to contain state; if it were a
   package, it would be used like this:

   ```js
   import Date from 'date';
   const someDate = new Date();
   // someDate contains state; Date does not
   ```

   The `new` keyword isn't required; a package's function can return a new
   object, or modify a passed-in object, to keep the state external to the
   package.

2. Isolate the state in one or more CommonJS files that are shared between the
   CommonJS and ES module versions of the package. For example, if the CommonJS
   and ES module entry points are `index.cjs` and `index.mjs`, respectively:

   ```cjs
   // ./node_modules/pkg/index.cjs
   const state = require('./state.cjs');
   module.exports.state = state;
   ```

   ```js
   // ./node_modules/pkg/index.mjs
   import state from './state.cjs';
   export {
     state,
   };
   ```

   Even if `pkg` is used via both `require` and `import` in an application (for
   example, via `import` in application code and via `require` by a dependency)
   each reference of `pkg` will contain the same state; and modifying that
   state from either module system will apply to both.

Any plugins that attach to the package's singleton would need to separately
attach to both the CommonJS and ES module singletons.

This approach is appropriate for any of the following use cases:

* The package is currently written in ES module syntax and the package author
  wants that version to be used wherever such syntax is supported.
* The package is stateless or its state can be isolated without too much
  difficulty.
* The package is unlikely to have other public packages that depend on it, or if
  it does, the package is stateless or has state that need not be shared between
  dependencies or with the overall application.

Even with isolated state, there is still the cost of possible extra code
execution between the CommonJS and ES module versions of a package.

As with the previous approach, a variant of this approach not requiring
conditional exports for consumers could be to add an export, e.g.
`"./module"`, to point to an all-ES module-syntax version of the package:

```json
// ./node_modules/pkg/package.json
{
  "type": "module",
  "exports": {
    ".": "./index.cjs",
    "./module": "./index.mjs"
  }
}
```

## Node.js `package.json` field definitions

This section describes the fields used by the Node.js runtime. Other tools (such
as [npm](https://docs.npmjs.com/cli/v8/configuring-npm/package-json)) use
additional fields which are ignored by Node.js and not documented here.

The following fields in `package.json` files are used in Node.js:

* [`"name"`][] - Relevant when using named imports within a package. Also used
  by package managers as the name of the package.
* [`"main"`][] - The default module when loading the package, if exports is not
  specified, and in versions of Node.js prior to the introduction of exports.
* [`"packageManager"`][] - The package manager recommended when contributing to
  the package. Leveraged by the [Corepack][] shims.
* [`"type"`][] - The package type determining whether to load `.js` files as
  CommonJS or ES modules.
* [`"exports"`][] - Package exports and conditional exports. When present,
  limits which submodules can be loaded from within the package.
* [`"imports"`][] - Package imports, for use by modules within the package
  itself.

### `"name"`

<!-- YAML
added:
  - v13.1.0
  - v12.16.0
changes:
  - version:
    - v13.6.0
    - v12.16.0
    pr-url: https://github.com/nodejs/node/pull/31002
    description: Remove the `--experimental-resolve-self` option.
-->

* Type: {string}

```json
{
  "name": "package-name"
}
```

The `"name"` field defines your package's name. Publishing to the
_npm_ registry requires a name that satisfies
[certain requirements](https://docs.npmjs.com/files/package.json#name).

The `"name"` field can be used in addition to the [`"exports"`][] field to
[self-reference][] a package using its name.

### `"main"`

<!-- YAML
added: v0.4.0
-->

* Type: {string}

```json
{
  "main": "./index.js"
}
```

The `"main"` field defines the entry point of a package when imported by name
via a `node_modules` lookup.  Its value is a path.

When a package has an [`"exports"`][] field, this will take precedence over the
`"main"` field when importing the package by name.

It also defines the script that is used when the [package directory is loaded
via `require()`](modules.md#folders-as-modules).

```cjs
// This resolves to ./path/to/directory/index.js.
require('./path/to/directory');
```

### `"packageManager"`

<!-- YAML
added:
  - v16.9.0
  - v14.19.0
-->

> Stability: 1 - Experimental

* Type: {string}

```json
{
  "packageManager": "<package manager name>@<version>"
}
```

The `"packageManager"` field defines which package manager is expected to be
used when working on the current project. It can be set to any of the
[supported package managers][], and will ensure that your teams use the exact
same package manager versions without having to install anything else other than
Node.js.

This field is currently experimental and needs to be opted-in; check the
[Corepack][] page for details about the procedure.

### `"type"`

<!-- YAML
added: v12.0.0
changes:
  - version:
    - v13.2.0
    - v12.17.0
    pr-url: https://github.com/nodejs/node/pull/29866
    description: Unflag `--experimental-modules`.
-->

* Type: {string}

The `"type"` field defines the module format that Node.js uses for all
`.js` files that have that `package.json` file as their nearest parent.

Files ending with `.js` are loaded as ES modules when the nearest parent
`package.json` file contains a top-level field `"type"` with a value of
`"module"`.

The nearest parent `package.json` is defined as the first `package.json` found
when searching in the current folder, that folder's parent, and so on up
until a node\_modules folder or the volume root is reached.

```json
// package.json
{
  "type": "module"
}
```

```bash
# In same folder as preceding package.json
node my-app.js # Runs as ES module
```

If the nearest parent `package.json` lacks a `"type"` field, or contains
`"type": "commonjs"`, `.js` files are treated as [CommonJS][]. If the volume
root is reached and no `package.json` is found, `.js` files are treated as
[CommonJS][].

`import` statements of `.js` files are treated as ES modules if the nearest
parent `package.json` contains `"type": "module"`.

```js
// my-app.js, part of the same example as above
import './startup.js'; // Loaded as ES module because of package.json
```

Regardless of the value of the `"type"` field, `.mjs` files are always treated
as ES modules and `.cjs` files are always treated as CommonJS.

### `"exports"`

<!-- YAML
added: v12.7.0
changes:
  - version:
    - v14.13.0
    - v12.20.0
    pr-url: https://github.com/nodejs/node/pull/34718
    description: Add support for `"exports"` patterns.
  - version:
    - v13.7.0
    - v12.17.0
    pr-url: https://github.com/nodejs/node/pull/29866
    description: Unflag conditional exports.
  - version:
    - v13.7.0
    - v12.16.0
    pr-url: https://github.com/nodejs/node/pull/31008
    description: Implement logical conditional exports ordering.
  - version:
    - v13.7.0
    - v12.16.0
    pr-url: https://github.com/nodejs/node/pull/31001
    description: Remove the `--experimental-conditional-exports` option. In 12.16.0, conditional exports are still behind `--experimental-modules`.
  - version:
    - v13.2.0
    - v12.16.0
    pr-url: https://github.com/nodejs/node/pull/29978
    description: Implement conditional exports.
-->

* Type: {Object} | {string} | {string\[]}

```json
{
  "exports": "./index.js"
}
```

The `"exports"` field allows defining the [entry points][] of a package when
imported by name loaded either via a `node_modules` lookup or a
[self-reference][] to its own name. It is supported in Node.js 12+ as an
alternative to the [`"main"`][] that can support defining [subpath exports][]
and [conditional exports][] while encapsulating internal unexported modules.

[Conditional Exports][] can also be used within `"exports"` to define different
package entry points per environment, including whether the package is
referenced via `require` or via `import`.

All paths defined in the `"exports"` must be relative file URLs starting with
`./`.

### `"imports"`

<!-- YAML
added:
 - v14.6.0
 - v12.19.0
-->

* Type: {Object}

```json
// package.json
{
  "imports": {
    "#dep": {
      "node": "dep-node-native",
      "default": "./dep-polyfill.js"
    }
  },
  "dependencies": {
    "dep-node-native": "^1.0.0"
  }
}
```

Entries in the imports field must be strings starting with `#`.

Package imports permit mapping to external packages.

This field defines [subpath imports][] for the current package.

[Babel]: https://babeljs.io/
[CommonJS]: modules.md
[Conditional exports]: #conditional-exports
[Corepack]: corepack.md
[ES module]: esm.md
[ES modules]: esm.md
[Node.js documentation for this section]: https://github.com/nodejs/node/blob/HEAD/doc/api/packages.md#conditions-definitions
[Runtime Keys]: https://runtime-keys.proposal.wintercg.org/
[Syntax detection]: #syntax-detection
[WinterCG]: https://wintercg.org/
[`"exports"`]: #exports
[`"imports"`]: #imports
[`"main"`]: #main
[`"name"`]: #name
[`"packageManager"`]: #packagemanager
[`"type"`]: #type
[`--conditions` / `-C` flag]: #resolving-user-conditions
[`--experimental-default-type`]: cli.md#--experimental-default-typetype
[`--no-addons` flag]: cli.md#--no-addons
[`ERR_PACKAGE_PATH_NOT_EXPORTED`]: errors.md#err_package_path_not_exported
[`esm`]: https://github.com/standard-things/esm#readme
[`package.json`]: #nodejs-packagejson-field-definitions
[entry points]: #package-entry-points
[folders as modules]: modules.md#folders-as-modules
[import maps]: https://github.com/WICG/import-maps
[load ECMASCript modules from CommonJS modules]: modules.md#loading-ecmascript-modules-using-require
[loader hooks]: esm.md#loaders
[packages folder mapping]: https://github.com/WICG/import-maps#packages-via-trailing-slashes
[self-reference]: #self-referencing-a-package-using-its-name
[subpath exports]: #subpath-exports
[subpath imports]: #subpath-imports
[supported package managers]: corepack.md#supported-package-managers
[the dual CommonJS/ES module packages section]: #dual-commonjses-module-packages
[the full specifier path]: esm.md#mandatory-file-extensions
