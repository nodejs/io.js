'use strict';
require('../common');
const { readFileSync } = require('fs');
const { testWasiPreview1 } = require('../common/wasi');

const checkoutEOL = readFileSync(__filename).includes('\r\n') ? '\r\n' : '\n';

// TODO(@jasnell): It's not entirely clear what this test is asserting.
// More comments would be helpful.

testWasiPreview1(['freopen'], {}, { stdout: `hello from input2.txt${checkoutEOL}` });
testWasiPreview1(['read_file'], {}, { stdout: `hello from input.txt${checkoutEOL}` });
testWasiPreview1(['read_file_twice'], {}, {
  stdout: `hello from input.txt${checkoutEOL}hello from input.txt${checkoutEOL}`,
});
// Tests that are currently unsupported on Windows.
if (process.platform !== 'win32') {
  testWasiPreview1(['stdin'], { input: 'hello world' }, { stdout: 'hello world' });
}
