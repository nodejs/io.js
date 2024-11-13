
const {
  setDeserializeMainFunction,
} = require('v8').startupSnapshot;

console.log(`During snapshot building, Error.stackTraceLimit =`, Error.stackTraceLimit);
console.log(getError('During snapshot building', 30));

setDeserializeMainFunction(() => {
  console.log(`After snapshot deserialization, Error.stackTraceLimit =`, Error.stackTraceLimit);
  console.log(getError('After snapshot deserialization', 30));
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
