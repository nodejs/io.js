'use strict';

const { WPTRunner } = require('../common/wpt');

const runner = new WPTRunner('urlpattern');

runner.pretendGlobalThisAs('Window');
runner.runJsTests();
