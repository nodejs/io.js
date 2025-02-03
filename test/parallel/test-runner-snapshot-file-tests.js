'use strict';
const common = require('../common');
const fixtures = require('../common/fixtures');
const tmpdir = require('../common/tmpdir');
const { suite, test } = require('node:test');

tmpdir.refresh();

suite('t.assert.fileSnapshot() validation', () => {
  test('path must be a string', (t) => {
    t.assert.throws(() => {
      t.assert.fileSnapshot({}, 5);
    }, /The "path" argument must be of type string/);
  });

  test('options must be an object', (t) => {
    t.assert.throws(() => {
      t.assert.fileSnapshot({}, '', null);
    }, /The "options" argument must be of type object/);
  });

  test('options.serializers must be an array if present', (t) => {
    t.assert.throws(() => {
      t.assert.fileSnapshot({}, '', { serializers: 5 });
    }, /The "options\.serializers" property must be an instance of Array/);
  });

  test('options.serializers must only contain functions', (t) => {
    t.assert.throws(() => {
      t.assert.fileSnapshot({}, '', { serializers: [() => {}, ''] });
    }, /The "options\.serializers\[1\]" property must be of type function/);
  });
});

suite('t.assert.fileSnapshot() update/read flow', () => {
  const fixture = fixtures.path(
    'test-runner', 'snapshots', 'file-snapshots.js'
  );

  test('fails prior to snapshot generation', async (t) => {
    const child = await common.spawnPromisified(
      process.execPath,
      [fixture],
      { cwd: tmpdir.path },
    );

    t.assert.strictEqual(child.code, 1);
    t.assert.strictEqual(child.signal, null);
    t.assert.match(child.stdout, /tests 3/);
    t.assert.match(child.stdout, /pass 0/);
    t.assert.match(child.stdout, /fail 3/);
    t.assert.match(child.stdout, /Missing snapshots can be generated/);
  });

  test('passes when regenerating snapshots', async (t) => {
    const child = await common.spawnPromisified(
      process.execPath,
      ['--test-update-snapshots', fixture],
      { cwd: tmpdir.path },
    );

    t.assert.strictEqual(child.code, 0);
    t.assert.strictEqual(child.signal, null);
    t.assert.match(child.stdout, /tests 3/);
    t.assert.match(child.stdout, /pass 3/);
    t.assert.match(child.stdout, /fail 0/);
  });

  test('passes when snapshots exist', async (t) => {
    const child = await common.spawnPromisified(
      process.execPath,
      [fixture],
      { cwd: tmpdir.path },
    );

    t.assert.strictEqual(child.code, 0);
    t.assert.strictEqual(child.signal, null);
    t.assert.match(child.stdout, /tests 3/);
    t.assert.match(child.stdout, /pass 3/);
    t.assert.match(child.stdout, /fail 0/);
  });
});
