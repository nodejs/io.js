// META: global=window,worker,shadowrealm

'use strict';

// The browser is assumed to use the same implementation as for TextDecoder, so
// this file don't replicate the exhaustive checks it has. It is just a smoke
// test that non-UTF-8 encodings work at all.

const encodings = [
  {
    name: 'UTF-16BE',
    value: [108, 52],
    expected: "\u{6c34}",
    invalid: [0xD8, 0x00]
  },
  {
    name: 'UTF-16LE',
    value: [52, 108],
    expected: "\u{6c34}",
    invalid: [0x00, 0xD8]
  },
  {
    name: 'Shift_JIS',
    value: [144, 133],
    expected: "\u{6c34}",
    invalid: [255]
  },
  {
    name: 'ISO-2022-JP',
    value: [65, 66, 67, 0x1B, 65, 66, 67],
    expected: "ABC\u{fffd}ABC",
    invalid: [0x0E]
  },
  {
    name: 'ISO-8859-14',
    value: [100, 240, 114],
    expected: "d\u{0175}r",
    invalid: undefined  // all bytes are treated as valid
  }
];

for (const encoding of encodings) {
  promise_test(async () => {
    const stream = new TextDecoderStream(encoding.name);
    const reader = stream.readable.getReader();
    const writer = stream.writable.getWriter();
    const writePromise = writer.write(new Uint8Array(encoding.value));
    const {value, done} = await reader.read();
    assert_false(done, 'readable should not be closed');
    assert_equals(value, encoding.expected, 'chunk should match expected');
    await writePromise;
  }, `TextDecoderStream should be able to decode ${encoding.name}`);

  if (!encoding.invalid)
    continue;

  promise_test(async t => {
    const stream = new TextDecoderStream(encoding.name);
    const reader = stream.readable.getReader();
    const writer = stream.writable.getWriter();
    const writePromise = writer.write(new Uint8Array(encoding.invalid));
    const closePromise = writer.close();
    const {value, done} = await reader.read();
    assert_false(done, 'readable should not be closed');
    assert_equals(value, '\u{FFFD}', 'output should be replacement character');
    await Promise.all([writePromise, closePromise]);
  }, `TextDecoderStream should be able to decode invalid sequences in ` +
     `${encoding.name}`);

  promise_test(async t => {
    const stream = new TextDecoderStream(encoding.name, {fatal: true});
    const reader = stream.readable.getReader();
    const writer = stream.writable.getWriter();
    const writePromise = writer.write(new Uint8Array(encoding.invalid));
    const closePromise = writer.close();
    await promise_rejects_js(t, TypeError, reader.read(),
                          'readable should be errored');
    await promise_rejects_js(t, TypeError,
                          Promise.all([writePromise, closePromise]),
                          'writable should be errored');
  }, `TextDecoderStream should be able to reject invalid sequences in ` +
     `${encoding.name}`);
}
