// Flags:  --enable-source-maps

'use strict';
require('../../../common');
const vm = require('vm');

const fs = require('fs');
const filename = require.resolve('../tabs.js');
const content = fs.readFileSync(filename, 'utf8');

const context = vm.createContext({});
vm.runInContext('Error.stackTraceLimit = 3', context);

const fn = vm.compileFunction(content, [], {
  filename,
  parsingContext: context,
})
fn();
