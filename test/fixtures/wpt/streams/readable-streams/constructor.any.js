// META: global=window,worker,shadowrealm
'use strict';

const error1 = new Error('error1');
error1.name = 'error1';

const error2 = new Error('error2');
error2.name = 'error2';

test(() => {
  const underlyingSource = { get start() { throw error1; } };
  const queuingStrategy = { highWaterMark: 0, get size() { throw error2; } };

  // underlyingSource is converted in prose in the method body, whereas queuingStrategy is done at the IDL layer.
  // So the queuingStrategy exception should be encountered first.
  assert_throws_exactly(error2, () => new ReadableStream(underlyingSource, queuingStrategy));
}, 'underlyingSource argument should be converted after queuingStrategy argument');
