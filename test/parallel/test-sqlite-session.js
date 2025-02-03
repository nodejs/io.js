// Flags: --experimental-sqlite
'use strict';
require('../common');
const {
  DatabaseSync,
  constants,
} = require('node:sqlite');
const { test, suite } = require('node:test');

/**
 * Convenience wrapper around assert.deepStrictEqual that sets a null
 * prototype to the expected object.
 */
function deepStrictEqual(t) {
  return (actual, expected, message) => {
    if (Array.isArray(expected)) {
      expected = expected.map((obj) => ({ ...obj, __proto__: null }));
    } else if (typeof expected === 'object') {
      expected = { ...expected, __proto__: null };
    }
    t.assert.deepStrictEqual(actual, expected, message);
  };
}

test('creating and applying a changeset', (t) => {
  const createDataTableSql = `
      CREATE TABLE data(
        key INTEGER PRIMARY KEY,
        value TEXT
      ) STRICT`;

  const createDatabase = () => {
    const database = new DatabaseSync(':memory:');
    database.exec(createDataTableSql);
    return database;
  };

  const databaseFrom = createDatabase();
  const session = databaseFrom.createSession();

  const select = 'SELECT * FROM data ORDER BY key';

  const insert = databaseFrom.prepare('INSERT INTO data (key, value) VALUES (?, ?)');
  insert.run(1, 'hello');
  insert.run(2, 'world');

  const databaseTo = createDatabase();

  t.assert.strictEqual(databaseTo.applyChangeset(session.changeset()), true);
  deepStrictEqual(t)(
    databaseFrom.prepare(select).all(),
    databaseTo.prepare(select).all()
  );
});

test('database.createSession() - closed database results in exception', (t) => {
  const database = new DatabaseSync(':memory:');
  database.close();
  t.assert.throws(() => {
    database.createSession();
  }, {
    name: 'Error',
    message: 'database is not open',
  });
});

test('session.changeset() - closed database results in exception', (t) => {
  const database = new DatabaseSync(':memory:');
  const session = database.createSession();
  database.close();
  t.assert.throws(() => {
    session.changeset();
  }, {
    name: 'Error',
    message: 'database is not open',
  });
});

test('database.applyChangeset() - closed database results in exception', (t) => {
  const database = new DatabaseSync(':memory:');
  const session = database.createSession();
  const changeset = session.changeset();
  database.close();
  t.assert.throws(() => {
    database.applyChangeset(changeset);
  }, {
    name: 'Error',
    message: 'database is not open',
  });
});

test('database.createSession() - use table option to track specific table', (t) => {
  const database1 = new DatabaseSync(':memory:');
  const database2 = new DatabaseSync(':memory:');

  const createData1TableSql = `CREATE TABLE data1 (
      key INTEGER PRIMARY KEY,
      value TEXT
    ) STRICT
    `;
  const createData2TableSql = `CREATE TABLE data2 (
      key INTEGER PRIMARY KEY,
      value TEXT
    ) STRICT
    `;
  database1.exec(createData1TableSql);
  database1.exec(createData2TableSql);
  database2.exec(createData1TableSql);
  database2.exec(createData2TableSql);

  const session = database1.createSession({
    table: 'data1'
  });
  const insert1 = database1.prepare('INSERT INTO data1 (key, value) VALUES (?, ?)');
  insert1.run(1, 'hello');
  insert1.run(2, 'world');
  const insert2 = database1.prepare('INSERT INTO data2 (key, value) VALUES (?, ?)');
  insert2.run(1, 'hello');
  insert2.run(2, 'world');
  const select1 = 'SELECT * FROM data1 ORDER BY key';
  const select2 = 'SELECT * FROM data2 ORDER BY key';
  t.assert.strictEqual(database2.applyChangeset(session.changeset()), true);
  deepStrictEqual(t)(
    database1.prepare(select1).all(),
    database2.prepare(select1).all());  // data1 table should be equal
  deepStrictEqual(t)(database2.prepare(select2).all(), []);  // data2 should be empty in database2
  t.assert.strictEqual(database1.prepare(select2).all().length, 2);  // data1 should have values in database1
});

suite('conflict resolution', () => {
  const createDataTableSql = `CREATE TABLE data (
      key INTEGER PRIMARY KEY,
      value TEXT UNIQUE
    ) STRICT`;

  const prepareConflict = () => {
    const database1 = new DatabaseSync(':memory:');
    const database2 = new DatabaseSync(':memory:');

    database1.exec(createDataTableSql);
    database2.exec(createDataTableSql);

    const insertSql = 'INSERT INTO data (key, value) VALUES (?, ?)';
    const session = database1.createSession();
    database1.prepare(insertSql).run(1, 'hello');
    database1.prepare(insertSql).run(2, 'foo');
    database2.prepare(insertSql).run(1, 'world');
    return {
      database2,
      changeset: session.changeset()
    };
  };

  const prepareDataConflict = () => {
    const database1 = new DatabaseSync(':memory:');
    const database2 = new DatabaseSync(':memory:');

    database1.exec(createDataTableSql);
    database2.exec(createDataTableSql);

    const insertSql = 'INSERT INTO data (key, value) VALUES (?, ?)';
    database1.prepare(insertSql).run(1, 'hello');
    database2.prepare(insertSql).run(1, 'othervalue');
    const session = database1.createSession();
    database1.prepare('UPDATE data SET value = ? WHERE key = ?').run('foo', 1);
    return {
      database2,
      changeset: session.changeset()
    };
  };

  const prepareNotFoundConflict = () => {
    const database1 = new DatabaseSync(':memory:');
    const database2 = new DatabaseSync(':memory:');

    database1.exec(createDataTableSql);
    database2.exec(createDataTableSql);

    const insertSql = 'INSERT INTO data (key, value) VALUES (?, ?)';
    database1.prepare(insertSql).run(1, 'hello');
    const session = database1.createSession();
    database1.prepare('DELETE FROM data WHERE key = 1').run();
    return {
      database2,
      changeset: session.changeset()
    };
  };

  const prepareFkConflict = () => {
    const database1 = new DatabaseSync(':memory:');
    const database2 = new DatabaseSync(':memory:');

    database1.exec(createDataTableSql);
    database2.exec(createDataTableSql);
    const fkTableSql = `CREATE TABLE other (
      key INTEGER PRIMARY KEY,
      ref REFERENCES data(key)
    )`;
    database1.exec(fkTableSql);
    database2.exec(fkTableSql);

    const insertDataSql = 'INSERT INTO data (key, value) VALUES (?, ?)';
    const insertOtherSql = 'INSERT INTO other (key, ref) VALUES (?, ?)';
    database1.prepare(insertDataSql).run(1, 'hello');
    database2.prepare(insertDataSql).run(1, 'hello');
    database1.prepare(insertOtherSql).run(1, 1);
    database2.prepare(insertOtherSql).run(1, 1);

    database1.exec('DELETE FROM other WHERE key = 1');  // So we don't get a fk violation in database1
    const session = database1.createSession();
    database1.prepare('DELETE FROM data WHERE key = 1').run(); // Changeset with fk violation
    database2.exec('PRAGMA foreign_keys = ON');  // Needs to be supported, otherwise will fail here

    return {
      database2,
      changeset: session.changeset()
    };
  };

  const prepareConstraintConflict = () => {
    const database1 = new DatabaseSync(':memory:');
    const database2 = new DatabaseSync(':memory:');

    database1.exec(createDataTableSql);
    database2.exec(createDataTableSql);

    const insertSql = 'INSERT INTO data (key, value) VALUES (?, ?)';
    const session = database1.createSession();
    database1.prepare(insertSql).run(1, 'hello');
    database2.prepare(insertSql).run(2, 'hello');  // database2 already constains hello

    return {
      database2,
      changeset: session.changeset()
    };
  };

  test('database.applyChangeset() - SQLITE_CHANGESET_CONFLICT conflict with default behavior (abort)', (t) => {
    const { database2, changeset } = prepareConflict();
    // When changeset is aborted due to a conflict, applyChangeset should return false
    t.assert.strictEqual(database2.applyChangeset(changeset), false);
    deepStrictEqual(t)(
      database2.prepare('SELECT value from data').all(),
      [{ value: 'world' }]);  // unchanged
  });

  test('database.applyChangeset() - SQLITE_CHANGESET_CONFLICT conflict handled with SQLITE_CHANGESET_ABORT', (t) => {
    const { database2, changeset } = prepareConflict();
    let conflictType = null;
    const result = database2.applyChangeset(changeset, {
      onConflict: (conflictType_) => {
        conflictType = conflictType_;
        return constants.SQLITE_CHANGESET_ABORT;
      }
    });
    // When changeset is aborted due to a conflict, applyChangeset should return false
    t.assert.strictEqual(result, false);
    t.assert.strictEqual(conflictType, constants.SQLITE_CHANGESET_CONFLICT);
    deepStrictEqual(t)(
      database2.prepare('SELECT value from data').all(),
      [{ value: 'world' }]);  // unchanged
  });

  test('database.applyChangeset() - SQLITE_CHANGESET_DATA conflict handled with SQLITE_CHANGESET_REPLACE', (t) => {
    const { database2, changeset } = prepareDataConflict();
    let conflictType = null;
    const result = database2.applyChangeset(changeset, {
      onConflict: (conflictType_) => {
        conflictType = conflictType_;
        return constants.SQLITE_CHANGESET_REPLACE;
      }
    });
    // Not aborted due to conflict, so should return true
    t.assert.strictEqual(result, true);
    t.assert.strictEqual(conflictType, constants.SQLITE_CHANGESET_DATA);
    deepStrictEqual(t)(
      database2.prepare('SELECT value from data ORDER BY key').all(),
      [{ value: 'foo' }]);  // replaced
  });

  test('database.applyChangeset() - SQLITE_CHANGESET_NOTFOUND conflict with SQLITE_CHANGESET_OMIT', (t) => {
    const { database2, changeset } = prepareNotFoundConflict();
    let conflictType = null;
    const result = database2.applyChangeset(changeset, {
      onConflict: (conflictType_) => {
        conflictType = conflictType_;
        return constants.SQLITE_CHANGESET_OMIT;
      }
    });
    // Not aborted due to conflict, so should return true
    t.assert.strictEqual(result, true);
    t.assert.strictEqual(conflictType, constants.SQLITE_CHANGESET_NOTFOUND);
    deepStrictEqual(t)(database2.prepare('SELECT value from data').all(), []);
  });

  test('database.applyChangeset() - SQLITE_CHANGESET_FOREIGN_KEY conflict', (t) => {
    const { database2, changeset } = prepareFkConflict();
    let conflictType = null;
    const result = database2.applyChangeset(changeset, {
      onConflict: (conflictType_) => {
        conflictType = conflictType_;
        return constants.SQLITE_CHANGESET_OMIT;
      }
    });
    // Not aborted due to conflict, so should return true
    t.assert.strictEqual(result, true);
    t.assert.strictEqual(conflictType, constants.SQLITE_CHANGESET_FOREIGN_KEY);
    deepStrictEqual(t)(database2.prepare('SELECT value from data').all(), []);
  });

  test('database.applyChangeset() - SQLITE_CHANGESET_CONSTRAINT conflict', (t) => {
    const { database2, changeset } = prepareConstraintConflict();
    let conflictType = null;
    const result = database2.applyChangeset(changeset, {
      onConflict: (conflictType_) => {
        conflictType = conflictType_;
        return constants.SQLITE_CHANGESET_OMIT;
      }
    });
    // Not aborted due to conflict, so should return true
    t.assert.strictEqual(result, true);
    t.assert.strictEqual(conflictType, constants.SQLITE_CHANGESET_CONSTRAINT);
    deepStrictEqual(t)(database2.prepare('SELECT key, value from data').all(), [{ key: 2, value: 'hello' }]);
  });

  test('conflict resolution handler returns invalid value', (t) => {
    const invalidHandlers = [
      () => -1,
      () => ({}),
      () => null,
      async () => constants.SQLITE_CHANGESET_ABORT,
    ];

    for (const invalidHandler of invalidHandlers) {
      const { database2, changeset } = prepareConflict();
      t.assert.throws(() => {
        database2.applyChangeset(changeset, {
          onConflict: invalidHandler
        });
      }, {
        name: 'Error',
        message: 'bad parameter or other API misuse',
        errcode: 21,
        code: 'ERR_SQLITE_ERROR'
      }, `Did not throw expected exception when using invalid onConflict handler: ${invalidHandler}`);
    }
  });

  test('conflict resolution handler throws', (t) => {
    const { database2, changeset } = prepareConflict();
    t.assert.throws(() => {
      database2.applyChangeset(changeset, {
        onConflict: () => {
          throw new Error('some error');
        }
      });
    }, {
      name: 'Error',
      message: 'some error'
    });
  });
});

test('database.createSession() - filter changes', (t) => {
  const database1 = new DatabaseSync(':memory:');
  const database2 = new DatabaseSync(':memory:');
  const createTableSql = 'CREATE TABLE data1(key INTEGER PRIMARY KEY); CREATE TABLE data2(key INTEGER PRIMARY KEY);';
  database1.exec(createTableSql);
  database2.exec(createTableSql);

  const session = database1.createSession();

  database1.exec('INSERT INTO data1 (key) VALUES (1), (2), (3)');
  database1.exec('INSERT INTO data2 (key) VALUES (1), (2), (3), (4), (5)');

  database2.applyChangeset(session.changeset(), {
    filter: (tableName) => tableName === 'data2'
  });

  const data1Rows = database2.prepare('SELECT * FROM data1').all();
  const data2Rows = database2.prepare('SELECT * FROM data2').all();

  // Expect no rows since all changes were filtered out
  t.assert.strictEqual(data1Rows.length, 0);
  // Expect 5 rows since these changes were not filtered out
  t.assert.strictEqual(data2Rows.length, 5);
});

test('database.createSession() - specify other database', (t) => {
  const database = new DatabaseSync(':memory:');
  const session = database.createSession();
  const sessionMain = database.createSession({
    db: 'main'
  });
  const sessionTest = database.createSession({
    db: 'test'
  });
  database.exec('CREATE TABLE data (key INTEGER PRIMARY KEY)');
  database.exec('INSERT INTO data (key) VALUES (1)');
  t.assert.notStrictEqual(session.changeset().length, 0);
  t.assert.notStrictEqual(sessionMain.changeset().length, 0);
  // Since this session is attached to a different database, its changeset should be empty
  t.assert.strictEqual(sessionTest.changeset().length, 0);
});

test('database.createSession() - wrong arguments', (t) => {
  const database = new DatabaseSync(':memory:');
  t.assert.throws(() => {
    database.createSession(null);
  }, {
    name: 'TypeError',
    message: 'The "options" argument must be an object.'
  });

  t.assert.throws(() => {
    database.createSession({
      table: 123
    });
  }, {
    name: 'TypeError',
    message: 'The "options.table" argument must be a string.'
  });

  t.assert.throws(() => {
    database.createSession({
      db: 123
    });
  }, {
    name: 'TypeError',
    message: 'The "options.db" argument must be a string.'
  });
});

test('database.applyChangeset() - wrong arguments', (t) => {
  const database = new DatabaseSync(':memory:');
  const session = database.createSession();
  t.assert.throws(() => {
    database.applyChangeset(null);
  }, {
    name: 'TypeError',
    message: 'The "changeset" argument must be a Uint8Array.'
  });

  t.assert.throws(() => {
    database.applyChangeset(session.changeset(), null);
  }, {
    name: 'TypeError',
    message: 'The "options" argument must be an object.'
  });

  t.assert.throws(() => {
    database.applyChangeset(session.changeset(), {
      filter: null
    }, null);
  }, {
    name: 'TypeError',
    message: 'The "options.filter" argument must be a function.'
  });

  t.assert.throws(() => {
    database.applyChangeset(session.changeset(), {
      onConflict: null
    }, null);
  }, {
    name: 'TypeError',
    message: 'The "options.onConflict" argument must be a function.'
  });
});

test('session.patchset()', (t) => {
  const database = new DatabaseSync(':memory:');
  database.exec('CREATE TABLE data(key INTEGER PRIMARY KEY, value TEXT)');

  database.exec("INSERT INTO data VALUES ('1', 'Lorem ipsum dolor sit amet, consectetur adipiscing elit.')");

  const session = database.createSession();
  database.exec("UPDATE data SET value = 'hi' WHERE key = 1");

  const patchset = session.patchset();
  const changeset = session.changeset();

  t.assert.ok(patchset instanceof Uint8Array);
  t.assert.ok(changeset instanceof Uint8Array);

  t.assert.deepStrictEqual(patchset, session.patchset());
  t.assert.deepStrictEqual(changeset, session.changeset());

  t.assert.ok(
    patchset.length < changeset.length,
    'expected patchset to be smaller than changeset');
});

test('session.close() - using session after close throws exception', (t) => {
  const database = new DatabaseSync(':memory:');
  database.exec('CREATE TABLE data(key INTEGER PRIMARY KEY, value TEXT)');

  database.exec("INSERT INTO data VALUES ('1', 'Lorem ipsum dolor sit amet, consectetur adipiscing elit.')");

  const session = database.createSession();
  database.exec("UPDATE data SET value = 'hi' WHERE key = 1");
  session.close();

  database.exec("UPDATE data SET value = 'world' WHERE key = 1");
  t.assert.throws(() => {
    session.changeset();
  }, {
    name: 'Error',
    message: 'session is not open'
  });
});

test('session.close() - after closing database throws exception', (t) => {
  const database = new DatabaseSync(':memory:');
  database.exec('CREATE TABLE data(key INTEGER PRIMARY KEY, value TEXT)');

  database.exec("INSERT INTO data VALUES ('1', 'Lorem ipsum dolor sit amet, consectetur adipiscing elit.')");

  const session = database.createSession();
  database.close();

  t.assert.throws(() => {
    session.close();
  }, {
    name: 'Error',
    message: 'database is not open'
  });
});

test('session.close() - closing twice', (t) => {
  const database = new DatabaseSync(':memory:');
  const session = database.createSession();
  session.close();

  t.assert.throws(() => {
    session.close();
  }, {
    name: 'Error',
    message: 'session is not open'
  });
});
