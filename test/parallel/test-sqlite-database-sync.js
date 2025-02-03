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

  test('throws if options.readOnly is provided but is not a boolean', (t) => {
    t.assert.throws(() => {
      new DatabaseSync('foo', { readOnly: 5 });
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /The "options\.readOnly" argument must be a boolean/,
    });
  });

  test('is not read-only by default', (t) => {
    const dbPath = nextDb();
    const db = new DatabaseSync(dbPath);
    db.exec('CREATE TABLE foo (id INTEGER PRIMARY KEY)');
  });

  test('is read-only if readOnly is set', (t) => {
    const dbPath = nextDb();
    {
      const db = new DatabaseSync(dbPath);
      db.exec('CREATE TABLE foo (id INTEGER PRIMARY KEY)');
      db.close();
    }
    {
      const db = new DatabaseSync(dbPath, { readOnly: true });
      t.assert.throws(() => {
        db.exec('CREATE TABLE bar (id INTEGER PRIMARY KEY)');
      }, {
        code: 'ERR_SQLITE_ERROR',
        message: /attempt to write a readonly database/,
      });
    }
  });

  test('throws if options.enableForeignKeyConstraints is provided but is not a boolean', (t) => {
    t.assert.throws(() => {
      new DatabaseSync('foo', { enableForeignKeyConstraints: 5 });
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /The "options\.enableForeignKeyConstraints" argument must be a boolean/,
    });
  });

  test('enables foreign key constraints by default', (t) => {
    const dbPath = nextDb();
    const db = new DatabaseSync(dbPath);
    db.exec(`
      CREATE TABLE foo (id INTEGER PRIMARY KEY);
      CREATE TABLE bar (foo_id INTEGER REFERENCES foo(id));
    `);
    t.after(() => { db.close(); });
    t.assert.throws(() => {
      db.exec('INSERT INTO bar (foo_id) VALUES (1)');
    }, {
      code: 'ERR_SQLITE_ERROR',
      message: 'FOREIGN KEY constraint failed',
    });
  });

  test('allows disabling foreign key constraints', (t) => {
    const dbPath = nextDb();
    const db = new DatabaseSync(dbPath, { enableForeignKeyConstraints: false });
    db.exec(`
      CREATE TABLE foo (id INTEGER PRIMARY KEY);
      CREATE TABLE bar (foo_id INTEGER REFERENCES foo(id));
    `);
    t.after(() => { db.close(); });
    db.exec('INSERT INTO bar (foo_id) VALUES (1)');
  });

  test('throws if options.enableDoubleQuotedStringLiterals is provided but is not a boolean', (t) => {
    t.assert.throws(() => {
      new DatabaseSync('foo', { enableDoubleQuotedStringLiterals: 5 });
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /The "options\.enableDoubleQuotedStringLiterals" argument must be a boolean/,
    });
  });

  test('disables double-quoted string literals by default', (t) => {
    const dbPath = nextDb();
    const db = new DatabaseSync(dbPath);
    t.after(() => { db.close(); });
    t.assert.throws(() => {
      db.exec('SELECT "foo";');
    }, {
      code: 'ERR_SQLITE_ERROR',
      message: /no such column: "foo"/,
    });
  });

  test('allows enabling double-quoted string literals', (t) => {
    const dbPath = nextDb();
    const db = new DatabaseSync(dbPath, { enableDoubleQuotedStringLiterals: true });
    t.after(() => { db.close(); });
    db.exec('SELECT "foo";');
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
      { __proto__: null, key: 1, val: 2 },
      { __proto__: null, key: 8, val: 9 },
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
