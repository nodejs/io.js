import { strictEqual } from 'node:assert';

export function covered() {
  strictEqual(1, 2);
}

export function uncovered() {
  return 'uncovered';
}