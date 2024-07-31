'use strict';

const common = require('../common.js');

const configs = {
  n: [1e6],
  mode: [
    'multi-concat',
    'multi-join',
    'multi-template',
    'to-string-string',
    'to-string-concat',
    'to-string-template',
  ],
};

const bench = common.createBenchmark(main, configs);

function main({ n, mode }) {
  const str = 'abc';
  const num = 123;

  let string;

  switch (mode) {
    case 'multi-concat':
      bench.start();
      for (let i = 0; i < n; i++)
        string = '...' + str + ', ' + num + ', ' + str + ', ' + num + '.';
      bench.end(n);
      break;
    case 'multi-join':
      bench.start();
      for (let i = 0; i < n; i++)
        string = ['...', str, ', ', num, ', ', str, ', ', num, '.'].join('');
      bench.end(n);
      break;
    case 'multi-template':
      bench.start();
      for (let i = 0; i < n; i++)
        string = `...${str}, ${num}, ${str}, ${num}.`;
      bench.end(n);
      break;
    case 'to-string-string':
      bench.start();
      for (let i = 0; i < n; i++)
        string = String(num);
      bench.end(n);
      break;
    case 'to-string-concat':
      bench.start();
      for (let i = 0; i < n; i++)
        string = '' + num;
      bench.end(n);
      break;
    case 'to-string-template':
      bench.start();
      for (let i = 0; i < n; i++)
        string = `${num}`;
      bench.end(n);
      break;
    default:
      throw new Error(`Unexpected method "${mode}"`);
  }

  return string;
}
