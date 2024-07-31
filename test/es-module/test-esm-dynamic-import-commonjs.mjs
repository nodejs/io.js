import '../common/index.mjs';
import fixtures from '../common/fixtures.js';
import assert from 'node:assert';

let tickDuringCJSImport = false;
process.nextTick(() => { tickDuringCJSImport = true; });
await import(fixtures.fileURL('empty.cjs'));

assert(!tickDuringCJSImport);
