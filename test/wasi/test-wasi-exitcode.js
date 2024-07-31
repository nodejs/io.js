'use strict';
require('../common');
const { testWasiPreview1 } = require('../common/wasi');

testWasiPreview1(['exitcode']);
testWasiPreview1(['exitcode'], { env: { RETURN_ON_EXIT: true } });
testWasiPreview1(['exitcode'], { env: { RETURN_ON_EXIT: false } }, { status: 120 });
