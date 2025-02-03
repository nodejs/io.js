// Flags: --expose-internals

'use strict';

require('../common');

const assert = require('node:assert');
const { internalBinding } = require('internal/test/binding');
const binding = internalBinding('encoding_binding');

{
  // Valid input
  const buf = Uint8Array.from([0xC1, 0xE9, 0xF3]);
  assert.strictEqual(binding.decodeLatin1(buf, false, false), 'Áéó');
}

{
  // Empty input
  const buf = Uint8Array.from([]);
  assert.strictEqual(binding.decodeLatin1(buf, false, false), '');
}

{
  // Invalid input, but Latin1 has no invalid chars and should never throw.
  const buf = new TextEncoder().encode('Invalid Latin1 🧑‍🧑‍🧒‍🧒');
  assert.strictEqual(
    binding.decodeLatin1(buf, false, false),
    'Invalid Latin1 ð\x9F§\x91â\x80\x8Dð\x9F§\x91â\x80\x8Dð\x9F§\x92â\x80\x8Dð\x9F§\x92'
  );
}

{
  // IgnoreBOM with BOM
  const buf = Uint8Array.from([0xFE, 0xFF, 0xC1, 0xE9, 0xF3]);
  assert.strictEqual(binding.decodeLatin1(buf, true, false), 'þÿÁéó');
}

{
  // Fatal and InvalidInput, but Latin1 has no invalid chars and should never throw.
  const buf = Uint8Array.from([0xFF, 0xFF, 0xFF]);
  assert.strictEqual(binding.decodeLatin1(buf, false, true), 'ÿÿÿ');
}

{
  // IgnoreBOM and Fatal, but Latin1 has no invalid chars and should never throw.
  const buf = Uint8Array.from([0xFE, 0xFF, 0xC1, 0xE9, 0xF3]);
  assert.strictEqual(binding.decodeLatin1(buf, true, true), 'þÿÁéó');
}
