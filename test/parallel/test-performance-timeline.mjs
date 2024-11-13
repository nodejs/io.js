// This file may needs to be updated to wpt:
// https://github.com/web-platform-tests/wpt

import '../common/index.mjs';
import assert from 'assert';

import { performance } from 'perf_hooks';
import { setTimeout } from 'timers/promises';

// Order by startTime
performance.mark('one');
await setTimeout(50);
performance.mark('two');
await setTimeout(50);
performance.mark('three');
await setTimeout(50);
performance.measure('three', 'three');
await setTimeout(50);
performance.measure('two', 'two');
await setTimeout(50);
performance.measure('one', 'one');
const entries = performance.getEntriesByType('measure');
assert.deepStrictEqual(entries.map((x) => x.name), ['one', 'two', 'three']);
const allEntries = performance.getEntries();
assert.deepStrictEqual(allEntries.map((x) => x.name), ['one', 'one', 'two', 'two', 'three', 'three']);

performance.mark('a');
await setTimeout(50);
performance.measure('a', 'a');
await setTimeout(50);
performance.mark('a');
await setTimeout(50);
performance.measure('a', 'one');
const entriesByName = performance.getEntriesByName('a');
assert.deepStrictEqual(entriesByName.map((x) => x.entryType), ['measure', 'mark', 'measure', 'mark']);
const marksByName = performance.getEntriesByName('a', 'mark');
assert.deepStrictEqual(marksByName.map((x) => x.entryType), ['mark', 'mark']);
const measuresByName = performance.getEntriesByName('a', 'measure');
assert.deepStrictEqual(measuresByName.map((x) => x.entryType), ['measure', 'measure']);
const invalidTypeEntriesByName = performance.getEntriesByName('a', null);
assert.strictEqual(invalidTypeEntriesByName.length, 0);

// getEntriesBy[Name|Type](undefined)
performance.mark(undefined);
assert.strictEqual(performance.getEntriesByName(undefined).length, 1);
assert.strictEqual(performance.getEntriesByType(undefined).length, 0);
assert.throws(() => performance.getEntriesByName(), {
  name: 'TypeError',
  message: 'The "name" argument must be specified',
  code: 'ERR_MISSING_ARGS'
});
assert.throws(() => performance.getEntriesByType(), {
  name: 'TypeError',
  message: 'The "type" argument must be specified',
  code: 'ERR_MISSING_ARGS'
});
