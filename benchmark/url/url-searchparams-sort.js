'use strict';
const common = require('../common.js');

const inputs = {
  wpt: 'wpt',  // To work around tests
  empty: '',
  sorted: 'a&b&c&d&e&f&g&h&i&j&k&l&m&n&o&p&q&r&s&t&u&v&w&x&y&z',
  almostsorted: 'a&b&c&d&e&f&g&i&h&j&k&l&m&n&o&p&q&r&s&t&u&w&v&x&y&z',
  reversed: 'z&y&x&w&v&u&t&s&r&q&p&o&n&m&l&k&j&i&h&g&f&e&d&c&b&a',
  random: 'm&t&d&c&z&v&a&n&p&y&u&o&h&l&f&j&e&q&b&i&s&x&k&w&r&g',
  // 8 parameters
  short: 'm&t&d&c&z&v&a&n',
  // 88 parameters
  long: 'g&r&t&h&s&r&d&w&b&n&h&k&x&m&k&h&o&e&x&c&c&g&e&b&p&p&s&n&j&b&y&z&' +
        'u&l&o&r&w&a&u&l&m&f&j&q&p&f&e&y&e&n&e&l&m&w&u&w&t&n&t&q&v&y&c&o&' +
        'k&f&j&i&l&m&g&j&d&i&z&q&p&x&q&q&d&n&y&w&g&i&v&r',
};

function getParams(str) {
  return str.split('&');
}

const bench = common.createBenchmark(main, {
  type: Object.keys(inputs),
  n: [1e6],
});

function main({ type, n }) {
  const input = inputs[type];
  const params = new URLSearchParams();
  const array = getParams(input);

  for (let i = 0; i < array.length; i++) {
    params.append(array[i], '');
  }

  bench.start();
  for (let i = 0; i < n; i++) {
    const paramsForSort = new URLSearchParams(params);
    paramsForSort.sort();
  }
  bench.end(n);
}
