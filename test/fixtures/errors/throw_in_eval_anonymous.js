'use strict';
require('../../common');

Error.stackTraceLimit = 1;
eval(`

  throw new Error('error in anonymous script');

`)
