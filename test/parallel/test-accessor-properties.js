// Flags: --expose-internals --no-warnings
'use strict';

const { hasCrypto } = require('../common');

// This tests that the accessor properties do not raise assertions
// when called with incompatible receivers.

const assert = require('assert');
const { test } = require('node:test');

// Objects that call StreamBase::AddMethods, when setting up
// their prototype
const { internalBinding } = require('internal/test/binding');
const { TTY } = internalBinding('tty_wrap');
const { UDP } = internalBinding('udp_wrap');

test('Should throw instead of raise assertions', () => {
  assert.throws(() => {
    UDP.prototype.fd; // eslint-disable-line no-unused-expressions
  }, TypeError);

  const StreamWrapProto = Object.getPrototypeOf(TTY.prototype);
  const properties = ['bytesRead', 'fd', '_externalStream'];

  properties.forEach((property) => {
    // Should throw instead of raise assertions
    assert.throws(() => {
      TTY.prototype[property]; // eslint-disable-line no-unused-expressions
    }, TypeError, `Missing expected TypeError for TTY.prototype.${property}`);

    // Should not throw for Object.getOwnPropertyDescriptor
    assert.strictEqual(
      typeof Object.getOwnPropertyDescriptor(StreamWrapProto, property),
      'object',
      'typeof property descriptor ' + property + ' is not \'object\''
    );
  });
});

test('There are accessor properties in crypto too', { skip: !hasCrypto }, () => {
  // There are accessor properties in crypto too
  const crypto = internalBinding('crypto');  // eslint-disable-line node-core/crypto-check

  assert.throws(() => {
    // eslint-disable-next-line no-unused-expressions
    crypto.SecureContext.prototype._external;
  }, TypeError);

  assert.strictEqual(
    typeof Object.getOwnPropertyDescriptor(
      crypto.SecureContext.prototype, '_external'),
    'object'
  );
});
