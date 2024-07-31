'use strict';
require('../common');
const { testWasiPreview1 } = require('../common/wasi');

testWasiPreview1([process.platform === 'win32' ? 'poll_win' : 'poll']);
