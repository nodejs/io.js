'use strict';
require('../common');
const vm = require('vm');
// Make this test OS-independent by overriding stdio getColorDepth().
process.stdout.getColorDepth = () => 8;
process.stderr.getColorDepth = () => 8;
Error.stackTraceLimit = Infinity;

console.log({ foo: 'bar' });
console.log('%s q', 'string');
console.log('%o with object format param', { foo: 'bar' });

console.log(
  new Error('test\n    at abc (../fixtures/node_modules/bar.js:4:4)\nfoobar'),
);

try {
  require('../fixtures/node_modules/node_modules/bar.js');
} catch (err) {
  console.log(err);
}

vm.runInThisContext('console.log(new Error())');
