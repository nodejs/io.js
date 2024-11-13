// Flags: --test-only
import { describe, test, after } from 'node:test';

after(() => { console.log('with global after()'); });
await Promise.resolve();

console.log('Execution order was:');
const ll = (t) => { console.log(` * ${t.fullName}`) };

describe('A', () => {
  test.only('A', ll);
  test('B', ll);
  describe.only('C', () => {
    test.only('A', ll);
    test('B', ll);
  });
  describe('D', () => {
    test.only('A', ll);
    test('B', ll);
  });
});
describe.only('B', () => {
  test('A', ll);
  test('B', ll);
  describe('C', () => {
    test('A', ll);
  });
});
describe('C', () => {
  test.only('A', ll);
  test('B', ll);
  describe.only('C', () => {
    test('A', ll);
    test('B', ll);
  });
  describe('D', () => {
    test('A', ll);
    test.only('B', ll);
  });
});
describe('D', () => {
  test('A', ll);
  test.only('B', ll);
});
describe.only('E', () => {
  test('A', ll);
  test('B', ll);
});
test.only('F', ll);
