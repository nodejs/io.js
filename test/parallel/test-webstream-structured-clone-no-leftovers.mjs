import '../common/index.mjs';
import { test } from 'node:test';
import assert from 'node:assert';

test('do not leak promises', async () => {
  const buf = new Uint8Array(1);
  const readable = new ReadableStream({
    start(controller) {
      controller.enqueue(buf);
      controller.close();
    }
  });

  const [out1, out2] = readable.tee();
  const cloned = structuredClone(out2, { transfer: [out2] });

  for await (const chunk of cloned) {
    assert.deepStrictEqual(chunk, buf);
  }

  for await (const chunk of out2) {
    assert.deepStrictEqual(chunk, buf);
  }

  for await (const chunk of out1) {
    assert.deepStrictEqual(chunk, buf);
  }
});
