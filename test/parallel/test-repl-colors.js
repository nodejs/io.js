'use strict';
require('../common');
const { Duplex } = require('stream');
const { inspect } = require('util');
const { strictEqual } = require('assert');
const { REPLServer } = require('repl');

let output = '';

const inout = new Duplex({ decodeStrings: false });
inout._read = function() {
  this.push('util.inspect("string")\n');
  this.push(null);
};
inout._write = function(s, _, cb) {
  output += s;
  cb();
};

const repl = new REPLServer({ input: inout, output: inout, useColors: true });
inout.isTTY = true;
const repl2 = new REPLServer({ input: inout, output: inout });

process.on('exit', function() {
  // https://github.com/nodejs/node/pull/16485#issuecomment-350428638
  // The color setting of the REPL should not have leaked over into
  // the color setting of `util.inspect.defaultOptions`.
  strictEqual(output.includes(`"'string'"`), true);
  strictEqual(output.includes(`'\u001b[32m\\'string\\'\u001b[39m'`), false);
  strictEqual(inspect.defaultOptions.colors, false);
  strictEqual(repl.writer.options.colors, true);
  strictEqual(repl2.writer.options.colors, true);
});
