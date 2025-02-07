'use strict';

require('../common');

const { throws } = require('assert');
const { URLPattern } = require('url');

// Verify that if an error is thrown while accessing any of the
// init options, the error is appropriately propagated.
throws(() => {
  new URLPattern({
    get protocol() {
      throw new Error('boom');
    }
  });
}, {
  message: 'boom',
});

// Verify that if an error is thrown while accessing the ignoreCase
// option, the error is appropriately propagated.
throws(() => {
  new URLPattern({}, { get ignoreCase() {
    throw new Error('boom');
  } });
}, {
  message: 'boom'
});
