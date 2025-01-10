// Flags:  --enable-source-maps

'use strict';
require('../../../common');
Error.stackTraceLimit = 3;

const fs = require('fs');

const content = fs.readFileSync(require.resolve('../tabs.js'), 'utf8');
// SourceURL magic comment is hardcoded in the source content.
eval(content);
