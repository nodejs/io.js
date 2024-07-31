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
const { code } = amaro.transformSync("const foo: string = 'bar';");
console.log(code); // "const foo         = 'bar';"
```

## License (MIT)

See [`LICENSE.md`](./LICENSE.md).
