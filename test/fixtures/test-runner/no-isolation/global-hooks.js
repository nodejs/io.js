import('node:test').then((test) => {
  test.before(() => console.log('before(): global'));
  test.beforeEach(() => console.log('beforeEach(): global'));
  test.after(() => console.log('after(): global'));
  test.afterEach(() => console.log('afterEach(): global'));
});
