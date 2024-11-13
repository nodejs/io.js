'use strict';

const common = require('../../common.js');

const bench = common.createBenchmark(main, {
  n: [5e6],
  addon: ['binding', 'binding_node_api_v8'],
  implem: ['createExternalBuffer'],
});

function main({ n, implem, addon }) {
  const binding = require(`./build/${common.buildType}/${addon}`);
  binding[implem](bench, n);
}
