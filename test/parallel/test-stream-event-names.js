'use strict';

require('../common');
const assert = require('assert');
const { Readable, Writable, Duplex } = require('stream');

{
  const stream = new Readable();
  assert.strictEqual(stream.eventNames().length, 0);
}

{
  const stream = new Readable();
  stream.on('foo', () => {});
  stream.on('data', () => {});
  stream.on('error', () => {});
  assert.deepStrictEqual(stream.eventNames(), ['error', 'data', 'foo']);
}

{
  const stream = new Writable();
  assert.strictEqual(stream.eventNames().length, 0);
}

{
  const stream = new Writable();
  stream.on('foo', () => {});
  stream.on('drain', () => {});
  stream.on('prefinish', () => {});
  assert.deepStrictEqual(stream.eventNames(), ['prefinish', 'drain', 'foo']);
}
{
  const stream = new Duplex();
  assert.strictEqual(stream.eventNames().length, 0);
}

{
  const stream = new Duplex();
  stream.on('foo', () => {});
  stream.on('finish', () => {});
  assert.deepStrictEqual(stream.eventNames(), ['finish', 'foo']);
}
