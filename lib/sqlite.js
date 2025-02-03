'use strict';
const { emitExperimentalWarning } = require('internal/util');

emitExperimentalWarning('SQLite');
module.exports = internalBinding('sqlite');
