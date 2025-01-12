import common from '../common/index.js';
import tmpdir from '../common/tmpdir.js';
import { join } from 'path';
import { DatabaseSync } from 'node:sqlite';
import { test } from 'node:test';
import { writeFileSync } from 'fs';

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

test('throws exception when trying to start backup from a closed database', async (t) => {
  t.assert.rejects(async () => {
    const database = new DatabaseSync(':memory:');

    database.close();

    await database.backup('backup.db');
  }, common.expectsError({
    code: 'ERR_INVALID_STATE',
    message: 'database is not open'
  }));
});

test('database backup', async (t) => {
  const progressFn = t.mock.fn();
  const database = makeSourceDb();
  const destDb = nextDb();

  await database.backup(destDb, {
    rate: 1,
    progress: progressFn,
  });

  const backup = new DatabaseSync(destDb);
  const rows = backup.prepare('SELECT * FROM data').all();

  // The source database has two pages - using the default page size -, so the progress function should be called once (the last call is not made since
  // the promise resolves)
  t.assert.strictEqual(progressFn.mock.calls.length, 1);
  t.assert.deepStrictEqual(rows, [
    { __proto__: null, key: 1, value: 'value-1' },
    { __proto__: null, key: 2, value: 'value-2' },
  ]);
});

test('database backup fails when dest file is not writable', (t) => {
  const readonlyDestDb = nextDb();
  writeFileSync(readonlyDestDb, '', { mode: 0o444 });

  const database = makeSourceDb();

  t.assert.rejects(async () => {
    await database.backup(readonlyDestDb);
  }, common.expectsError({
    code: 'ERR_SQLITE_ERROR',
    message: 'attempt to write a readonly database'
  }));
});

