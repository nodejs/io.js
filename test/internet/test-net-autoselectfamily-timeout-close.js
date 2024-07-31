'use strict';

const common = require('../common');
const { addresses } = require('../common/internet');

const assert = require('assert');
const { connect } = require('net');

//
// When testing this is MacOS, remember that the last connection will have no timeout at Node.js
// level but only at operating system one.
//
// The default for MacOS is 75 seconds. It can be changed by doing:
//
// sudo sysctl net.inet.tcp.keepinit=VALUE_IN_MS
//

// Test that when all errors are returned when no connections succeeded and that the close event is emitted
{
  const connection = connect({
    host: addresses.INET_HOST,
    port: 10,
    autoSelectFamily: true,
    // Purposely set this to a low value because we want all non last connection to fail due to early timeout
    autoSelectFamilyAttemptTimeout: 10,
  });

  connection.on('close', common.mustCall());
  connection.on('ready', common.mustNotCall());

  connection.on('error', common.mustCall((error) => {
    assert.ok(connection.autoSelectFamilyAttemptedAddresses.length > 0);
    assert.strictEqual(error.constructor.name, 'AggregateError');
    assert.ok(error.errors.length > 0);
  }));
}
