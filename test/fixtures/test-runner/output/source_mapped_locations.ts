// Flags: --enable-source-maps
import { test } from 'node:test';
import { strictEqual } from 'node:assert';

test('fails', () => {
  strictEqual(1, 2);
});
