const { test } = require('node:test');

test('should pass', () => {});
test('should fail', () => { throw new Error('fail'); });
test('should skip', { skip: true }, () => {});
test('parent', (t) => {
  t.test('should fail', () => { throw new Error('fail'); });
  t.test('should pass but parent fail', (t, done) => {
    setImmediate(done);
  });
});
