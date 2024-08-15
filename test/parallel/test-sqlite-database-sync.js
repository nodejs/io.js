// Flags: --experimental-sqlite
'use strict';
require('../common');
const tmpdir = require('../common/tmpdir');
const { existsSync } = require('node:fs');
const { join } = require('node:path');
const { DatabaseSync, StatementSync } = require('node:sqlite');
const { suite, test } = require('node:test');
let cnt = 0;

tmpdir.refresh();

function nextDb() {
  return join(tmpdir.path, `database-${cnt++}.db`);
}

suite('DatabaseSync() constructor', () => {
  test('throws if called without new', (t) => {
    t.assert.throws(() => {
      DatabaseSync();
    }, {
      code: 'ERR_CONSTRUCT_CALL_REQUIRED',
      message: /Cannot call constructor without `new`/,
    });
  });

  test('throws if database path is not a string', (t) => {
    t.assert.throws(() => {
      new DatabaseSync();
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /The "path" argument must be a string/,
    });
  });

  test('throws if options is provided but is not an object', (t) => {
    t.assert.throws(() => {
      new DatabaseSync('foo', null);
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /The "options" argument must be an object/,
    });
  });

  test('throws if options.open is provided but is not a boolean', (t) => {
    t.assert.throws(() => {
      new DatabaseSync('foo', { open: 5 });
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /The "options\.open" argument must be a boolean/,
    });
  });
});

suite('DatabaseSync.prototype.open()', () => {
  test('opens a database connection', (t) => {
    const dbPath = nextDb();
    const db = new DatabaseSync(dbPath, { open: false });
    t.after(() => { db.close(); });

    t.assert.strictEqual(existsSync(dbPath), false);
    t.assert.strictEqual(db.open(), undefined);
    t.assert.strictEqual(existsSync(dbPath), true);
  });

  test('throws if database is already open', (t) => {
    const db = new DatabaseSync(nextDb(), { open: false });
    t.after(() => { db.close(); });

    db.open();
    t.assert.throws(() => {
      db.open();
    }, {
      code: 'ERR_INVALID_STATE',
      message: /database is already open/,
    });
  });
});

suite('DatabaseSync.prototype.close()', () => {
  test('closes an open database connection', (t) => {
    const db = new DatabaseSync(nextDb());

    t.assert.strictEqual(db.close(), undefined);
  });

  test('throws if database is not open', (t) => {
    const db = new DatabaseSync(nextDb(), { open: false });

    t.assert.throws(() => {
      db.close();
    }, {
      code: 'ERR_INVALID_STATE',
      message: /database is not open/,
    });
  });
});

suite('DatabaseSync.prototype.prepare()', () => {
  test('returns a prepared statement', (t) => {
    const db = new DatabaseSync(nextDb());
    t.after(() => { db.close(); });
    const stmt = db.prepare('CREATE TABLE webstorage(key TEXT)');
    t.assert.ok(stmt instanceof StatementSync);
  });

  test('throws if database is not open', (t) => {
    const db = new DatabaseSync(nextDb(), { open: false });

    t.assert.throws(() => {
      db.prepare();
    }, {
      code: 'ERR_INVALID_STATE',
      message: /database is not open/,
    });
  });

  test('throws if sql is not a string', (t) => {
    const db = new DatabaseSync(nextDb());
    t.after(() => { db.close(); });

    t.assert.throws(() => {
      db.prepare();
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /The "sql" argument must be a string/,
    });
  });
});

suite('DatabaseSync.prototype.exec()', () => {
  test('executes SQL', (t) => {
    const db = new DatabaseSync(nextDb());
    t.after(() => { db.close(); });
    const result = db.exec(`
      CREATE TABLE data(
        key INTEGER PRIMARY KEY,
        val INTEGER
      ) STRICT;
      INSERT INTO data (key, val) VALUES (1, 2);
      INSERT INTO data (key, val) VALUES (8, 9);
    `);
    t.assert.strictEqual(result, undefined);
    const stmt = db.prepare('SELECT * FROM data ORDER BY key');
    t.assert.deepStrictEqual(stmt.all(), [
      { key: 1, val: 2 },
      { key: 8, val: 9 },
    ]);
  });

  test('reports errors from SQLite', (t) => {
    const db = new DatabaseSync(nextDb());
    t.after(() => { db.close(); });

    t.assert.throws(() => {
      db.exec('CREATE TABLEEEE');
    }, {
      code: 'ERR_SQLITE_ERROR',
      message: /syntax error/,
    });
  });

  test('throws if database is not open', (t) => {
    const db = new DatabaseSync(nextDb(), { open: false });

    t.assert.throws(() => {
      db.exec();
    }, {
      code: 'ERR_INVALID_STATE',
      message: /database is not open/,
    });
  });

  test('throws if sql is not a string', (t) => {
    const db = new DatabaseSync(nextDb());
    t.after(() => { db.close(); });

    t.assert.throws(() => {
      db.exec();
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /The "sql" argument must be a string/,
    });
  });
});
