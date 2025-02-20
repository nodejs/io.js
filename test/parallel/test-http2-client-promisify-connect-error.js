'use strict';

const common = require('../common');

if (!common.hasCrypto)
  common.skip('missing crypto');
const assert = require('assert');
const http2 = require('http2');
const util = require('util');

const connect = util.promisify(http2.connect);

const error = new Error('Unable to resolve hostname');

function lookup(hostname, options, callback) {
  callback(error);
}

assert.rejects(
  connect('http://hostname', { lookup }),
  error,
).then(common.mustCall());
