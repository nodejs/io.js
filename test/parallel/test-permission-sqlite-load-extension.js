'use strict';
const common = require('../common');
const assert = require('node:assert');

const code = `const sqlite = require('node:sqlite');
const db = new sqlite.DatabaseSync(':memory:', { allowExtension: true });
db.loadExtension('nonexistent');`.replace(/\n/g, ' ');

common.spawnPromisified(
  process.execPath,
  ['--permission', '--eval', code],
).then(common.mustCall(({ code, stderr }) => {
  assert.match(stderr, /Error: Cannot load SQLite extensions when the permission model is enabled/);
  assert.match(stderr, /code: 'ERR_LOAD_SQLITE_EXTENSION'/);
  assert.strictEqual(code, 1);
}));
