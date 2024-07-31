'use strict';

const common = require('../../common.js');

const binding = require(`./build/${common.buildType}/binding`);

const bench = common.createBenchmark(main, {
  n: [5e6],
  implem: ['RunPropertyKey', 'RunNormalString'],
});

function main({ n, implem }) {
  const objs = Array(n).fill(null).map((item) => new Object());
  binding[implem](bench, objs);
}
