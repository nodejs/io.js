import { allowGlobals, mustCall, mustNotCall } from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import { deepStrictEqual } from 'node:assert';
import { run } from 'node:test';

const stream = run({
  files: [
    fixtures.path('test-runner', 'no-isolation', 'one.test.js'),
    fixtures.path('test-runner', 'no-isolation', 'two.test.js'),
  ],
  isolation: 'none',
});

stream.on('test:fail', mustNotCall());
stream.on('test:pass', mustCall(4));
// eslint-disable-next-line no-unused-vars
for await (const _ of stream);
allowGlobals(globalThis.GLOBAL_ORDER);
deepStrictEqual(globalThis.GLOBAL_ORDER, [
  'before one: <root>',
  'suite one',
  'before two: <root>',
  'suite two',

  'beforeEach one: suite one - test',
  'beforeEach two: suite one - test',
  'suite one - test',
  'afterEach one: suite one - test',
  'afterEach two: suite one - test',

  'before suite two: suite two',

  'beforeEach one: suite two - test',
  'beforeEach two: suite two - test',
  'suite two - test',
  'afterEach one: suite two - test',
  'afterEach two: suite two - test',

  'after one: <root>',
  'after two: <root>',
]);
