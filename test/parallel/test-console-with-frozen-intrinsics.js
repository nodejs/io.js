// flags: --frozen-intrinsics
'use strict';
require('../common');
console.clear();

const consoleMethods = ['log', 'info', 'warn', 'error', 'debug', 'trace'];

for (const method of consoleMethods) {
  console[method]('foo');
  console[method]('foo', 'bar');
  console[method]('%s %s', 'foo', 'bar', 'hop');
}

console.dir({ slashes: '\\\\' });
console.dirxml({ slashes: '\\\\' });

console.time('label');
console.timeLog('label', 'hi');
console.timeEnd('label');

console.assert(true, 'true');

console.count('label');
console.countReset('label');

console.group('label');
console.groupCollapsed('label');
console.groupEnd();

console.table([{ a: 1, b: 2 }, { a: 'foo', b: 'bar' }]);
