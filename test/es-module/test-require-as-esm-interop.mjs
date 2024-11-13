// Flags: --experimental-require-module
import '../common/index.mjs';
import assert from 'assert';
import { directRequireFixture, importFixture } from '../fixtures/pkgexports.mjs';

const tests = {
  'false': false,
  'string': 'cjs',
  'object': { a: 'cjs a', b: 'cjs b' },
  'fauxesmdefault': { default: 'faux esm default' },
  'fauxesmmixed': { default: 'faux esm default', a: 'faux esm a', b: 'faux esm b' },
  'fauxesmnamed': { a: 'faux esm a', b: 'faux esm b' }
};

// This test demonstrates interop between CJS and CJS represented as ESM
// under the new `export { ... as 'module.exports'}` pattern, for the above cases.
for (const [test, exactShape] of Object.entries(tests)) {
  // Each case represents a CJS dependency, which has the expected shape in CJS:
  assert.deepStrictEqual(directRequireFixture(`interop-cjsdep-${test}`), exactShape);

  // Each dependency is reexported through CJS as if it is a library being consumed,
  // which in CJS is fully shape-preserving:
  assert.deepStrictEqual(directRequireFixture(`interop-cjs/${test}`), exactShape);

  // Now we have ESM conversions of these dependencies, using `export ... as "module.exports"`
  // staring with the conversion of those dependencies into ESM under require(esm):
  assert.deepStrictEqual(directRequireFixture(`interop-cjsdep-${test}-esm`), exactShape);

  // When importing these ESM conversions, from require(esm), we should preserve the shape:
  assert.deepStrictEqual(directRequireFixture(`interop-cjs/${test}-esm`), exactShape);

  // Now if the importer itself is converted into ESM, it should still be able to load the original
  // CJS and reexport it, preserving the shape:
  assert.deepStrictEqual(directRequireFixture(`interop-cjs-esm/${test}`), exactShape);

  // And then if we have the converted CJS to ESM importing from converted CJS to ESM,
  // that should also work:
  assert.deepStrictEqual(directRequireFixture(`interop-cjs-esm/${test}-esm`), exactShape);

  // Finally, the CJS ESM representation under `import()` should match all these cases equivalently,
  // where the CJS module is exported as the default export:
  const esmCjsImport = await importFixture(`interop-cjsdep-${test}`);
  assert.deepStrictEqual(esmCjsImport.default, exactShape);

  assert.deepStrictEqual((await importFixture(`interop-cjsdep-${test}`)).default, exactShape);
  assert.deepStrictEqual((await importFixture(`interop-cjs/${test}`)).default, exactShape);
  assert.deepStrictEqual((await importFixture(`interop-cjsdep-${test}-esm`)).default, exactShape);
  assert.deepStrictEqual((await importFixture(`interop-cjs/${test}-esm`)).default, exactShape);
  assert.deepStrictEqual((await importFixture(`interop-cjs-esm/${test}`)).default, exactShape);
  assert.deepStrictEqual((await importFixture(`interop-cjs-esm/${test}-esm`)).default, exactShape);
}
