
const {
  addSerializeCallback,
  setDeserializeMainFunction,
} = require('v8').startupSnapshot;
const assert = require('assert');

if (process.env.TEST_IN_SERIALIZER) {
  addSerializeCallback(checkMutate);
} else {
  checkMutate();
}

function checkMutate() {
  // Check that mutation to Error.stackTraceLimit is effective in the snapshot
  // builder script.
  assert.strictEqual(typeof Error.stackTraceLimit, 'number');
  Error.stackTraceLimit = 0;
  assert.strictEqual(getError('', 30), 'Error');
}

setDeserializeMainFunction(() => {
  // Check that the mutation is preserved in the deserialized main function.
  assert.strictEqual(Error.stackTraceLimit, 0);
  assert.strictEqual(getError('', 30), 'Error');

  // Check that it can still be mutated.
  Error.stackTraceLimit = 10;
  const error = getError('', 30);
  const matches = [...error.matchAll(/at recurse/g)];
  assert.strictEqual(matches.length, 10);
});

function getError(message, depth) {
  let counter = 1;
  function recurse() {
    if (counter++ < depth) {
      return recurse();
    }
    const error = new Error(message);
    return error.stack;
  }
  return recurse();
}
