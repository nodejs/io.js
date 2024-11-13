'use strict';

const { enableCompileCache, getCompileCacheDir, constants } = require('module');

console.log('dir before enableCompileCache:', getCompileCacheDir());
const result = enableCompileCache(process.env.NODE_TEST_COMPILE_CACHE_DIR);
switch (result.status) {
  case constants.compileCacheStatus.FAILED:
    console.log('Compile cache failed. ' + result.message);
    break;
  case constants.compileCacheStatus.ENABLED:
    console.log('Compile cache enabled. ' + result.directory);
    break;
  case constants.compileCacheStatus.ALREADY_ENABLED:
    console.log('Compile cache already enabled.');
    break;
  case constants.compileCacheStatus.DISABLED:
    console.log('Compile cache already disabled.');
    break;
}
console.log('dir after enableCompileCache:', getCompileCacheDir());
