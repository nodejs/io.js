'use strict';
const util = require('util');

const common = require('../common.js');

const opts = {
  showHidden: { showHidden: true },
  colors: { colors: true },
  none: undefined,
};
const bench = common.createBenchmark(main, {
  n: [8e4],
  method: [
    'Object',
    'Object_empty',
    'Object_deep_ln',
    'String',
    'String_complex',
    'String_boxed',
    'Date',
    'Set',
    'Error',
    'Array',
    'TypedArray',
    'TypedArray_extra',
    'Number',
  ],
  option: Object.keys(opts),
});

function benchmark(n, obj, options) {
  bench.start();
  for (let i = 0; i < n; i += 1) {
    util.inspect(obj, options);
  }
  bench.end(n);
}

function main({ method, n, option }) {
  let obj;
  const options = opts[option];
  switch (method) {
    case 'Object':
      benchmark(n, { a: 'a', b: 'b', c: 'c', d: 'd' }, options);
      break;
    case 'Object_empty':
      benchmark(n, {}, options);
      break;
    case 'Object_deep_ln':
      if (options)
        options.depth = Infinity;
      obj = { first:
              { second:
                { third:
                  { a: 'first',
                    b: 'second',
                    c: 'third',
                    d: 'fourth',
                    e: 'fifth',
                    f: 'sixth',
                    g: 'seventh' } } } };
      benchmark(n, obj, options || { depth: Infinity });
      break;
    case 'String':
      benchmark(n, 'Simple string', options);
      break;
    case 'String_complex':
      benchmark(n, 'This string\nhas to be\tescaped!', options);
      break;
    case 'String_boxed':
      benchmark(n, new String('string'), options);
      break;
    case 'Date':
      benchmark(n, new Date(), options);
      break;
    case 'Set':
      obj = new Set([5, 3]);
      benchmark(n, obj, options);
      break;
    case 'Error':
      benchmark(n, new Error('error'), options);
      break;
    case 'Array':
      benchmark(n, Array(50).fill().map((_, i) => i), options);
      break;
    case 'TypedArray':
      obj = new Uint8Array(Array(50).fill().map((_, i) => i));
      benchmark(n, obj, options);
      break;
    case 'TypedArray_extra':
      obj = new Uint8Array(Array(50).fill().map((_, i) => i));
      obj.foo = 'bar';
      obj[Symbol('baz')] = 5;
      benchmark(n, obj, options);
      break;
    case 'Number':
      benchmark(n, 0, options);
      break;
    default:
      throw new Error(`Unsupported method "${method}"`);
  }
}
