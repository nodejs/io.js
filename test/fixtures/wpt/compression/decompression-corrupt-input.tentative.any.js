// META global=window,worker,shadowrealm

// This test checks that DecompressionStream behaves according to the standard
// when the input is corrupted. To avoid a combinatorial explosion in the
// number of tests, we only mutate one field at a time, and we only test
// "interesting" values.

'use strict';

// The many different cases are summarised in this data structure.
const expectations = [
  {
    format: 'deflate',

    // Decompresses to 'expected output'.
    baseInput: [120, 156, 75, 173, 40, 72, 77, 46, 73, 77, 81, 200, 47, 45, 41,
                40, 45, 1, 0, 48, 173, 6, 36],

    // See RFC1950 for the definition of the various fields used by deflate:
    // https://tools.ietf.org/html/rfc1950.
    fields: [
      {
        // The function of this field. This matches the name used in the RFC.
        name: 'CMF',

        // The offset of the field in bytes from the start of the input.
        offset: 0,

        // The length of the field in bytes.
        length: 1,

        cases: [
          {
            // The value to set the field to. If the field contains multiple
            // bytes, all the bytes will be set to this value.
            value: 0,

            // The expected result. 'success' means the input is decoded
            // successfully. 'error' means that the stream will be errored.
            result: 'error'
          }
        ]
      },
      {
        name: 'FLG',
        offset: 1,
        length: 1,

        // FLG contains a 4-bit checksum (FCHECK) which is calculated in such a
        // way that there are 4 valid values for this field.
        cases: [
          {
            value: 218,
            result: 'success'
          },
          {
            value: 1,
            result: 'success'
          },
          {
            value: 94,
            result: 'success'
          },
          {
            // The remaining 252 values cause an error.
            value: 157,
            result: 'error'
          }
        ]
      },
      {
        name: 'DATA',
        // In general, changing any bit of the data will trigger a checksum
        // error. Only the last byte does anything else.
        offset: 18,
        length: 1,
        cases: [
          {
            value: 4,
            result: 'success'
          },
          {
            value: 5,
            result: 'error'
          }
        ]
      },
      {
        name: 'ADLER',
        offset: -4,
        length: 4,
        cases: [
          {
            value: 255,
            result: 'error'
          }
        ]
      }
    ]
  },
  {
    format: 'gzip',

    // Decompresses to 'expected output'.
    baseInput: [31, 139, 8, 0, 0, 0, 0, 0, 0, 3, 75, 173, 40, 72, 77, 46, 73,
                77, 81, 200, 47, 45, 41, 40, 45, 1, 0, 176, 1, 57, 179, 15, 0,
                0, 0],

    // See RFC1952 for the definition of the various fields used by gzip:
    // https://tools.ietf.org/html/rfc1952.
    fields: [
      {
        name: 'ID',
        offset: 0,
        length: 2,
        cases: [
          {
            value: 255,
            result: 'error'
          }
        ]
      },
      {
        name: 'CM',
        offset: 2,
        length: 1,
        cases: [
          {
            value: 0,
            result: 'error'
          }
        ]
      },
      {
        name: 'FLG',
        offset: 3,
        length: 1,
        cases: [
          {
            value: 1, // FTEXT
            result: 'success'
          },
          {
            value: 2, // FHCRC
            result: 'error'
          }
        ]
      },
      {
        name: 'MTIME',
        offset: 4,
        length: 4,
        cases: [
          {
            // Any value is valid for this field.
            value: 255,
            result: 'success'
          }
        ]
      },
      {
        name: 'XFL',
        offset: 8,
        length: 1,
        cases: [
          {
            // Any value is accepted.
            value: 255,
            result: 'success'
          }
        ]
      },
      {
        name: 'OS',
        offset: 9,
        length: 1,
        cases: [
          {
            // Any value is accepted.
            value: 128,
            result: 'success'
          }
        ]
      },
      {
        name: 'DATA',

        // The last byte of the data is the most interesting.
        offset: 26,
        length: 1,
        cases: [
          {
            value: 3,
            result: 'error'
          },
          {
            value: 4,
            result: 'success'
          }
        ]
      },
      {
        name: 'CRC',
        offset: -8,
        length: 4,
        cases: [
          {
            // Any change will error the stream.
            value: 0,
            result: 'error'
          }
        ]
      },
      {
        name: 'ISIZE',
        offset: -4,
        length: 4,
        cases: [
          {
            // A mismatch will error the stream.
            value: 1,
            result: 'error'
          }
        ]
      }
    ]
  }
];

async function tryDecompress(input, format) {
  const ds = new DecompressionStream(format);
  const reader = ds.readable.getReader();
  const writer = ds.writable.getWriter();
  writer.write(input).catch(() => {});
  writer.close().catch(() => {});
  let out = [];
  while (true) {
    try {
      const { value, done } = await reader.read();
      if (done) {
        break;
      }
      out = out.concat(Array.from(value));
    } catch (e) {
      if (e instanceof TypeError) {
        return { result: 'error' };
      } else {
        return { result: e.name };
      }
    }
  }
  const expectedOutput = 'expected output';
  if (out.length !== expectedOutput.length) {
    return { result: 'corrupt' };
  }
  for (let i = 0; i < out.length; ++i) {
    if (out[i] !== expectedOutput.charCodeAt(i)) {
      return { result: 'corrupt' };
    }
  }
  return { result: 'success' };
}

function corruptInput(input, offset, length, value) {
  const output = new Uint8Array(input);
  if (offset < 0) {
    offset += input.length;
  }
  for (let i = offset; i < offset + length; ++i) {
    output[i] = value;
  }
  return output;
}

for (const { format, baseInput, fields } of expectations) {
  promise_test(async () => {
    const { result } = await tryDecompress(new Uint8Array(baseInput), format);
    assert_equals(result, 'success', 'decompression should succeed');
  }, `the unchanged input for '${format}' should decompress successfully`);

  promise_test(async () => {
    const truncatedInput = new Uint8Array(baseInput.slice(0, -1));
    const { result } = await tryDecompress(truncatedInput, format);
    assert_equals(result, 'error', 'decompression should fail');
  }, `truncating the input for '${format}' should give an error`);

  promise_test(async () => {
    const extendedInput = new Uint8Array(baseInput.concat([0]));
    const { result } = await tryDecompress(extendedInput, format);
    assert_equals(result, 'error', 'decompression should fail');
  }, `trailing junk for '${format}' should give an error`);

  for (const { name, offset, length, cases } of fields) {
    for (const { value, result } of cases) {
      promise_test(async () => {
        const corruptedInput = corruptInput(baseInput, offset, length, value);
        const { result: actual } =
              await tryDecompress(corruptedInput, format);
        assert_equals(actual, result, 'result should match');
      }, `format '${format}' field ${name} should be ${result} for ${value}`);
    }
  }
}

promise_test(async () => {
  // Data generated in Python:
  // ```py
  // h = b"thequickbrownfoxjumped\x00"
  // words = h.split()
  // zdict = b''.join(words)
  // co = zlib.compressobj(zdict=zdict)
  // cd = co.compress(h) + co.flush()
  // ```
  const { result } = await tryDecompress(new Uint8Array([
    0x78, 0xbb, 0x74, 0xee, 0x09, 0x59, 0x2b, 0xc1, 0x2e, 0x0c, 0x00, 0x74, 0xee, 0x09, 0x59
  ]), 'deflate');
  assert_equals(result, 'error', 'Data compressed with a dictionary should throw TypeError');
}, 'the deflate input compressed with dictionary should give an error')
