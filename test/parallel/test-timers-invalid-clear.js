'use strict';

const common = require('../common');

// clearImmediate should be a noop if anything other than an Immediate
// is passed to it.

const t = setTimeout(common.mustCall());

clearImmediate(t);

setTimeout(common.mustCall());
setTimeout(common.mustCall());
