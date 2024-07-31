// Flags: --experimental-loader ./test/fixtures/es-module-loaders/hooks-custom.mjs
import '../common/index.mjs';
import assert from 'assert';


await assert.rejects(
  import('nonexistent/file.mjs'),
  { code: 'ERR_MODULE_NOT_FOUND' },
);

await assert.rejects(
  import('../fixtures/es-modules/file.unknown'),
  { code: 'ERR_UNKNOWN_FILE_EXTENSION' },
);

await assert.rejects(
  import('esmHook/badReturnVal.mjs'),
  { code: 'ERR_INVALID_RETURN_VALUE' },
);

// Nullish values are allowed; booleans are not
await assert.rejects(
  import('esmHook/format.false'),
  { code: 'ERR_INVALID_RETURN_PROPERTY_VALUE' },
);
await assert.rejects(
  import('esmHook/format.true'),
  { code: 'ERR_INVALID_RETURN_PROPERTY_VALUE' },
);

await assert.rejects(
  import('esmHook/badReturnFormatVal.mjs'),
  { code: 'ERR_INVALID_RETURN_PROPERTY_VALUE' },
);

await assert.rejects(
  import('esmHook/unsupportedReturnFormatVal.mjs'),
  { code: 'ERR_UNKNOWN_MODULE_FORMAT' },
);

await assert.rejects(
  import('esmHook/badReturnSourceVal.mjs'),
  { code: 'ERR_INVALID_RETURN_PROPERTY_VALUE' },
);

await assert.rejects(import('esmHook/commonJsNullSource.mjs'), {
  code: 'ERR_INVALID_RETURN_PROPERTY_VALUE',
  message: /"source".*'load'.*got type bigint/,
});

await import('../fixtures/es-module-loaders/js-as-esm.js')
.then((parsedModule) => {
  assert.strictEqual(typeof parsedModule, 'object');
  assert.strictEqual(parsedModule.namedExport, 'named-export');
});

// The custom loader tells node .ext files are MJS
await import('../fixtures/es-modules/file.ext')
.then((parsedModule) => {
  assert.strictEqual(typeof parsedModule, 'object');
  const { default: defaultExport } = parsedModule;
  assert.strictEqual(typeof defaultExport, 'function');
  assert.strictEqual(defaultExport.name, 'iAmReal');
  assert.strictEqual(defaultExport(), true);
});

// The custom loader's resolve hook predetermines the format
await import('esmHook/preknownFormat.pre')
.then((parsedModule) => {
  assert.strictEqual(typeof parsedModule, 'object');
  assert.strictEqual(parsedModule.default, 'hello world');
});

// The custom loader provides source even though file does not actually exist
await import('esmHook/virtual.mjs')
.then((parsedModule) => {
  assert.strictEqual(typeof parsedModule, 'object');
  assert.strictEqual(typeof parsedModule.default, 'undefined');
  assert.strictEqual(parsedModule.message, 'WOOHOO!');
});

// Ensure custom loaders have separate context from userland
// hooks-custom.mjs also increments count (which starts at 0)
// if both share context, count here would be 2
await import('../fixtures/es-modules/stateful.mjs')
.then(({ default: count }) => {
  assert.strictEqual(count(), 1);
});
