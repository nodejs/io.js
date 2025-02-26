// Flags:  --enable-source-maps

'use strict';
require('../../../common');
const vm = require('vm');
Error.stackTraceLimit = 3;

const fs = require('fs');
const filename = require.resolve('../tabs.js');
const content = fs.readFileSync(filename, 'utf8');

const context = vm.createContext({});
vm.runInContext('Error.stackTraceLimit = 3', context);

// Scripts created implicitly with `vm.runInContext`.
try {
  vm.runInContext(content, context, {
    filename,
  });
} catch (e) {
  console.error(e);
}

// Scripts created explicitly with `vm.Script`.
const script = new vm.Script(content, {
  filename,
});
script.runInContext(context);
