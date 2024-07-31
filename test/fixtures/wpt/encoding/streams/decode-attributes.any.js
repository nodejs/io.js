// META: global=window,worker,shadowrealm

'use strict';

// Verify that constructor arguments are correctly reflected in the attributes.

// Mapping of the first argument to TextDecoderStream to the expected value of
// the encoding attribute. We assume that if this subset works correctly, the
// rest probably work too.
const labelToName = {
  'unicode-1-1-utf-8': 'utf-8',
  'iso-8859-2': 'iso-8859-2',
  'ascii': 'windows-1252',
  'utf-16': 'utf-16le'
};

for (const label of Object.keys(labelToName)) {
  test(() => {
    const stream = new TextDecoderStream(label);
    assert_equals(stream.encoding, labelToName[label], 'encoding should match');
  }, `encoding attribute should have correct value for '${label}'`);
}

for (const falseValue of [false, 0, '', undefined, null]) {
  test(() => {
    const stream = new TextDecoderStream('utf-8', { fatal: falseValue });
    assert_false(stream.fatal, 'fatal should be false');
  }, `setting fatal to '${falseValue}' should set the attribute to false`);

  test(() => {
    const stream = new TextDecoderStream('utf-8', { ignoreBOM: falseValue });
    assert_false(stream.ignoreBOM, 'ignoreBOM should be false');
  }, `setting ignoreBOM to '${falseValue}' should set the attribute to false`);
}

for (const trueValue of [true, 1, {}, [], 'yes']) {
  test(() => {
    const stream = new TextDecoderStream('utf-8', { fatal: trueValue });
    assert_true(stream.fatal, 'fatal should be true');
  }, `setting fatal to '${trueValue}' should set the attribute to true`);

  test(() => {
    const stream = new TextDecoderStream('utf-8', { ignoreBOM: trueValue });
    assert_true(stream.ignoreBOM, 'ignoreBOM should be true');
  }, `setting ignoreBOM to '${trueValue}' should set the attribute to true`);
}

test(() => {
  assert_throws_js(RangeError, () => new TextDecoderStream(''),
                   'the constructor should throw');
}, 'constructing with an invalid encoding should throw');

test(() => {
  assert_throws_js(TypeError, () => new TextDecoderStream({
    toString() { return {}; }
  }), 'the constructor should throw');
}, 'constructing with a non-stringifiable encoding should throw');

test(() => {
  assert_throws_js(Error,
                   () => new TextDecoderStream('utf-8', {
                     get fatal() { throw new Error(); }
                   }), 'the constructor should throw');
}, 'a throwing fatal member should cause the constructor to throw');

test(() => {
  assert_throws_js(Error,
                   () => new TextDecoderStream('utf-8', {
                     get ignoreBOM() { throw new Error(); }
                   }), 'the constructor should throw');
}, 'a throwing ignoreBOM member should cause the constructor to throw');
