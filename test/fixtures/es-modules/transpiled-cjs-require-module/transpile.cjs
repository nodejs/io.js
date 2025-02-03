'use strict';

// This script is used to transpile ESM fixtures from the src/ directory
// to CJS modules in dist/. The transpiled CJS files are used to test
// integration of transpiled CJS modules loading real ESM.

const { readFileSync, writeFileSync, readdirSync } = require('node:fs');

// We use typescript.js because it's already in the code base as a fixture.
// Most ecosystem tools follow a similar pattern, and this produces a bare
// minimum integration test for existing patterns.
const ts = require('../../snapshot/typescript');
const { join } = require('node:path');
const sourceDir = join(__dirname, 'src');
const files = readdirSync(sourceDir);
for (const filename of files) {
  const filePath = join(sourceDir, filename);
  const source = readFileSync(filePath, 'utf8');
  const { outputText } = ts.transpileModule(source, {
    compilerOptions: { module: ts.ModuleKind.NodeNext }
  });
  writeFileSync(join(__dirname, 'dist', filename.replace('.mjs', '.cjs')), outputText, 'utf8');
}
