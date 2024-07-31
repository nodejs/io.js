'use strict';

const { WPTRunner } = require('../common/wpt');

const runner = new WPTRunner('compression');

runner.pretendGlobalThisAs('Window');

runner.runJsTests();
