// Flags: --no-warnings --expose-internals
'use strict';

const common = require('../common');
const assert = require('assert');
const { Blob } = require('buffer');
const { inspect } = require('util');
const { EOL } = require('os');
const { kState } = require('internal/webstreams/util');

{
  const b = new Blob();
  assert.strictEqual(b.size, 0);
  assert.strictEqual(b.type, '');
}

assert.throws(() => new Blob(false), {
  code: 'ERR_INVALID_ARG_TYPE'
});

assert.throws(() => new Blob('hello'), {
  code: 'ERR_INVALID_ARG_TYPE'
});

assert.throws(() => new Blob({}), {
  code: 'ERR_INVALID_ARG_TYPE'
});

{
  const b = new Blob([]);
  assert(b);
  assert.strictEqual(b.size, 0);
  assert.strictEqual(b.type, '');

  b.arrayBuffer().then(common.mustCall((ab) => {
    assert.deepStrictEqual(ab, new ArrayBuffer(0));
  }));
  b.text().then(common.mustCall((text) => {
    assert.strictEqual(text, '');
  }));
  const c = b.slice();
  assert.strictEqual(c.size, 0);
}

{
  assert.strictEqual(new Blob([], { type: 1 }).type, '1');
  assert.strictEqual(new Blob([], { type: false }).type, 'false');
  assert.strictEqual(new Blob([], { type: {} }).type, '[object object]');
}

{
  const b = new Blob([Buffer.from('abc')]);
  assert.strictEqual(b.size, 3);
  b.text().then(common.mustCall((text) => {
    assert.strictEqual(text, 'abc');
  }));
}

{
  const b = new Blob([new ArrayBuffer(3)]);
  assert.strictEqual(b.size, 3);
  b.text().then(common.mustCall((text) => {
    assert.strictEqual(text, '\0\0\0');
  }));
}

{
  const b = new Blob([new Uint8Array(3)]);
  assert.strictEqual(b.size, 3);
  b.text().then(common.mustCall((text) => {
    assert.strictEqual(text, '\0\0\0');
  }));
}

{
  const b = new Blob([new Blob(['abc'])]);
  assert.strictEqual(b.size, 3);
  b.text().then(common.mustCall((text) => {
    assert.strictEqual(text, 'abc');
  }));
}

{
  const b = new Blob(['hello', Buffer.from('world')]);
  assert.strictEqual(b.size, 10);
  b.text().then(common.mustCall((text) => {
    assert.strictEqual(text, 'helloworld');
  }));
}

{
  const b = new Blob(['hello', new Uint8Array([0xed, 0xa0, 0x88])]);
  assert.strictEqual(b.size, 8);
  b.text().then(common.mustCall((text) => {
    assert.strictEqual(text, 'hello\ufffd\ufffd\ufffd');
    assert.strictEqual(text.length, 8);
  }));
}

{
  const b = new Blob(
    [
      'h',
      'e',
      'l',
      'lo',
      Buffer.from('world'),
    ]);
  assert.strictEqual(b.size, 10);
  b.text().then(common.mustCall((text) => {
    assert.strictEqual(text, 'helloworld');
  }));
}

{
  const b = new Blob(['hello', Buffer.from('world')]);
  assert.strictEqual(b.size, 10);
  assert.strictEqual(b.type, '');

  const c = b.slice(1, -1, 'foo');
  assert.strictEqual(c.type, 'foo');
  c.text().then(common.mustCall((text) => {
    assert.strictEqual(text, 'elloworl');
  }));

  const d = c.slice(1, -1);
  d.text().then(common.mustCall((text) => {
    assert.strictEqual(text, 'llowor');
  }));

  const e = d.slice(1, -1);
  e.text().then(common.mustCall((text) => {
    assert.strictEqual(text, 'lowo');
  }));

  const f = e.slice(1, -1);
  f.text().then(common.mustCall((text) => {
    assert.strictEqual(text, 'ow');
  }));

  const g = f.slice(1, -1);
  assert.strictEqual(g.type, '');
  g.text().then(common.mustCall((text) => {
    assert.strictEqual(text, '');
  }));

  const h = b.slice(-1, 1);
  assert.strictEqual(h.size, 0);

  const i = b.slice(1, 100);
  assert.strictEqual(i.size, 9);

  const j = b.slice(1, 2, false);
  assert.strictEqual(j.type, 'false');

  assert.strictEqual(b.size, 10);
  assert.strictEqual(b.type, '');
}

{
  const b = new Blob([Buffer.from('hello'), Buffer.from('world')]);
  const mc = new MessageChannel();
  mc.port1.onmessage = common.mustCall(({ data }) => {
    data.text().then(common.mustCall((text) => {
      assert.strictEqual(text, 'helloworld');
    }));
    mc.port1.close();
  });
  mc.port2.postMessage(b);
  b.text().then(common.mustCall((text) => {
    assert.strictEqual(text, 'helloworld');
  }));
}

{
  const b = new Blob(['hello'], { type: '\x01' });
  assert.strictEqual(b.type, '');
}

{
  const descriptor =
      Object.getOwnPropertyDescriptor(Blob.prototype, Symbol.toStringTag);
  assert.deepStrictEqual(descriptor, {
    configurable: true,
    enumerable: false,
    value: 'Blob',
    writable: false
  });
}

{
  const descriptors = Object.getOwnPropertyDescriptors(Blob.prototype);
  const enumerable = [
    'size',
    'type',
    'slice',
    'stream',
    'text',
    'arrayBuffer',
    'bytes',
  ];

  for (const prop of enumerable) {
    assert.notStrictEqual(descriptors[prop], undefined);
    assert.strictEqual(descriptors[prop].enumerable, true);
  }
}

{
  const b = new Blob(['test', 42]);
  b.text().then(common.mustCall((text) => {
    assert.strictEqual(text, 'test42');
  }));
}

{
  const b = new Blob();
  assert.strictEqual(inspect(b, { depth: null }),
                     'Blob { size: 0, type: \'\' }');
  assert.strictEqual(inspect(b, { depth: 1 }),
                     'Blob { size: 0, type: \'\' }');
  assert.strictEqual(inspect(b, { depth: -1 }), '[Blob]');
}

{
  // The Blob has to be over a specific size for the data to
  // be copied asynchronously..
  const b = new Blob(['hello', 'there'.repeat(820)]);
  b.arrayBuffer().then(common.mustCall());
}

(async () => {
  const b = new Blob(['hello']);
  const reader = b.stream().getReader();
  let res = await reader.read();
  assert.strictEqual(res.value.byteLength, 5);
  assert(!res.done);
  res = await reader.read();
  assert(res.done);
})().then(common.mustCall());

(async () => {
  const b = new Blob(Array(10).fill('hello'));
  const reader = b.stream().getReader();
  const chunks = [];
  while (true) {
    const res = await reader.read();
    if (res.done) break;
    assert.strictEqual(res.value.byteLength, 5);
    chunks.push(res.value);
  }
  assert.strictEqual(chunks.length, 10);
})().then(common.mustCall());

(async () => {
  const b = new Blob(Array(10).fill('hello'));
  const reader = b.stream().getReader();
  const chunks = [];
  while (true) {
    const res = await reader.read();
    if (chunks.length === 5) {
      reader.cancel('boom');
      break;
    }
    if (res.done) break;
    assert.strictEqual(res.value.byteLength, 5);
    chunks.push(res.value);
  }
  assert.strictEqual(chunks.length, 5);
  reader.closed.then(common.mustCall());
})().then(common.mustCall());

(async () => {
  const b = new Blob(['A', 'B', 'C']);
  const stream = b.stream();
  const chunks = [];
  const decoder = new TextDecoder();
  await stream.pipeTo(new WritableStream({
    write(chunk) {
      chunks.push(decoder.decode(chunk, { stream: true }));
    }
  }));
  assert.strictEqual(chunks.join(''), 'ABC');
})().then(common.mustCall());

(async () => {
  const b = new Blob(['A', 'B', 'C']);
  const stream = b.stream();
  const chunks = [];
  const decoder = new TextDecoder();
  await stream.pipeTo(
    new WritableStream({
      write(chunk) {
        chunks.push(decoder.decode(chunk, { stream: true }));
      },
    })
  );
  assert.strictEqual(chunks.join(''), 'ABC');
})().then(common.mustCall());

(async () => {
  // Ref: https://github.com/nodejs/node/issues/48668
  const chunks = [];
  const stream = new Blob(['Hello world']).stream();
  const decoder = new TextDecoder();
  await Promise.resolve();
  await stream.pipeTo(
    new WritableStream({
      write(chunk) {
        chunks.push(decoder.decode(chunk, { stream: true }));
      },
    })
  );
  assert.strictEqual(chunks.join(''), 'Hello world');
})().then(common.mustCall());

(async () => {
  // Ref: https://github.com/nodejs/node/issues/48668
  if (common.hasCrypto) {
    // Can only do this test if we have node built with crypto
    const file = new Blob(['<svg></svg>'], { type: 'image/svg+xml' });
    const url = URL.createObjectURL(file);
    const res = await fetch(url);
    const blob = await res.blob();
    assert.strictEqual(blob.size, 11);
    assert.strictEqual(blob.type, 'image/svg+xml');
    assert.strictEqual(await blob.text(), '<svg></svg>');
  }
})().then(common.mustCall());

(async () => {
  const b = new Blob(Array(10).fill('hello'));
  const stream = b.stream();
  const reader = stream.getReader();
  assert.strictEqual(stream[kState].controller.desiredSize, 0);
  const { value, done } = await reader.read();
  assert.strictEqual(value.byteLength, 5);
  assert(!done);
  setTimeout(() => {
    // The blob stream is now a byte stream hence after the first read,
    // it should pull in the next 'hello' which is 5 bytes hence -5.
    assert.strictEqual(stream[kState].controller.desiredSize, 0);
  }, 0);
})().then(common.mustCall());

(async () => {
  const blob = new Blob(['hello', 'world']);
  const stream = blob.stream();
  const reader = stream.getReader({ mode: 'byob' });
  const decoder = new TextDecoder();
  const chunks = [];
  while (true) {
    const { value, done } = await reader.read(new Uint8Array(100));
    if (done) break;
    chunks.push(decoder.decode(value, { stream: true }));
  }
  assert.strictEqual(chunks.join(''), 'helloworld');
})().then(common.mustCall());

(async () => {
  const b = new Blob(Array(10).fill('hello'));
  const stream = b.stream();
  const reader = stream.getReader({ mode: 'byob' });
  assert.strictEqual(stream[kState].controller.desiredSize, 0);
  const { value, done } = await reader.read(new Uint8Array(100));
  assert.strictEqual(value.byteLength, 5);
  assert(!done);
  setTimeout(() => {
    assert.strictEqual(stream[kState].controller.desiredSize, 0);
  }, 0);
})().then(common.mustCall());

(async () => {
  const b = new Blob(Array(10).fill('hello'));
  const stream = b.stream();
  const reader = stream.getReader({ mode: 'byob' });
  assert.strictEqual(stream[kState].controller.desiredSize, 0);
  const { value, done } = await reader.read(new Uint8Array(2));
  assert.strictEqual(value.byteLength, 2);
  assert(!done);
  setTimeout(() => {
    assert.strictEqual(stream[kState].controller.desiredSize, -3);
  }, 0);
})().then(common.mustCall());

{
  const b = new Blob(['hello\n'], { endings: 'native' });
  assert.strictEqual(b.size, EOL.length + 5);

  [1, {}, 'foo'].forEach((endings) => {
    assert.throws(() => new Blob([], { endings }), {
      code: 'ERR_INVALID_ARG_VALUE',
    });
  });
}

{
  assert.throws(() => Reflect.get(Blob.prototype, 'type', {}), {
    code: 'ERR_INVALID_THIS',
  });
  assert.throws(() => Reflect.get(Blob.prototype, 'size', {}), {
    code: 'ERR_INVALID_THIS',
  });
  assert.throws(() => Blob.prototype.slice(Blob.prototype, 0, 1), {
    code: 'ERR_INVALID_THIS',
  });
  assert.throws(() => Blob.prototype.stream.call(), {
    code: 'ERR_INVALID_THIS',
  });
}

(async () => {
  await assert.rejects(() => Blob.prototype.arrayBuffer.call(), {
    code: 'ERR_INVALID_THIS',
  });
  await assert.rejects(() => Blob.prototype.text.call(), {
    code: 'ERR_INVALID_THIS',
  });
  await assert.rejects(() => Blob.prototype.bytes.call(), {
    code: 'ERR_INVALID_THIS',
  });
})().then(common.mustCall());

(async () => {
  const blob = new Blob([
    new Uint8Array([0x50, 0x41, 0x53, 0x53]),
    new Int8Array([0x50, 0x41, 0x53, 0x53]),
    new Uint16Array([0x4150, 0x5353]),
    new Int16Array([0x4150, 0x5353]),
    new Uint32Array([0x53534150]),
    new Int32Array([0x53534150]),
    new Float32Array([0xD341500000]),
  ]);

  assert.strictEqual(blob.size, 28);
  assert.strictEqual(blob.type, '');
})().then(common.mustCall());

{
  // Testing the defaults
  [undefined, null, { __proto__: null }, { type: undefined }, {
    get type() {}, // eslint-disable-line getter-return
  }].forEach((options) => {
    assert.strictEqual(
      new Blob([], options).type,
      new Blob([]).type,
    );
  });

  Reflect.defineProperty(Object.prototype, 'type', {
    __proto__: null,
    configurable: true,
    get: common.mustCall(() => 3, 7),
  });

  [{}, [], () => {}, Number, new Number(), new String(), new Boolean()].forEach(
    (options) => {
      assert.strictEqual(new Blob([], options).type, '3');
    },
  );
  [0, '', true, Symbol(), 0n].forEach((options) => {
    assert.throws(() => new Blob([], options), { code: 'ERR_INVALID_ARG_TYPE' });
  });

  delete Object.prototype.type;
}

(async () => {
  // Refs: https://github.com/nodejs/node/issues/47301

  const random = Buffer.alloc(256).fill('0');
  const chunks = [];

  for (let i = 0; i < random.length; i += 2) {
    chunks.push(random.subarray(i, i + 2));
  }

  await new Blob(chunks).arrayBuffer();
})().then(common.mustCall());

{
  const blob = new Blob(['hello']);

  assert.ok(blob.slice(0, 1).constructor === Blob);
  assert.ok(blob.slice(0, 1) instanceof Blob);
  assert.ok(blob.slice(0, 1.5) instanceof Blob);
}

(async () => {
  const blob = new Blob(['hello']);

  assert.ok(structuredClone(blob).constructor === Blob);
  assert.ok(structuredClone(blob) instanceof Blob);
  assert.ok(structuredClone(blob).size === blob.size);
  assert.ok(structuredClone(blob).size === blob.size);
  assert.ok((await structuredClone(blob).text()) === (await blob.text()));
})().then(common.mustCall());

(async () => {
  const blob = new Blob(['hello']);
  const { arrayBuffer } = Blob.prototype;

  Blob.prototype.arrayBuffer = common.mustNotCall();

  try {
    assert.strictEqual(await blob.text(), 'hello');
  } finally {
    Blob.prototype.arrayBuffer = arrayBuffer;
  }
})().then(common.mustCall());
