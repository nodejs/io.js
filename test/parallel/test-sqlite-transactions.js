'use strict';
require('../common');
const tmpdir = require('../common/tmpdir');
const { join } = require('node:path');
const { DatabaseSync } = require('node:sqlite');
const { suite, test } = require('node:test');
let cnt = 0;

tmpdir.refresh();

function nextDb() {
  return join(tmpdir.path, `database-${cnt++}.db`);
}

suite('manual transactions', () => {
  test('a transaction is committed', (t) => {
    const db = new DatabaseSync(nextDb());
    t.after(() => { db.close(); });
    const setup = db.exec(`
      CREATE TABLE data(
        key INTEGER PRIMARY KEY
      ) STRICT;
    `);
    t.assert.strictEqual(setup, undefined);
    t.assert.deepStrictEqual(
      db.prepare('BEGIN').run(),
      { changes: 0, lastInsertRowid: 0 },
    );
    t.assert.deepStrictEqual(
      db.prepare('INSERT INTO data (key) VALUES (100)').run(),
      { changes: 1, lastInsertRowid: 100 },
    );
    t.assert.deepStrictEqual(
      db.prepare('COMMIT').run(),
      { changes: 1, lastInsertRowid: 100 },
    );
    t.assert.deepStrictEqual(
      db.prepare('SELECT * FROM data').all(),
      [{ __proto__: null, key: 100 }],
    );
  });

  test('a transaction is rolled back', (t) => {
    const db = new DatabaseSync(nextDb());
    t.after(() => { db.close(); });
    const setup = db.exec(`
      CREATE TABLE data(
        key INTEGER PRIMARY KEY
      ) STRICT;
    `);
    t.assert.strictEqual(setup, undefined);
    t.assert.deepStrictEqual(
      db.prepare('BEGIN').run(),
      { changes: 0, lastInsertRowid: 0 },
    );
    t.assert.deepStrictEqual(
      db.prepare('INSERT INTO data (key) VALUES (100)').run(),
      { changes: 1, lastInsertRowid: 100 },
    );
    t.assert.deepStrictEqual(
      db.prepare('ROLLBACK').run(),
      { changes: 1, lastInsertRowid: 100 },
    );
    t.assert.deepStrictEqual(db.prepare('SELECT * FROM data').all(), []);
  });
});
