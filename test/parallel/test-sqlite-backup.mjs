import '../common/index.mjs';
import tmpdir from '../common/tmpdir.js';
import { join } from 'node:path';
import { backup, DatabaseSync } from 'node:sqlite';
import { describe, test } from 'node:test';
import { writeFileSync } from 'node:fs';

let cnt = 0;

tmpdir.refresh();

function nextDb() {
  return join(tmpdir.path, `database-${cnt++}.db`);
}

function makeSourceDb() {
  const database = new DatabaseSync(':memory:');

  database.exec(`
    CREATE TABLE data(
      key INTEGER PRIMARY KEY,
      value TEXT
    ) STRICT
  `);

  const insert = database.prepare('INSERT INTO data (key, value) VALUES (?, ?)');

  for (let i = 1; i <= 2; i++) {
    insert.run(i, `value-${i}`);
  }

  return database;
}

describe('backup()', () => {
  test('throws if the source database is not provided', (t) => {
    t.assert.throws(() => {
      backup();
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      message: 'The "sourceDb" argument must be an object.'
    });
  });

  test('throws if path is not a string', (t) => {
    const database = makeSourceDb();

    t.assert.throws(() => {
      backup(database);
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      message: 'The "destination" argument must be a string.'
    });

    t.assert.throws(() => {
      backup(database, {});
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      message: 'The "destination" argument must be a string.'
    });
  });

  test('throws if options is not an object', (t) => {
    const database = makeSourceDb();

    t.assert.throws(() => {
      backup(database, 'hello.db', 'invalid');
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      message: 'The "options" argument must be an object.'
    });
  });

  test('throws if any of provided options is invalid', (t) => {
    const database = makeSourceDb();

    t.assert.throws(() => {
      backup(database, 'hello.db', {
        source: 42
      });
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      message: 'The "options.source" argument must be a string.'
    });

    t.assert.throws(() => {
      backup(database, 'hello.db', {
        target: 42
      });
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      message: 'The "options.target" argument must be a string.'
    });

    t.assert.throws(() => {
      backup(database, 'hello.db', {
        rate: 'invalid'
      });
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      message: 'The "options.rate" argument must be an integer.'
    });

    t.assert.throws(() => {
      backup(database, 'hello.db', {
        progress: 'invalid'
      });
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      message: 'The "options.progress" argument must be a function.'
    });
  });
});

test('database backup', async (t) => {
  const progressFn = t.mock.fn();
  const database = makeSourceDb();
  const destDb = nextDb();

  await backup(database, destDb, {
    rate: 1,
    progress: progressFn,
  });

  const backupDb = new DatabaseSync(destDb);
  const rows = backupDb.prepare('SELECT * FROM data').all();

  // The source database has two pages - using the default page size -,
  // so the progress function should be called once (the last call is not made since
  // the promise resolves)
  t.assert.strictEqual(progressFn.mock.calls.length, 1);
  t.assert.deepStrictEqual(progressFn.mock.calls[0].arguments, [{ totalPages: 2, remainingPages: 1 }]);
  t.assert.deepStrictEqual(rows, [
    { __proto__: null, key: 1, value: 'value-1' },
    { __proto__: null, key: 2, value: 'value-2' },
  ]);

  t.after(() => {
    database.close();
    backupDb.close();
  });
});

test('database backup in a single call', async (t) => {
  const progressFn = t.mock.fn();
  const database = makeSourceDb();
  const destDb = nextDb();

  // Let rate to be default (100) to backup in a single call
  await backup(database, destDb, {
    progress: progressFn,
  });

  const backupDb = new DatabaseSync(destDb);
  const rows = backupDb.prepare('SELECT * FROM data').all();

  t.assert.strictEqual(progressFn.mock.calls.length, 0);
  t.assert.deepStrictEqual(rows, [
    { __proto__: null, key: 1, value: 'value-1' },
    { __proto__: null, key: 2, value: 'value-2' },
  ]);

  t.after(() => {
    database.close();
    backupDb.close();
  });
});

test('throws exception when trying to start backup from a closed database', (t) => {
  t.assert.throws(() => {
    const database = new DatabaseSync(':memory:');

    database.close();

    backup(database, 'backup.db');
  }, {
    code: 'ERR_INVALID_STATE',
    message: 'database is not open'
  });
});

test('database backup fails when dest file is not writable', async (t) => {
  const readonlyDestDb = nextDb();
  writeFileSync(readonlyDestDb, '', { mode: 0o444 });

  const database = makeSourceDb();

  await t.assert.rejects(async () => {
    await backup(database, readonlyDestDb);
  }, {
    code: 'ERR_SQLITE_ERROR',
    message: 'attempt to write a readonly database'
  });
});

test('backup fails when progress function throws', async (t) => {
  const database = makeSourceDb();
  const destDb = nextDb();

  const progressFn = t.mock.fn(() => {
    throw new Error('progress error');
  });

  await t.assert.rejects(async () => {
    await backup(database, destDb, {
      rate: 1,
      progress: progressFn,
    });
  }, {
    message: 'progress error'
  });
});

test('backup fails when source db is invalid', async (t) => {
  const database = makeSourceDb();
  const destDb = nextDb();

  await t.assert.rejects(async () => {
    await backup(database, destDb, {
      rate: 1,
      source: 'invalid',
    });
  }, {
    message: 'unknown database invalid'
  });
});

test('backup fails when destination cannot be opened', async (t) => {
  const database = makeSourceDb();

  await t.assert.rejects(async () => {
    await backup(database, `${tmpdir.path}/invalid/backup.db`);
  }, {
    message: 'unable to open database file'
  });
});
