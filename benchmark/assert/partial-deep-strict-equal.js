'use strict';

const common = require('../common.js');
const assert = require('assert');

const bench = common.createBenchmark(main, {
  n: [25, 2e2],
  size: [1e2, 1e4],
  datasetName: ['objects', 'sets', 'maps', 'arrayBuffers'],
}, {
  combinationFilter: (p) => {
    return p.size === 1e4 && p.n === 25 ||
           p.size === 1e3 && p.n === 2e2 ||
           p.size === 1e2 && p.n === 2e3 ||
           p.size === 1;
  },
});

function createObjects(size, depth = 0) {
  return Array.from({ length: size }, (n) => ({
    foo: 'yarp',
    nope: {
      bar: '123',
      a: [1, 2, 3],
      baz: n,
      c: {},
      b: !depth ? createObjects(2, depth + 1) : [],
    },
  }));
}

function createSets(size, depth = 0) {
  return Array.from({ length: size }, (n) => new Set([
    'yarp',
    {
      bar: '123',
      a: [1, 2, 3],
      baz: n,
      c: {},
      b: !depth ? createSets(2, depth + 1) : new Set(),
    },
  ]));
}

function createMaps(size, depth = 0) {
  return Array.from({ length: size }, (n) => new Map([
    ['foo', 'yarp'],
    ['nope', new Map([
      ['bar', '123'],
      ['a', [1, 2, 3]],
      ['baz', n],
      ['c', {}],
      ['b', !depth ? createMaps(2, depth + 1) : new Map()],
    ])],
  ]));
}

function createArrayBuffers(size) {
  return Array.from({ length: size }, (n) => {
    if (n % 2) {
      return new DataView(new ArrayBuffer(n));
    }
    return new ArrayBuffer(n);
  });
}

const datasetMappings = {
  objects: createObjects,
  sets: createSets,
  maps: createMaps,
  arrayBuffers: createArrayBuffers,
};

function getDatasets(datasetName, size) {
  return {
    actual: datasetMappings[datasetName](size),
    expected: datasetMappings[datasetName](size),
  };
}

function main({ size, n, datasetName }) {
  const { actual, expected } = getDatasets(datasetName, size);

  bench.start();
  for (let i = 0; i < n; ++i) {
    assert.partialDeepStrictEqual(actual, expected);
  }
  bench.end(n);
}
