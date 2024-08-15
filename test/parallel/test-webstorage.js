'use strict';
const common = require('../common');
const tmpdir = require('../common/tmpdir');
const assert = require('node:assert');
const { join } = require('node:path');
const { readdir } = require('node:fs/promises');
const { test, describe } = require('node:test');
const { spawnPromisified } = common;
let cnt = 0;

tmpdir.refresh();

function nextLocalStorage() {
  return join(tmpdir.path, `${++cnt}.localstorage`);
}

test('disabled without --experimental-webstorage', async () => {
  for (const api of ['Storage', 'localStorage', 'sessionStorage']) {
    const cp = await spawnPromisified(process.execPath, ['-e', api]);

    assert.strictEqual(cp.code, 1);
    assert.strictEqual(cp.signal, null);
    assert.strictEqual(cp.stdout, '');
    assert(cp.stderr.includes(`ReferenceError: ${api} is not defined`));
  }
});

test('emits a warning when used', async () => {
  for (const api of ['Storage', 'localStorage', 'sessionStorage']) {
    const cp = await spawnPromisified(process.execPath, [
      '--experimental-webstorage',
      '--localstorage-file', nextLocalStorage(),
      '-e', api,
    ]);

    assert.strictEqual(cp.code, 0);
    assert.strictEqual(cp.signal, null);
    assert.strictEqual(cp.stdout, '');
    assert.match(cp.stderr, /ExperimentalWarning: Web Storage/);
  }
});

test('Storage instances cannot be created in userland', async () => {
  const cp = await spawnPromisified(process.execPath, [
    '--experimental-webstorage', '-e', 'new globalThis.Storage()',
  ]);

  assert.strictEqual(cp.code, 1);
  assert.strictEqual(cp.signal, null);
  assert.strictEqual(cp.stdout, '');
  assert.match(cp.stderr, /Error: Illegal constructor/);
});

test('sessionStorage is not persisted', async () => {
  let cp = await spawnPromisified(process.execPath, [
    '--experimental-webstorage', '-pe', 'sessionStorage.foo = "barbaz"',
  ]);
  assert.strictEqual(cp.code, 0);
  assert.match(cp.stdout, /barbaz/);

  cp = await spawnPromisified(process.execPath, [
    '--experimental-webstorage', '-pe', 'sessionStorage.foo',
  ]);
  assert.strictEqual(cp.code, 0);
  assert.match(cp.stdout, /undefined/);
  assert.strictEqual((await readdir(tmpdir.path)).length, 0);
});

test('localStorage throws without --localstorage-file ', async () => {
  const cp = await spawnPromisified(process.execPath, [
    '--experimental-webstorage',
    '-pe', 'localStorage === global.localStorage',
  ]);
  assert.strictEqual(cp.code, 1);
  assert.strictEqual(cp.signal, null);
  assert.strictEqual(cp.stdout, '');
  assert.match(cp.stderr, /The argument '--localstorage-file' is an invalid localStorage location/);
});

test('localStorage is not persisted if it is unused', async () => {
  const cp = await spawnPromisified(process.execPath, [
    '--experimental-webstorage',
    '--localstorage-file', nextLocalStorage(),
    '-pe', 'localStorage === global.localStorage',
  ]);
  assert.strictEqual(cp.code, 0);
  assert.match(cp.stdout, /true/);
  assert.strictEqual((await readdir(tmpdir.path)).length, 0);
});

test('localStorage is persisted if it is used', async () => {
  const localStorageFile = nextLocalStorage();
  let cp = await spawnPromisified(process.execPath, [
    '--experimental-webstorage',
    '--localstorage-file', localStorageFile,
    '-pe', 'localStorage.foo = "barbaz"',
  ]);
  assert.strictEqual(cp.code, 0);
  assert.match(cp.stdout, /barbaz/);
  const entries = await readdir(tmpdir.path);
  assert.strictEqual(entries.length, 1);
  assert.match(entries[0], /\d+\.localstorage/);

  cp = await spawnPromisified(process.execPath, [
    '--experimental-webstorage',
    '--localstorage-file', localStorageFile,
    '-pe', 'localStorage.foo',
  ]);
  assert.strictEqual(cp.code, 0);
  assert.match(cp.stdout, /barbaz/);
});


describe('webstorage quota for localStorage and sessionStorage', () => {
  const MAX_STORAGE_SIZE = 10 * 1024 * 1024;

  test('localStorage can store and retrieve a max of 10 MB quota', async () => {
    const localStorageFile = nextLocalStorage();
    const cp = await spawnPromisified(process.execPath, [
      '--experimental-webstorage',
      '--localstorage-file', localStorageFile,
      // Each character is 2 bytes
      '-pe', `
      localStorage['a'.repeat(${MAX_STORAGE_SIZE} / 2)] = '';
      console.error('filled');
      localStorage.anything = 'should fail';
      `,
    ]);

    assert.match(cp.stderr, /filled/);
    assert.match(cp.stderr, /QuotaExceededError: Setting the value exceeded the quota/);
  });

  test('sessionStorage can store a max of 10 MB quota', async () => {
    const cp = await spawnPromisified(process.execPath, [
      '--experimental-webstorage',
      // Each character is 2 bytes
      '-pe', `sessionStorage['a'.repeat(${MAX_STORAGE_SIZE} / 2)] = '';
      console.error('filled');
      sessionStorage.anything = 'should fail';
      `,
    ]);

    assert.match(cp.stderr, /filled/);
    assert.match(cp.stderr, /QuotaExceededError/);
  });
});
