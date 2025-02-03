# Amaro

Amaro is a wrapper around `@swc/wasm-typescript`, a WebAssembly port of the SWC TypeScript parser.
It's currently used as an internal in Node.js for [Type Stripping](https://github.com/nodejs/loaders/issues/208), but in the future it will be possible to be upgraded separately by users.
The main goal of this package is to provide a stable API for TypeScript parser, which is unstable and subject to change.

> Amaro means "bitter" in Italian. It's a reference to [Mount Amaro](https://en.wikipedia.org/wiki/Monte_Amaro_(Abruzzo)) on whose slopes this package was conceived.

## How to Install

To install Amaro, run:

```shell
npm install amaro
```

## How to Use

By default Amaro exports a `transformSync` function that performs type stripping.
Stack traces are preserved, by replacing removed types with white spaces.

```javascript
const amaro = require('amaro');
const { code } = amaro.transformSync("const foo: string = 'bar';", { mode: "strip-only" });
console.log(code); // "const foo         = 'bar';"
```

### Loader

It is possible to use Amaro as an external loader to execute TypeScript files.
This allows the installed Amaro to override the Amaro version used by Node.js.

```bash
node --experimental-strip-types --import="amaro/register" script.ts
```

Or with the alias:

```bash
node --experimental-strip-types --import="amaro/strip" script.ts
```

Enabling TypeScript feature transformation:

```bash
node --experimental-transform-types --import="amaro/transform" script.ts
```

> Note that the "amaro/transform" loader should be used with `--experimental-transform-types` flag, or
> at least with `--enable-source-maps` flag, to preserve the original source maps.

### How to update SWC

To update the SWC version, run:

```shell
./tools/update-swc.sh
git add deps
git commit -m "chore: update swc to vX.Y.Z"
```

Once you have updated the rust source code we must build the wasm.
To build the wasm it is necessary to have Docker installed.

```shell
node ./tools/build-wasm.js
git add lib
git commit -m "chore: build wasm from swc vX.Y.Z"
```

### TypeScript Version

The supported TypeScript version is 5.5.4.

## License (MIT)

See [`LICENSE.md`](./LICENSE.md).
