import test from 'node:test';
import assert from 'node:assert';
import { foo } from './logic-file.js';

test('foo returns 1', () => {
    assert.strictEqual(foo(), 1);
});
