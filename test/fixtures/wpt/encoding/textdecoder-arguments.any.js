// META: global=window,dedicatedworker,shadowrealm
// META: title=Encoding API: TextDecoder decode() optional arguments

test(t => {
  const decoder = new TextDecoder();

  // Just passing nothing.
  assert_equals(
    decoder.decode(undefined), '',
    'Undefined as first arg should decode to empty string');

  // Flushing an incomplete sequence.
  decoder.decode(new Uint8Array([0xc9]), {stream: true});
  assert_equals(
    decoder.decode(undefined), '\uFFFD',
    'Undefined as first arg should flush the stream');

}, 'TextDecoder decode() with explicit undefined');

test(t => {
  const decoder = new TextDecoder();

  // Just passing nothing.
  assert_equals(
    decoder.decode(undefined, undefined), '',
    'Undefined as first arg should decode to empty string');

  // Flushing an incomplete sequence.
  decoder.decode(new Uint8Array([0xc9]), {stream: true});
  assert_equals(
    decoder.decode(undefined, undefined), '\uFFFD',
    'Undefined as first arg should flush the stream');

}, 'TextDecoder decode() with undefined and undefined');

test(t => {
  const decoder = new TextDecoder();

  // Just passing nothing.
  assert_equals(
    decoder.decode(undefined, {}), '',
    'Undefined as first arg should decode to empty string');

  // Flushing an incomplete sequence.
  decoder.decode(new Uint8Array([0xc9]), {stream: true});
  assert_equals(
    decoder.decode(undefined, {}), '\uFFFD',
    'Undefined as first arg should flush the stream');

}, 'TextDecoder decode() with undefined and options');
