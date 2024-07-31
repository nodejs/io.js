// Flags: --expose-internals
'use strict';

// Check that if libuv reports a memory error on a DNS query, that the memory
// error is passed through and not replaced with ENOTFOUND.

require('../common');

const assert = require('assert');
const errors = require('internal/errors');
const { internalBinding } = require('internal/test/binding');

const { UV_EAI_MEMORY } = internalBinding('uv');
const memoryError = new errors.DNSException(UV_EAI_MEMORY, 'fhqwhgads');

assert.strictEqual(memoryError.code, 'EAI_MEMORY');
const stack = memoryError.stack.split('\n');
assert.match(stack[1], /^ {4}at Object/);
