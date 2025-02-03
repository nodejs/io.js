const util = require('node:util');
const assert = require('node:assert');
assert.ok(util.getCallSites().length > 1);
process.stdout.write(util.getCallSites()[0].scriptName);
