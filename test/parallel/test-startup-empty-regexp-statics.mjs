// We must load the CJS version here because the ESM wrapper call `hasIPv6`
// which compiles a RegEx.
// eslint-disable-next-line node-core/require-common-first
import common from '../common/index.js';
import assert from 'node:assert';

if (!common.isInsideDirWithUnusualChars) {
  assert.strictEqual(RegExp.$_, '');
  assert.strictEqual(RegExp.$0, undefined);
  assert.strictEqual(RegExp.$1, '');
  assert.strictEqual(RegExp.$2, '');
  assert.strictEqual(RegExp.$3, '');
  assert.strictEqual(RegExp.$4, '');
  assert.strictEqual(RegExp.$5, '');
  assert.strictEqual(RegExp.$6, '');
  assert.strictEqual(RegExp.$7, '');
  assert.strictEqual(RegExp.$8, '');
  assert.strictEqual(RegExp.$9, '');
  assert.strictEqual(RegExp.input, '');
  assert.strictEqual(RegExp.lastMatch, '');
  assert.strictEqual(RegExp.lastParen, '');
  assert.strictEqual(RegExp.leftContext, '');
  assert.strictEqual(RegExp.rightContext, '');
  assert.strictEqual(RegExp['$&'], '');
  assert.strictEqual(RegExp['$`'], '');
  assert.strictEqual(RegExp['$+'], '');
  assert.strictEqual(RegExp["$'"], '');
}
