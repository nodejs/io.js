const fs = require('fs');
const terser = require('terser');

const MINIFY = true;

try { fs.mkdirSync('./dist'); }
catch (e) {}

const wasmBuffer = fs.readFileSync('./lib/lexer.wasm');
const jsSource = fs.readFileSync('./src/lexer.js').toString();
const pjson = JSON.parse(fs.readFileSync('./package.json').toString());

const jsSourceProcessed = jsSource.replace('WASM_BINARY', wasmBuffer.toString('base64'));

const minified = MINIFY && terser.minify(jsSourceProcessed, {
  module: true,
  output: {
    preamble: `/* cjs-module-lexer ${pjson.version} */`
  }
});

if (minified.error)
  throw minified.error;

fs.writeFileSync('./dist/lexer.mjs', minified ? minified.code : jsSourceProcessed);
