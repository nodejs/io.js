import { test } from 'node:test';
import { covered } from './b.test';

test('fails', () => {
  covered();
});

function uncovered() {
  return 'uncovered';
}
if (false) {
  uncovered();
}