// Flags: --expose-internals --no-warnings

import '../common/index.mjs';
import { describe, it, beforeEach } from 'node:test';
import assert from 'node:assert';
import { finished } from 'node:stream/promises';
import { DefaultSerializer } from 'node:v8';
import serializer from 'internal/test_runner/reporter/v8-serializer';
import runner from 'internal/test_runner/runner';

async function toArray(chunks) {
  const arr = [];
  for await (const i of chunks) arr.push(i);
  return arr;
}

const chunks = await toArray(serializer([
  { type: 'test:diagnostic', data: { nesting: 0, details: {}, message: 'diagnostic' } },
]));
const defaultSerializer = new DefaultSerializer();
defaultSerializer.writeHeader();
const headerLength = defaultSerializer.releaseBuffer().length;

describe('v8 deserializer', () => {
  let fileTest;
  let reported;
  beforeEach(() => {
    reported = [];
    fileTest = new runner.FileTest({
      name: 'filetest',
      harness: { config: {} },
    });
    fileTest.reporter.on('data', (data) => reported.push(data));
    assert(fileTest.isClearToSend());
  });

  async function collectReported(chunks) {
    chunks.forEach((chunk) => fileTest.parseMessage(chunk));
    fileTest.drain();
    fileTest.reporter.end();
    await finished(fileTest.reporter);
    return reported;
  }

  it('should do nothing when no chunks', async () => {
    const reported = await collectReported([]);
    assert.deepStrictEqual(reported, []);
  });

  it('should deserialize a chunk with no serialization', async () => {
    const reported = await collectReported([Buffer.from('unknown')]);
    assert.deepStrictEqual(reported, [
      { data: { __proto__: null, file: 'filetest', message: 'unknown' }, type: 'test:stdout' },
    ]);
  });

  it('should deserialize a serialized chunk', async () => {
    const reported = await collectReported(chunks);
    assert.deepStrictEqual(reported, [
      { data: { nesting: 0, details: {}, message: 'diagnostic' }, type: 'test:diagnostic' },
    ]);
  });

  it('should deserialize a serialized chunk after non-serialized chunk', async () => {
    const reported = await collectReported([Buffer.concat([Buffer.from('unknown'), ...chunks])]);
    assert.deepStrictEqual(reported, [
      { data: { __proto__: null, file: 'filetest', message: 'unknown' }, type: 'test:stdout' },
      { data: { nesting: 0, details: {}, message: 'diagnostic' }, type: 'test:diagnostic' },
    ]);
  });

  it('should deserialize a serialized chunk before non-serialized output', async () => {
    const reported = await collectReported([Buffer.concat([ ...chunks, Buffer.from('unknown')])]);
    assert.deepStrictEqual(reported, [
      { data: { nesting: 0, details: {}, message: 'diagnostic' }, type: 'test:diagnostic' },
      { data: { __proto__: null, file: 'filetest', message: 'unknown' }, type: 'test:stdout' },
    ]);
  });

  const headerPosition = headerLength * 2 + 4;
  for (let i = 0; i < headerPosition + 5; i++) {
    const message = `should deserialize a serialized message split into two chunks {...${i},${i + 1}...}`;
    it(message, async () => {
      const data = chunks[0];
      const reported = await collectReported([data.subarray(0, i), data.subarray(i)]);
      assert.deepStrictEqual(reported, [
        { data: { nesting: 0, details: {}, message: 'diagnostic' }, type: 'test:diagnostic' },
      ]);
    });

    it(`${message} wrapped by non-serialized data`, async () => {
      const data = chunks[0];
      const reported = await collectReported([
        Buffer.concat([Buffer.from('unknown'), data.subarray(0, i)]),
        Buffer.concat([data.subarray(i), Buffer.from('unknown')]),
      ]);
      assert.deepStrictEqual(reported, [
        { data: { __proto__: null, file: 'filetest', message: 'unknown' }, type: 'test:stdout' },
        { data: { nesting: 0, details: {}, message: 'diagnostic' }, type: 'test:diagnostic' },
        { data: { __proto__: null, file: 'filetest', message: 'unknown' }, type: 'test:stdout' },
      ]);
    }
    );
  }

});
