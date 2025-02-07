'use strict';

require('../common');

// Assigning to itself should not throw.
globalThis.console = globalThis.console; // eslint-disable-line no-self-assign
