// Flags: --experimental-sqlite
'use strict';
const { spawnPromisified } = require('../common');
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

suite('accessing the node:sqlite module', () => {
  test('cannot be accessed without the node: scheme', (t) => {
    t.assert.throws(() => {
      require('sqlite');
    }, {
      code: 'MODULE_NOT_FOUND',
      message: /Cannot find module 'sqlite'/,
    });
  });

  test('cannot be accessed without --experimental-sqlite flag', async (t) => {
    const {
      stdout,
      stderr,
      code,
      signal,
    } = await spawnPromisified(process.execPath, [
      '-e',
      'require("node:sqlite")',
    ]);

    t.assert.strictEqual(stdout, '');
    t.assert.match(stderr, /No such built-in module: node:sqlite/);
    t.assert.notStrictEqual(code, 0);
    t.assert.strictEqual(signal, null);
  });
});

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

    t.assert.strictEqual(existsSync(dbPath), false);
    t.assert.strictEqual(db.open(), undefined);
    t.assert.strictEqual(existsSync(dbPath), true);
  });

  test('throws if database is already open', (t) => {
    const db = new DatabaseSync(nextDb(), { open: false });

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

    t.assert.throws(() => {
      db.exec();
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /The "sql" argument must be a string/,
    });
  });
});

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
    let stmt = db.prepare('CREATE TABLE storage(key TEXT, val TEXT)');
    t.assert.strictEqual(stmt.get(), undefined);
    stmt = db.prepare('SELECT * FROM storage');
    t.assert.strictEqual(stmt.get(), undefined);
  });

  test('executes a query and returns the first result', (t) => {
    const db = new DatabaseSync(nextDb());
    let stmt = db.prepare('CREATE TABLE storage(key TEXT, val TEXT)');
    t.assert.strictEqual(stmt.get(), undefined);
    stmt = db.prepare('INSERT INTO storage (key, val) VALUES (?, ?)');
    t.assert.strictEqual(stmt.get('key1', 'val1'), undefined);
    t.assert.strictEqual(stmt.get('key2', 'val2'), undefined);
    stmt = db.prepare('SELECT * FROM storage ORDER BY key');
    t.assert.deepStrictEqual(stmt.get(), { key: 'key1', val: 'val1' });
  });
});

suite('StatementSync.prototype.all()', () => {
  test('executes a query and returns an empty array on no results', (t) => {
    const db = new DatabaseSync(nextDb());
    const stmt = db.prepare('CREATE TABLE storage(key TEXT, val TEXT)');
    t.assert.deepStrictEqual(stmt.all(), []);
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
    stmt = db.prepare('SELECT * FROM storage ORDER BY key');
    t.assert.deepStrictEqual(stmt.all(), [
      { key: 'key1', val: 'val1' },
      { key: 'key2', val: 'val2' },
    ]);
  });
});

suite('StatementSync.prototype.run()', () => {
  test('executes a query and returns change metadata', (t) => {
    const db = new DatabaseSync(nextDb());
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

suite('StatementSync.prototype.sourceSQL()', () => {
  test('returns input SQL', (t) => {
    const db = new DatabaseSync(nextDb());
    const setup = db.exec(
      'CREATE TABLE types(key INTEGER PRIMARY KEY, val INTEGER) STRICT;'
    );
    t.assert.strictEqual(setup, undefined);
    const sql = 'INSERT INTO types (key, val) VALUES ($k, $v)';
    const stmt = db.prepare(sql);
    t.assert.strictEqual(stmt.sourceSQL(), sql);
  });
});

suite('StatementSync.prototype.expandedSQL()', () => {
  test('returns expanded SQL', (t) => {
    const db = new DatabaseSync(nextDb());
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
    t.assert.strictEqual(stmt.expandedSQL(), expanded);
  });
});

suite('StatementSync.prototype.setReadBigInts()', () => {
  test('BigInts support can be toggled', (t) => {
    const db = new DatabaseSync(nextDb());
    const setup = db.exec(`
      CREATE TABLE data(key INTEGER PRIMARY KEY, val INTEGER) STRICT;
      INSERT INTO data (key, val) VALUES (1, 42);
    `);
    t.assert.strictEqual(setup, undefined);

    const query = db.prepare('SELECT val FROM data');
    t.assert.deepStrictEqual(query.get(), { val: 42 });
    t.assert.strictEqual(query.setReadBigInts(true), undefined);
    t.assert.deepStrictEqual(query.get(), { val: 42n });
    t.assert.strictEqual(query.setReadBigInts(false), undefined);
    t.assert.deepStrictEqual(query.get(), { val: 42 });

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
      [`${Number.MAX_SAFE_INTEGER} + 1`]: 2n ** 53n,
    });
  });
});

suite('StatementSync.prototype.setAllowBareNamedParameters()', () => {
  test('bare named parameter support can be toggled', (t) => {
    const db = new DatabaseSync(nextDb());
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

suite('data binding and mapping', () => {
  test('supported data types', (t) => {
    const u8a = new TextEncoder().encode('a☃b☃c');
    const db = new DatabaseSync(nextDb());
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
      key: 1,
      int: 42,
      double: 3.14159,
      text: 'foo',
      buf: u8a,
    });
    t.assert.deepStrictEqual(query.get(2), {
      key: 2,
      int: null,
      double: null,
      text: null,
      buf: null,
    });
    t.assert.deepStrictEqual(query.get(3), {
      key: 3,
      int: 8,
      double: 2.718,
      text: 'bar',
      buf: new TextEncoder().encode('x☃y☃'),
    });
    t.assert.deepStrictEqual(query.get(4), {
      key: 4,
      int: 99,
      double: 0xf,
      text: '',
      buf: new Uint8Array(),
    });
  });

  test('unsupported data types', (t) => {
    const db = new DatabaseSync(nextDb());
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
      [{ key: 1, val: 5 }, { key: 2, val: null }],
    );
  });
});

suite('manual transactions', () => {
  test('a transaction is committed', (t) => {
    const db = new DatabaseSync(nextDb());
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
      [{ key: 100 }],
    );
  });

  test('a transaction is rolled back', (t) => {
    const db = new DatabaseSync(nextDb());
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

suite('named parameters', () => {
  test('throws on unknown named parameters', (t) => {
    const db = new DatabaseSync(nextDb());
    const setup = db.exec(
      'CREATE TABLE types(key INTEGER PRIMARY KEY, val INTEGER) STRICT;'
    );
    t.assert.strictEqual(setup, undefined);

    t.assert.throws(() => {
      const stmt = db.prepare('INSERT INTO types (key, val) VALUES ($k, $v)');
      stmt.run({ $k: 1, $unknown: 1 });
    }, {
      code: 'ERR_INVALID_STATE',
      message: /Unknown named parameter '\$unknown'/,
    });
  });

  test('bare named parameters are supported', (t) => {
    const db = new DatabaseSync(nextDb());
    const setup = db.exec(
      'CREATE TABLE data(key INTEGER PRIMARY KEY, val INTEGER) STRICT;'
    );
    t.assert.strictEqual(setup, undefined);
    const stmt = db.prepare('INSERT INTO data (key, val) VALUES ($k, $v)');
    stmt.run({ k: 1, v: 9 });
    t.assert.deepStrictEqual(
      db.prepare('SELECT * FROM data').get(),
      { key: 1, val: 9 },
    );
  });

  test('duplicate bare named parameters are supported', (t) => {
    const db = new DatabaseSync(nextDb());
    const setup = db.exec(
      'CREATE TABLE data(key INTEGER PRIMARY KEY, val INTEGER) STRICT;'
    );
    t.assert.strictEqual(setup, undefined);
    const stmt = db.prepare('INSERT INTO data (key, val) VALUES ($k, $k)');
    stmt.run({ k: 1 });
    t.assert.deepStrictEqual(
      db.prepare('SELECT * FROM data').get(),
      { key: 1, val: 1 },
    );
  });

  test('bare named parameters throw on ambiguous names', (t) => {
    const db = new DatabaseSync(nextDb());
    const setup = db.exec(
      'CREATE TABLE types(key INTEGER PRIMARY KEY, val INTEGER) STRICT;'
    );
    t.assert.strictEqual(setup, undefined);
    const stmt = db.prepare('INSERT INTO types (key, val) VALUES ($k, @k)');
    t.assert.throws(() => {
      stmt.run({ k: 1 });
    }, {
      code: 'ERR_INVALID_STATE',
      message: 'Cannot create bare named parameter \'k\' because of ' +
               'conflicting names \'$k\' and \'@k\'.',
    });
  });
});

test('ERR_SQLITE_ERROR is thrown for errors originating from SQLite', (t) => {
  const db = new DatabaseSync(nextDb());
  const setup = db.exec(`
    CREATE TABLE test(
      key INTEGER PRIMARY KEY
    ) STRICT;
  `);
  t.assert.strictEqual(setup, undefined);
  const stmt = db.prepare('INSERT INTO test (key) VALUES (?)');
  t.assert.deepStrictEqual(stmt.run(1), { changes: 1, lastInsertRowid: 1 });
  t.assert.throws(() => {
    stmt.run(1);
  }, {
    code: 'ERR_SQLITE_ERROR',
    message: 'UNIQUE constraint failed: test.key',
    errcode: 1555,
    errstr: 'constraint failed',
  });
});

test('in-memory databases are supported', (t) => {
  const db1 = new DatabaseSync(':memory:');
  const db2 = new DatabaseSync(':memory:');
  const setup1 = db1.exec(`
    CREATE TABLE data(key INTEGER PRIMARY KEY);
    INSERT INTO data (key) VALUES (1);
  `);
  const setup2 = db2.exec(`
    CREATE TABLE data(key INTEGER PRIMARY KEY);
    INSERT INTO data (key) VALUES (1);
  `);
  t.assert.strictEqual(setup1, undefined);
  t.assert.strictEqual(setup2, undefined);
  t.assert.deepStrictEqual(
    db1.prepare('SELECT * FROM data').all(),
    [{ key: 1 }]
  );
  t.assert.deepStrictEqual(
    db2.prepare('SELECT * FROM data').all(),
    [{ key: 1 }]
  );
});

test('PRAGMAs are supported', (t) => {
  const db = new DatabaseSync(nextDb());
  t.assert.deepStrictEqual(
    db.prepare('PRAGMA journal_mode = WAL').get(),
    { journal_mode: 'wal' },
  );
  t.assert.deepStrictEqual(
    db.prepare('PRAGMA journal_mode').get(),
    { journal_mode: 'wal' },
  );
});
