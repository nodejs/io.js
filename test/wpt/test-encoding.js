'use strict';

const { WPTRunner } = require('../common/wpt');
const runner = new WPTRunner('encoding');

runner.pretendGlobalThisAs('Window');

runner.runJsTests();
