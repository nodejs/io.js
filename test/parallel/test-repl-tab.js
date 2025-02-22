'use strict';
const common = require('../common');
const assert = require('assert');
const repl = require('repl');
const zlib = require('zlib');

// Just use builtin stream inherited from Duplex
const putIn = zlib.createGzip();
const testMe = repl.start('', putIn, function(cmd, context, filename,
                                              callback) {
  callback(null, cmd);
});

testMe._onEvalError = common.mustNotCall();

testMe.complete('', function(err, results) {
  assert.strictEqual(err, null);
});
