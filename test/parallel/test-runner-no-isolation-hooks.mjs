import * as common from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import { test } from 'node:test';

const testArguments = [
  '--test',
  '--test-isolation=none',
];

const testFiles = [
  fixtures.path('test-runner', 'no-isolation', 'one.test.js'),
  fixtures.path('test-runner', 'no-isolation', 'two.test.js'),
];

const order = [
  'before(): global',

  'before one: <root>',
  'suite one',

  'before two: <root>',
  'suite two',

  'beforeEach(): global',
  'beforeEach one: suite one - test',
  'beforeEach two: suite one - test',

  'suite one - test',
  'afterEach(): global',
  'afterEach one: suite one - test',
  'afterEach two: suite one - test',

  'before suite two: suite two',
  'beforeEach(): global',
  'beforeEach one: suite two - test',
  'beforeEach two: suite two - test',

  'suite two - test',
  'afterEach(): global',
  'afterEach one: suite two - test',
  'afterEach two: suite two - test',

  'after(): global',
  'after one: <root>',
  'after two: <root>',
];

test('Using --require to define global hooks works', async (t) => {
  const spawned = await common.spawnPromisified(process.execPath, [
    ...testArguments,
    '--require', fixtures.path('test-runner', 'no-isolation', 'global-hooks.js'),
    ...testFiles,
  ]);

  t.assert.ok(spawned.stdout.includes(order.join('\n')));
});

test('Using --import to define global hooks works', async (t) => {
  const spawned = await common.spawnPromisified(process.execPath, [
    ...testArguments,
    '--import', fixtures.fileURL('test-runner', 'no-isolation', 'global-hooks.js'),
    ...testFiles,
  ]);

  t.assert.ok(spawned.stdout.includes(order.join('\n')));
});
