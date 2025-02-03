// Flags: --enable-source-maps
'use strict';

require('../common');
const test = require('node:test');

// Verify that test runner can handle invalid source maps.

test('ok', () => {});

// eslint-disable-next-line @stylistic/js/spaced-comment
//# sourceMappingURL=data:application/json;base64,-1
