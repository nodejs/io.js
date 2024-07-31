import '../common/index.mjs';
import assert from 'assert';

assert.strictEqual(Object.getPrototypeOf(import.meta), null);

const keys = ['dirname', 'filename', 'resolve', 'url'];
assert.deepStrictEqual(Reflect.ownKeys(import.meta), keys);

const descriptors = Object.getOwnPropertyDescriptors(import.meta);
for (const descriptor of Object.values(descriptors)) {
  delete descriptor.value; // Values are verified below.
  assert.deepStrictEqual(descriptor, {
    enumerable: true,
    writable: true,
    configurable: true
  });
}

const urlReg = /^file:\/\/\/.*\/test\/es-module\/test-esm-import-meta\.mjs$/;
assert(import.meta.url.match(urlReg));

// Match *nix paths: `/some/path/test/es-module`
// Match Windows paths: `d:\\some\\path\\test\\es-module`
const dirReg = /^(\/|\w:\\).*(\/|\\)test(\/|\\)es-module$/;
assert.match(import.meta.dirname, dirReg);

// Match *nix paths: `/some/path/test/es-module/test-esm-import-meta.mjs`
// Match Windows paths: `d:\\some\\path\\test\\es-module\\test-esm-import-meta.js`
const fileReg = /^(\/|\w:\\).*(\/|\\)test(\/|\\)es-module(\/|\\)test-esm-import-meta\.mjs$/;
assert.match(import.meta.filename, fileReg);

// Verify that `data:` imports do not behave like `file:` imports.
import dataDirname from 'data:text/javascript,export default "dirname" in import.meta';
assert.strictEqual(dataDirname, false);
