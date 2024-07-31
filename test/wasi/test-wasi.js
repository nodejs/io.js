'use strict';
const common = require('../common');
const { testWasiPreview1 } = require('../common/wasi');

// TODO(joyeecheung): tests that don't need special configurations can be ported
// to a special python test case configuration and get run in parallel.
// Tests that are currently unsupported on IBM i PASE.
if (!common.isIBMi) {
  testWasiPreview1(['clock_getres']);
  testWasiPreview1(['getrusage']);
}

// Tests that are currently unsupported on Windows and Android.
if (!common.isWindows && process.platform !== 'android') {
  testWasiPreview1(['readdir']);
}

testWasiPreview1(['cant_dotdot']);
testWasiPreview1(['fd_prestat_get_refresh']);
testWasiPreview1(['ftruncate']);
testWasiPreview1(['getentropy']);
testWasiPreview1(['gettimeofday']);
testWasiPreview1(['main_args']);
testWasiPreview1(['notdir']);
testWasiPreview1(['preopen_populates']);
testWasiPreview1(['stat']);
testWasiPreview1(['sock']);
testWasiPreview1(['write_file']);
