const {
  addSerializeCallback,
  setDeserializeMainFunction,
} = require('v8').startupSnapshot;

// To make sure the cwd is present in the cache
process.cwd();

// Also access it from a serialization callback once
addSerializeCallback(() => {
  process.cwd();
});

setDeserializeMainFunction(() => {
  console.log(process.cwd());
});
