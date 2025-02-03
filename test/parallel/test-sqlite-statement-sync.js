'use strict';
require('../common');
const tmpdir = require('../common/tmpdir');
const { join } = require('node:path');
const { DatabaseSync, StatementSync } = require('node:sqlite');
const { suite, test } = require('node:test');
let cnt = 0;

tmpdir.refresh();

function nextDb() {
  return join(tmpdir.path, `database-${cnt++}.db`);
}

suite('StatementSync() constructor', () => {
  test('StatementSync cannot be constructed directly', (t) => {
    t.assert.throws(() => {
      new StatementSync();
    }, {
      code: 'ERR_ILLEGAL_CONSTRUCTOR',
      message: /Illegal constructor/,
    });
  });
});

suite('StatementSync.prototype.get()', () => {
  test('executes a query and returns undefined on no results', (t) => {
    const db = new DatabaseSync(nextDb());
    t.after(() => { db.close(); });
    let stmt = db.prepare('CREATE TABLE storage(key TEXT, val TEXT)');
    t.assert.strictEqual(stmt.get(), undefined);
    stmt = db.prepare('SELECT * FROM storage');
    t.assert.strictEqual(stmt.get(), undefined);
  });

  test('executes a query and returns the first result', (t) => {
    const db = new DatabaseSync(nextDb());
    t.after(() => { db.close(); });
    let stmt = db.prepare('CREATE TABLE storage(key TEXT, val TEXT)');
    t.assert.strictEqual(stmt.get(), undefined);
    stmt = db.prepare('INSERT INTO storage (key, val) VALUES (?, ?)');
    t.assert.strictEqual(stmt.get('key1', 'val1'), undefined);
    t.assert.strictEqual(stmt.get('key2', 'val2'), undefined);
    stmt = db.prepare('SELECT * FROM storage ORDER BY key');
    t.assert.deepStrictEqual(stmt.get(), { __proto__: null, key: 'key1', val: 'val1' });
  });

  test('executes a query that returns special columns', (t) => {
    const db = new DatabaseSync(nextDb());
    t.after(() => { db.close(); });
    const stmt = db.prepare('SELECT 1 as __proto__, 2 as constructor, 3 as toString');
    // eslint-disable-next-line no-dupe-keys
    t.assert.deepStrictEqual(stmt.get(), { __proto__: null, ['__proto__']: 1, constructor: 2, toString: 3 });
  });
});

suite('StatementSync.prototype.all()', () => {
  test('executes a query and returns an empty array on no results', (t) => {
    const db = new DatabaseSync(nextDb());
    t.after(() => { db.close(); });
    const stmt = db.prepare('CREATE TABLE storage(key TEXT, val TEXT)');
    t.assert.deepStrictEqual(stmt.all(), []);
  });

  test('executes a query and returns all results', (t) => {
    const db = new DatabaseSync(nextDb());
    t.after(() => { db.close(); });
    let stmt = db.prepare('CREATE TABLE storage(key TEXT, val TEXT)');
    t.assert.deepStrictEqual(stmt.run(), { changes: 0, lastInsertRowid: 0 });
    stmt = db.prepare('INSERT INTO storage (key, val) VALUES (?, ?)');
    t.assert.deepStrictEqual(
      stmt.run('key1', 'val1'),
      { changes: 1, lastInsertRowid: 1 },
    );
    t.assert.deepStrictEqual(
      stmt.run('key2', 'val2'),
      { changes: 1, lastInsertRowid: 2 },
    );
    stmt = db.prepare('SELECT * FROM storage ORDER BY key');
    t.assert.deepStrictEqual(stmt.all(), [
      { __proto__: null, key: 'key1', val: 'val1' },
      { __proto__: null, key: 'key2', val: 'val2' },
    ]);
  });
});

suite('StatementSync.prototype.iterate()', () => {
  test('executes a query and returns an empty iterator on no results', (t) => {
    const db = new DatabaseSync(nextDb());
    const stmt = db.prepare('CREATE TABLE storage(key TEXT, val TEXT)');
    t.assert.deepStrictEqual(stmt.iterate().toArray(), []);
  });

  test('executes a query and returns all results', (t) => {
    const db = new DatabaseSync(nextDb());
    let stmt = db.prepare('CREATE TABLE storage(key TEXT, val TEXT)');
    t.assert.deepStrictEqual(stmt.run(), { changes: 0, lastInsertRowid: 0 });
    stmt = db.prepare('INSERT INTO storage (key, val) VALUES (?, ?)');
    t.assert.deepStrictEqual(
      stmt.run('key1', 'val1'),
      { changes: 1, lastInsertRowid: 1 },
    );
    t.assert.deepStrictEqual(
      stmt.run('key2', 'val2'),
      { changes: 1, lastInsertRowid: 2 },
    );

    const items = [
      { __proto__: null, key: 'key1', val: 'val1' },
      { __proto__: null, key: 'key2', val: 'val2' },
    ];

    stmt = db.prepare('SELECT * FROM storage ORDER BY key');
    t.assert.deepStrictEqual(stmt.iterate().toArray(), items);

    const itemsLoop = items.slice();
    for (const item of stmt.iterate()) {
      t.assert.deepStrictEqual(item, itemsLoop.shift());
    }
  });
});

suite('StatementSync.prototype.run()', () => {
  test('executes a query and returns change metadata', (t) => {
    const db = new DatabaseSync(nextDb());
    t.after(() => { db.close(); });
    const setup = db.exec(`
      CREATE TABLE storage(key TEXT, val TEXT);
      INSERT INTO storage (key, val) VALUES ('foo', 'bar');
    `);
    t.assert.strictEqual(setup, undefined);
    const stmt = db.prepare('SELECT * FROM storage');
    t.assert.deepStrictEqual(stmt.run(), { changes: 1, lastInsertRowid: 1 });
  });

  test('SQLite throws when trying to bind too many parameters', (t) => {
    const db = new DatabaseSync(nextDb());
    t.after(() => { db.close(); });
    const setup = db.exec(
      'CREATE TABLE data(key INTEGER PRIMARY KEY, val INTEGER) STRICT;'
    );
    t.assert.strictEqual(setup, undefined);
    const stmt = db.prepare('INSERT INTO data (key, val) VALUES (?, ?)');
    t.assert.throws(() => {
      stmt.run(1, 2, 3);
    }, {
      code: 'ERR_SQLITE_ERROR',
      message: 'column index out of range',
      errcode: 25,
      errstr: 'column index out of range',
    });
  });

  test('SQLite defaults to NULL for unbound parameters', (t) => {
    const db = new DatabaseSync(nextDb());
    t.after(() => { db.close(); });
    const setup = db.exec(
      'CREATE TABLE data(key INTEGER PRIMARY KEY, val INTEGER NOT NULL) STRICT;'
    );
    t.assert.strictEqual(setup, undefined);
    const stmt = db.prepare('INSERT INTO data (key, val) VALUES (?, ?)');
    t.assert.throws(() => {
      stmt.run(1);
    }, {
      code: 'ERR_SQLITE_ERROR',
      message: 'NOT NULL constraint failed: data.val',
      errcode: 1299,
      errstr: 'constraint failed',
    });
  });
});

suite('StatementSync.prototype.sourceSQL', () => {
  test('equals input SQL', (t) => {
    const db = new DatabaseSync(nextDb());
    t.after(() => { db.close(); });
    const setup = db.exec(
      'CREATE TABLE types(key INTEGER PRIMARY KEY, val INTEGER) STRICT;'
    );
    t.assert.strictEqual(setup, undefined);
    const sql = 'INSERT INTO types (key, val) VALUES ($k, $v)';
    const stmt = db.prepare(sql);
    t.assert.strictEqual(stmt.sourceSQL, sql);
  });
});

suite('StatementSync.prototype.expandedSQL', () => {
  test('equals expanded SQL', (t) => {
    const db = new DatabaseSync(nextDb());
    t.after(() => { db.close(); });
    const setup = db.exec(
      'CREATE TABLE types(key INTEGER PRIMARY KEY, val INTEGER) STRICT;'
    );
    t.assert.strictEqual(setup, undefined);
    const sql = 'INSERT INTO types (key, val) VALUES ($k, ?)';
    const expanded = 'INSERT INTO types (key, val) VALUES (\'33\', \'42\')';
    const stmt = db.prepare(sql);
    t.assert.deepStrictEqual(
      stmt.run({ $k: '33' }, '42'),
      { changes: 1, lastInsertRowid: 33 },
    );
    t.assert.strictEqual(stmt.expandedSQL, expanded);
  });
});

suite('StatementSync.prototype.setReadBigInts()', () => {
  test('BigInts support can be toggled', (t) => {
    const db = new DatabaseSync(nextDb());
    t.after(() => { db.close(); });
    const setup = db.exec(`
      CREATE TABLE data(key INTEGER PRIMARY KEY, val INTEGER) STRICT;
      INSERT INTO data (key, val) VALUES (1, 42);
    `);
    t.assert.strictEqual(setup, undefined);

    const query = db.prepare('SELECT val FROM data');
    t.assert.deepStrictEqual(query.get(), { __proto__: null, val: 42 });
    t.assert.strictEqual(query.setReadBigInts(true), undefined);
    t.assert.deepStrictEqual(query.get(), { __proto__: null, val: 42n });
    t.assert.strictEqual(query.setReadBigInts(false), undefined);
    t.assert.deepStrictEqual(query.get(), { __proto__: null, val: 42 });

    const insert = db.prepare('INSERT INTO data (key) VALUES (?)');
    t.assert.deepStrictEqual(
      insert.run(10),
      { changes: 1, lastInsertRowid: 10 },
    );
    t.assert.strictEqual(insert.setReadBigInts(true), undefined);
    t.assert.deepStrictEqual(
      insert.run(20),
      { changes: 1n, lastInsertRowid: 20n },
    );
    t.assert.strictEqual(insert.setReadBigInts(false), undefined);
    t.assert.deepStrictEqual(
      insert.run(30),
      { changes: 1, lastInsertRowid: 30 },
    );
  });

  test('throws when input is not a boolean', (t) => {
    const db = new DatabaseSync(nextDb());
    t.after(() => { db.close(); });
    const setup = db.exec(
      'CREATE TABLE types(key INTEGER PRIMARY KEY, val INTEGER) STRICT;'
    );
    t.assert.strictEqual(setup, undefined);
    const stmt = db.prepare('INSERT INTO types (key, val) VALUES ($k, $v)');
    t.assert.throws(() => {
      stmt.setReadBigInts();
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /The "readBigInts" argument must be a boolean/,
    });
  });

  test('BigInt is required for reading large integers', (t) => {
    const db = new DatabaseSync(nextDb());
    t.after(() => { db.close(); });
    const bad = db.prepare(`SELECT ${Number.MAX_SAFE_INTEGER} + 1`);
    t.assert.throws(() => {
      bad.get();
    }, {
      code: 'ERR_OUT_OF_RANGE',
      message: /^The value of column 0 is too large.*: 9007199254740992$/,
    });
    const good = db.prepare(`SELECT ${Number.MAX_SAFE_INTEGER} + 1`);
    good.setReadBigInts(true);
    t.assert.deepStrictEqual(good.get(), {
      __proto__: null,
      [`${Number.MAX_SAFE_INTEGER} + 1`]: 2n ** 53n,
    });
  });
});

suite('StatementSync.prototype.setAllowBareNamedParameters()', () => {
  test('bare named parameter support can be toggled', (t) => {
    const db = new DatabaseSync(nextDb());
    t.after(() => { db.close(); });
    const setup = db.exec(
      'CREATE TABLE data(key INTEGER PRIMARY KEY, val INTEGER) STRICT;'
    );
    t.assert.strictEqual(setup, undefined);
    const stmt = db.prepare('INSERT INTO data (key, val) VALUES ($k, $v)');
    t.assert.deepStrictEqual(
      stmt.run({ k: 1, v: 2 }),
      { changes: 1, lastInsertRowid: 1 },
    );
    t.assert.strictEqual(stmt.setAllowBareNamedParameters(false), undefined);
    t.assert.throws(() => {
      stmt.run({ k: 2, v: 4 });
    }, {
      code: 'ERR_INVALID_STATE',
      message: /Unknown named parameter 'k'/,
    });
    t.assert.strictEqual(stmt.setAllowBareNamedParameters(true), undefined);
    t.assert.deepStrictEqual(
      stmt.run({ k: 3, v: 6 }),
      { changes: 1, lastInsertRowid: 3 },
    );
  });

  test('throws when input is not a boolean', (t) => {
    const db = new DatabaseSync(nextDb());
    t.after(() => { db.close(); });
    const setup = db.exec(
      'CREATE TABLE data(key INTEGER PRIMARY KEY, val INTEGER) STRICT;'
    );
    t.assert.strictEqual(setup, undefined);
    const stmt = db.prepare('INSERT INTO data (key, val) VALUES ($k, $v)');
    t.assert.throws(() => {
      stmt.setAllowBareNamedParameters();
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /The "allowBareNamedParameters" argument must be a boolean/,
    });
  });
});
