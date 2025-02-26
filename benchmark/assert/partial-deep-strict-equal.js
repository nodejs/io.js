'use strict';

const common = require('../common.js');
const assert = require('assert');

const bench = common.createBenchmark(main, {
  n: [50],
  size: [1e3],
  datasetName: ['objects', 'sets', 'maps', 'arrayBuffers', 'dataViewArrayBuffers'],
});

function createObjects(length, depth = 0) {
  return Array.from({ length }, () => ({
    foo: 'yarp',
    nope: {
      bar: '123',
      a: [1, 2, 3],
      c: {},
      b: !depth ? createObjects(2, depth + 1) : [],
    },
  }));
}

function createSets(length, depth = 0) {
  return Array.from({ length }, () => new Set([
    'yarp',
    {
      bar: '123',
      a: [1, 2, 3],
      c: {},
      b: !depth ? createSets(2, depth + 1) : new Set(),
    },
  ]));
}

function createMaps(length, depth = 0) {
  return Array.from({ length }, () => new Map([
    ['foo', 'yarp'],
    ['nope', new Map([
      ['bar', '123'],
      ['a', [1, 2, 3]],
      ['c', {}],
      ['b', !depth ? createMaps(2, depth + 1) : new Map()],
    ])],
  ]));
}

function createArrayBuffers(length) {
  return Array.from({ length }, (_, n) => {
    return new ArrayBuffer(n);
  });
}

function createDataViewArrayBuffers(length) {
  return Array.from({ length }, (_, n) => {
    return new DataView(new ArrayBuffer(n));
  });
}

const datasetMappings = {
  objects: createObjects,
  sets: createSets,
  maps: createMaps,
  arrayBuffers: createArrayBuffers,
  dataViewArrayBuffers: createDataViewArrayBuffers,
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
