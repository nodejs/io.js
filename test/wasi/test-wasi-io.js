'use strict';
const common = require('../common');
const { checkoutEOL } = common;
const { testWasiPreview1 } = require('../common/wasi');

testWasiPreview1(['freopen'], {}, { stdout: `hello from input2.txt${checkoutEOL}` });
testWasiPreview1(['read_file'], {}, { stdout: `hello from input.txt${checkoutEOL}` });
testWasiPreview1(['read_file_twice'], {}, {
  stdout: `hello from input.txt${checkoutEOL}hello from input.txt${checkoutEOL}`,
});
// Tests that are currently unsupported on Windows.
if (!common.isWindows) {
  testWasiPreview1(['stdin'], { input: 'hello world' }, { stdout: 'hello world' });
}
