const { test } = require('node:test');

test('process.argv is setup', (t) => {
    t.assert.deepStrictEqual(process.argv.slice(2), ['--a-custom-argument']);
});
