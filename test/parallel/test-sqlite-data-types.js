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

suite('data binding and mapping', () => {
  test('supported data types', (t) => {
    const u8a = new TextEncoder().encode('a☃b☃c');
    const db = new DatabaseSync(nextDb());
    t.after(() => { db.close(); });
    const setup = db.exec(`
      CREATE TABLE types(
        key INTEGER PRIMARY KEY,
        int INTEGER,
        double REAL,
        text TEXT,
        buf BLOB
      ) STRICT;
    `);
    t.assert.strictEqual(setup, undefined);
    const stmt = db.prepare('INSERT INTO types (key, int, double, text, buf) ' +
      'VALUES (?, ?, ?, ?, ?)');
    t.assert.deepStrictEqual(
      stmt.run(1, 42, 3.14159, 'foo', u8a),
      { changes: 1, lastInsertRowid: 1 },
    );
    t.assert.deepStrictEqual(
      stmt.run(2, null, null, null, null),
      { changes: 1, lastInsertRowid: 2 }
    );
    t.assert.deepStrictEqual(
      stmt.run(3, Number(8), Number(2.718), String('bar'), Buffer.from('x☃y☃')),
      { changes: 1, lastInsertRowid: 3 },
    );
    t.assert.deepStrictEqual(
      stmt.run(4, 99n, 0xf, '', new Uint8Array()),
      { changes: 1, lastInsertRowid: 4 },
    );

    const query = db.prepare('SELECT * FROM types WHERE key = ?');
    t.assert.deepStrictEqual(query.get(1), {
      __proto__: null,
      key: 1,
      int: 42,
      double: 3.14159,
      text: 'foo',
      buf: u8a,
    });
    t.assert.deepStrictEqual(query.get(2), {
      __proto__: null,
      key: 2,
      int: null,
      double: null,
      text: null,
      buf: null,
    });
    t.assert.deepStrictEqual(query.get(3), {
      __proto__: null,
      key: 3,
      int: 8,
      double: 2.718,
      text: 'bar',
      buf: new TextEncoder().encode('x☃y☃'),
    });
    t.assert.deepStrictEqual(query.get(4), {
      __proto__: null,
      key: 4,
      int: 99,
      double: 0xf,
      text: '',
      buf: new Uint8Array(),
    });
  });

  test('unsupported data types', (t) => {
    const db = new DatabaseSync(nextDb());
    t.after(() => { db.close(); });
    const setup = db.exec(
      'CREATE TABLE types(key INTEGER PRIMARY KEY, val INTEGER) STRICT;'
    );
    t.assert.strictEqual(setup, undefined);

    [
      undefined,
      () => {},
      Symbol(),
      /foo/,
      Promise.resolve(),
      new Map(),
      new Set(),
    ].forEach((val) => {
      t.assert.throws(() => {
        db.prepare('INSERT INTO types (key, val) VALUES (?, ?)').run(1, val);
      }, {
        code: 'ERR_INVALID_ARG_TYPE',
        message: /Provided value cannot be bound to SQLite parameter 2/,
      });
    });

    t.assert.throws(() => {
      const stmt = db.prepare('INSERT INTO types (key, val) VALUES ($k, $v)');
      stmt.run({ $k: 1, $v: () => {} });
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /Provided value cannot be bound to SQLite parameter 2/,
    });
  });

  test('throws when binding a BigInt that is too large', (t) => {
    const max = 9223372036854775807n; // Largest 64-bit signed integer value.
    const db = new DatabaseSync(nextDb());
    t.after(() => { db.close(); });
    const setup = db.exec(
      'CREATE TABLE types(key INTEGER PRIMARY KEY, val INTEGER) STRICT;'
    );
    t.assert.strictEqual(setup, undefined);
    const stmt = db.prepare('INSERT INTO types (key, val) VALUES (?, ?)');
    t.assert.deepStrictEqual(
      stmt.run(1, max),
      { changes: 1, lastInsertRowid: 1 },
    );
    t.assert.throws(() => {
      stmt.run(1, max + 1n);
    }, {
      code: 'ERR_INVALID_ARG_VALUE',
      message: /BigInt value is too large to bind/,
    });
  });

  test('statements are unbound on each call', (t) => {
    const db = new DatabaseSync(nextDb());
    t.after(() => { db.close(); });
    const setup = db.exec(
      'CREATE TABLE data(key INTEGER PRIMARY KEY, val INTEGER) STRICT;'
    );
    t.assert.strictEqual(setup, undefined);
    const stmt = db.prepare('INSERT INTO data (key, val) VALUES (?, ?)');
    t.assert.deepStrictEqual(
      stmt.run(1, 5),
      { changes: 1, lastInsertRowid: 1 },
    );
    t.assert.deepStrictEqual(
      stmt.run(),
      { changes: 1, lastInsertRowid: 2 },
    );
    t.assert.deepStrictEqual(
      db.prepare('SELECT * FROM data ORDER BY key').all(),
      [{ __proto__: null, key: 1, val: 5 }, { __proto__: null, key: 2, val: null }],
    );
  });
});
