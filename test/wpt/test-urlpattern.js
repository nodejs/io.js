'use strict';

const { WPTRunner } = require('../common/wpt');

const runner = new WPTRunner('urlpattern');

runner.pretendGlobalThisAs('Window');
// TODO(@anonrig): Remove this once URLPattern is global.
runner.setInitScript(`global.URLPattern = require('node:url').URLPattern;`);
runner.runJsTests();
