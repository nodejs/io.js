import { isMainThread, hasCrypto, hasIntl } from '../common/index.mjs';
import assert from 'node:assert';
import { builtinModules } from 'node:module';

for (const invalid of [1, undefined, null, false, [], {}, () => {}, Symbol('test')]) {
  assert.throws(() => process.getBuiltinModule(invalid), { code: 'ERR_INVALID_ARG_TYPE' });
}

for (const invalid of [
  'invalid', 'test', 'sea', 'test/reporter', 'internal/bootstrap/realm',
  'internal/deps/undici/undici', 'internal/util',
]) {
  assert.strictEqual(process.getBuiltinModule(invalid), undefined);
}

// Check that createRequire()(id) returns the same thing as process.getBuiltinModule(id).
const require = process.getBuiltinModule('module').createRequire(import.meta.url);
const publicBuiltins = new Set(builtinModules);

// Remove built-ins not available in the current setup.
if (!isMainThread) {
  publicBuiltins.delete('trace_events');
}
if (!hasCrypto) {
  publicBuiltins.delete('crypto');
  publicBuiltins.delete('tls');
  publicBuiltins.delete('_tls_common');
  publicBuiltins.delete('_tls_wrap');
  publicBuiltins.delete('http2');
  publicBuiltins.delete('https');
  publicBuiltins.delete('inspector');
  publicBuiltins.delete('inspector/promises');
}
if (!hasIntl) {
  publicBuiltins.delete('inspector');
  publicBuiltins.delete('trace_events');
}

for (const id of publicBuiltins) {
  assert.strictEqual(process.getBuiltinModule(id), require(id));
}
// Check that import(id).default returns the same thing as process.getBuiltinModule(id).
for (const id of publicBuiltins) {
  const imported = await import(`node:${id}`);
  assert.strictEqual(process.getBuiltinModule(id), imported.default);
}

// publicBuiltins does not include 'test' which requires the node: prefix.
const ids = publicBuiltins.add('test');
// Check that import(id).default returns the same thing as process.getBuiltinModule(id).
for (const id of ids) {
  const prefixed = `node:${id}`;
  const imported = await import(prefixed);
  assert.strictEqual(process.getBuiltinModule(prefixed), imported.default);
}
