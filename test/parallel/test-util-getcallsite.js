'use strict';

require('../common');
const { getCallSite } = require('node:util');
const { expectWarning } = require('../common');

const warning = 'The `util.getCallSite` API has been renamed to `util.getCallSites()`.';
expectWarning('ExperimentalWarning', warning);
getCallSite();
