'use strict';
const common = require('../common');

const bench = common.createBenchmark(main, {
  type: ['one_byte', 'two_bytes', 'three_bytes',
         'four_bytes', 'latin1'],
  encoding: ['utf8', 'base64'],
  repeat: [1, 2, 16, 256], // x16
  n: [4e6],
});

// 16 chars each
const chars = {
  one_byte: 'hello brendan!!!',
  two_bytes: 'ΰαβγδεζηθικλμνξο',
  three_bytes: '挰挱挲挳挴挵挶挷挸挹挺挻挼挽挾挿',
  four_bytes: '𠜎𠜱𠝹𠱓𠱸𠲖𠳏𠳕𠴕𠵼𠵿𠸎𠸏𠹷𠺝𠺢',
  latin1: 'Un homme sage est supérieur à toutes ' +
    'les insultes qui peuvent lui être adressées, et la meilleure réponse est la patience et la modération.',
};

function getInput(type, repeat, encoding) {
  const original = (repeat === 1) ? chars[type] : chars[type].repeat(repeat);
  if (encoding === 'base64') {
    Buffer.from(original, 'utf8').toString('base64');
  }
  return original;
}

function main({ n, repeat, encoding, type }) {
  const data = getInput(type, repeat, encoding);
  const expected = Buffer.byteLength(data, encoding);
  let changed = false;
  bench.start();
  for (let i = 0; i < n; i++) {
    const actual = Buffer.byteLength(data, encoding);
    if (expected !== actual) { changed = true; }
  }
  bench.end(n);
  if (changed) {
    throw new Error('Result changed during iteration');
  }
}
