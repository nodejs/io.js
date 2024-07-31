// META: global=window,dedicatedworker,shadowrealm
// META: title=Encoding API: Basics

test(function() {
    assert_equals((new TextEncoder).encoding, 'utf-8', 'default encoding is utf-8');
    assert_equals((new TextDecoder).encoding, 'utf-8', 'default encoding is utf-8');
}, 'Default encodings');

test(function() {
    assert_array_equals(new TextEncoder().encode(), [], 'input default should be empty string')
    assert_array_equals(new TextEncoder().encode(undefined), [], 'input default should be empty string')
}, 'Default inputs');


function testDecodeSample(encoding, string, bytes) {
  test(function() {
    assert_equals(new TextDecoder(encoding).decode(new Uint8Array(bytes)), string);
    assert_equals(new TextDecoder(encoding).decode(new Uint8Array(bytes).buffer), string);
  }, 'Decode sample: ' + encoding);
}

// z (ASCII U+007A), cent (Latin-1 U+00A2), CJK water (BMP U+6C34),
// G-Clef (non-BMP U+1D11E), PUA (BMP U+F8FF), PUA (non-BMP U+10FFFD)
// byte-swapped BOM (non-character U+FFFE)
var sample = 'z\xA2\u6C34\uD834\uDD1E\uF8FF\uDBFF\uDFFD\uFFFE';

test(function() {
  var encoding = 'utf-8';
  var string = sample;
  var bytes = [0x7A, 0xC2, 0xA2, 0xE6, 0xB0, 0xB4, 0xF0, 0x9D, 0x84, 0x9E, 0xEF, 0xA3, 0xBF, 0xF4, 0x8F, 0xBF, 0xBD, 0xEF, 0xBF, 0xBE];
  var encoded = new TextEncoder().encode(string);
  assert_array_equals([].slice.call(encoded), bytes);
  assert_equals(new TextDecoder(encoding).decode(new Uint8Array(bytes)), string);
  assert_equals(new TextDecoder(encoding).decode(new Uint8Array(bytes).buffer), string);
}, 'Encode/decode round trip: utf-8');

testDecodeSample(
  'utf-16le',
  sample,
  [0x7A, 0x00, 0xA2, 0x00, 0x34, 0x6C, 0x34, 0xD8, 0x1E, 0xDD, 0xFF, 0xF8, 0xFF, 0xDB, 0xFD, 0xDF, 0xFE, 0xFF]
);

testDecodeSample(
  'utf-16be',
  sample,
  [0x00, 0x7A, 0x00, 0xA2, 0x6C, 0x34, 0xD8, 0x34, 0xDD, 0x1E, 0xF8, 0xFF, 0xDB, 0xFF, 0xDF, 0xFD, 0xFF, 0xFE]
);

testDecodeSample(
  'utf-16',
  sample,
  [0x7A, 0x00, 0xA2, 0x00, 0x34, 0x6C, 0x34, 0xD8, 0x1E, 0xDD, 0xFF, 0xF8, 0xFF, 0xDB, 0xFD, 0xDF, 0xFE, 0xFF]
);
