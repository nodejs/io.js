const test = require('node:test');
const assert = require('node:assert');
const { foo } = require('../logic-file.js');

test('foo returns 1', () => {
    assert.strictEqual(foo(), 1);
});
