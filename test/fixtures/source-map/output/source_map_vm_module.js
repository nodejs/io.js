// Flags:  --enable-source-maps --experimental-vm-modules

'use strict';
const common = require('../../../common');
const vm = require('vm');
Error.stackTraceLimit = 3;

const fs = require('fs');
const filename = require.resolve('../tabs.js');
const content = fs.readFileSync(filename, 'utf8');

const context = vm.createContext({});
vm.runInContext('Error.stackTraceLimit = 3', context);

(async () => {
  const mod = new vm.SourceTextModule(content, {
    context,
    identifier: filename,
  });
  await mod.link(common.mustNotCall());
  await mod.evaluate();
})();
