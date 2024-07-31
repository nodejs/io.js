import { test, after } from 'node:test';

after(() => {});

test('a test with some delay', (t, done) => {
  setTimeout(done, 50);
});
