'use strict';
const common = require('../../../common');
const { before, after } = require('node:test');

before(common.mustCall(() => console.log('before')));
after(common.mustCall(() => console.log('after')));
