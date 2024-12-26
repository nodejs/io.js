'use strict'

self.test_blob = (fn, expectations) => {
  var expected = expectations.expected,
      type = expectations.type,
      desc = expectations.desc;

  promise_test(async (t) => {
    var blob = fn();
    assert_true(blob instanceof Blob);
    assert_false(blob instanceof File);
    assert_equals(blob.type, type);
    assert_equals(blob.size, expected.length);

    const text = await blob.text();
    assert_equals(text, expected);
  }, desc);
}

self.test_blob_binary = (fn, expectations) => {
  var expected = expectations.expected,
      type = expectations.type,
      desc = expectations.desc;

  promise_test(async (t) => {
    var blob = fn();
    assert_true(blob instanceof Blob);
    assert_false(blob instanceof File);
    assert_equals(blob.type, type);
    assert_equals(blob.size, expected.length);

    const ab = await blob.arrayBuffer();
    assert_true(ab instanceof ArrayBuffer,
      "Result should be an ArrayBuffer");
    assert_array_equals(new Uint8Array(ab), expected);
  }, desc);
}

// Assert that two TypedArray objects have the same byte values
self.assert_equals_typed_array = (array1, array2) => {
  const [view1, view2] = [array1, array2].map((array) => {
    assert_true(array.buffer instanceof ArrayBuffer,
      'Expect input ArrayBuffers to contain field `buffer`');
    return new DataView(array.buffer, array.byteOffset, array.byteLength);
  });

  assert_equals(view1.byteLength, view2.byteLength,
    'Expect both arrays to be of the same byte length');

  const byteLength = view1.byteLength;

  for (let i = 0; i < byteLength; ++i) {
    assert_equals(view1.getUint8(i), view2.getUint8(i),
      `Expect byte at buffer position ${i} to be equal`);
  }
}
