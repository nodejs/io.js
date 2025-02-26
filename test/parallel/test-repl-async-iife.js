'use strict';
require('../common');

// Note: This test ensures that async IIFE doesn't crash
// Ref: https://github.com/nodejs/node/issues/38685

const repl = require('repl').start({ terminal: true });

repl.write('(async() => { })()\n');
repl.write('.exit\n');
