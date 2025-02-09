'use strict';

require('../common');
const assert = require('assert');
const tmpdir = require('../common/tmpdir');
const repl = require('repl');

tmpdir.refresh();
const r = repl.start({});
const historyPath = tmpdir.resolve('.history');

r.setupHistory(historyPath, () => { assert.strictEqual(r.closed, true); });
r.close();
