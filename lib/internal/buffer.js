'use strict';

const {
  BigInt,
  Float32Array,
  Float64Array,
  MathFloor,
  Number,
  Uint8Array,
} = primordials;

const {
  ERR_BUFFER_OUT_OF_BOUNDS,
  ERR_INVALID_ARG_TYPE,
  ERR_OUT_OF_RANGE,
} = require('internal/errors').codes;
const { validateNumber } = require('internal/validators');
const {
  asciiSlice,
  base64Slice,
  base64urlSlice,
  latin1Slice,
  hexSlice,
  ucs2Slice,
  utf8Slice,
  asciiWriteStatic,
  base64Write,
  base64urlWrite,
  latin1WriteStatic,
  hexWrite,
  ucs2Write,
  utf8WriteStatic,
  getZeroFillToggle,
} = internalBinding('buffer');

const {
  privateSymbols: {
    untransferable_object_private_symbol,
  },
} = internalBinding('util');

// Temporary buffers to convert numbers.
const float32Array = new Float32Array(1);
const uInt8Float32Array = new Uint8Array(float32Array.buffer);
const float64Array = new Float64Array(1);
const uInt8Float64Array = new Uint8Array(float64Array.buffer);

// Check endianness.
float32Array[0] = -1; // 0xBF800000
// Either it is [0, 0, 128, 191] or [191, 128, 0, 0]. It is not possible to
// check this with `os.endianness()` because that is determined at compile time.
const bigEndian = uInt8Float32Array[3] === 0;

function checkBounds(buf, offset, byteLength) {
  validateNumber(offset, 'offset');
  if (buf[offset] === undefined || buf[offset + byteLength] === undefined)
    boundsError(offset, buf.length - (byteLength + 1));
}

function checkInt(value, min, max, buf, offset, byteLength) {
  if (value > max || value < min) {
    const n = typeof min === 'bigint' ? 'n' : '';
    let range;
    if (byteLength > 3) {
      if (min === 0 || min === 0n) {
        range = `>= 0${n} and < 2${n} ** ${(byteLength + 1) * 8}${n}`;
      } else {
        range = `>= -(2${n} ** ${(byteLength + 1) * 8 - 1}${n}) and ` +
                `< 2${n} ** ${(byteLength + 1) * 8 - 1}${n}`;
      }
    } else {
      range = `>= ${min}${n} and <= ${max}${n}`;
    }
    throw new ERR_OUT_OF_RANGE('value', range, value);
  }
  checkBounds(buf, offset, byteLength);
}

function boundsError(value, length, type) {
  if (MathFloor(value) !== value) {
    validateNumber(value, type);
    throw new ERR_OUT_OF_RANGE(type || 'offset', 'an integer', value);
  }

  if (length < 0)
    throw new ERR_BUFFER_OUT_OF_BOUNDS();

  throw new ERR_OUT_OF_RANGE(type || 'offset',
                             `>= ${type ? 1 : 0} and <= ${length}`,
                             value);
}

// Read integers.
function readBigUInt64LE(offset = 0) {
  validateNumber(offset, 'offset');
  const first = this[offset];
  const last = this[offset + 7];
  if (first === undefined || last === undefined)
    boundsError(offset, this.length - 8);

  const lo = first +
    this[++offset] * 2 ** 8 +
    this[++offset] * 2 ** 16 +
    this[++offset] * 2 ** 24;

  const hi = this[++offset] +
    this[++offset] * 2 ** 8 +
    this[++offset] * 2 ** 16 +
    last * 2 ** 24;

  return BigInt(lo) + (BigInt(hi) << 32n);
}

function readBigUInt64BE(offset = 0) {
  validateNumber(offset, 'offset');
  const first = this[offset];
  const last = this[offset + 7];
  if (first === undefined || last === undefined)
    boundsError(offset, this.length - 8);

  const hi = first * 2 ** 24 +
    this[++offset] * 2 ** 16 +
    this[++offset] * 2 ** 8 +
    this[++offset];

  const lo = this[++offset] * 2 ** 24 +
    this[++offset] * 2 ** 16 +
    this[++offset] * 2 ** 8 +
    last;

  return (BigInt(hi) << 32n) + BigInt(lo);
}

function readBigInt64LE(offset = 0) {
  validateNumber(offset, 'offset');
  const first = this[offset];
  const last = this[offset + 7];
  if (first === undefined || last === undefined)
    boundsError(offset, this.length - 8);

  const val = this[offset + 4] +
    this[offset + 5] * 2 ** 8 +
    this[offset + 6] * 2 ** 16 +
    (last << 24); // Overflow
  return (BigInt(val) << 32n) +
    BigInt(first +
    this[++offset] * 2 ** 8 +
    this[++offset] * 2 ** 16 +
    this[++offset] * 2 ** 24);
}

function readBigInt64BE(offset = 0) {
  validateNumber(offset, 'offset');
  const first = this[offset];
  const last = this[offset + 7];
  if (first === undefined || last === undefined)
    boundsError(offset, this.length - 8);

  const val = (first << 24) + // Overflow
    this[++offset] * 2 ** 16 +
    this[++offset] * 2 ** 8 +
    this[++offset];
  return (BigInt(val) << 32n) +
    BigInt(this[++offset] * 2 ** 24 +
    this[++offset] * 2 ** 16 +
    this[++offset] * 2 ** 8 +
    last);
}

function readUIntLE(offset, byteLength) {
  if (offset === undefined)
    throw new ERR_INVALID_ARG_TYPE('offset', 'number', offset);
  if (byteLength === 6)
    return readUInt48LE(this, offset);
  if (byteLength === 5)
    return readUInt40LE(this, offset);
  if (byteLength === 3)
    return readUInt24LE(this, offset);
  if (byteLength === 4)
    return this.readUInt32LE(offset);
  if (byteLength === 2)
    return this.readUInt16LE(offset);
  if (byteLength === 1)
    return this.readUInt8(offset);

  boundsError(byteLength, 6, 'byteLength');
}

function readUInt48LE(buf, offset = 0) {
  validateNumber(offset, 'offset');
  const first = buf[offset];
  const last = buf[offset + 5];
  if (first === undefined || last === undefined)
    boundsError(offset, buf.length - 6);

  return first +
    buf[++offset] * 2 ** 8 +
    buf[++offset] * 2 ** 16 +
    buf[++offset] * 2 ** 24 +
    (buf[++offset] + last * 2 ** 8) * 2 ** 32;
}

function readUInt40LE(buf, offset = 0) {
  validateNumber(offset, 'offset');
  const first = buf[offset];
  const last = buf[offset + 4];
  if (first === undefined || last === undefined)
    boundsError(offset, buf.length - 5);

  return first +
    buf[++offset] * 2 ** 8 +
    buf[++offset] * 2 ** 16 +
    buf[++offset] * 2 ** 24 +
    last * 2 ** 32;
}

function readUInt32LE(offset = 0) {
  validateNumber(offset, 'offset');
  const first = this[offset];
  const last = this[offset + 3];
  if (first === undefined || last === undefined)
    boundsError(offset, this.length - 4);

  return first +
    this[++offset] * 2 ** 8 +
    this[++offset] * 2 ** 16 +
    last * 2 ** 24;
}

function readUInt24LE(buf, offset = 0) {
  validateNumber(offset, 'offset');
  const first = buf[offset];
  const last = buf[offset + 2];
  if (first === undefined || last === undefined)
    boundsError(offset, buf.length - 3);

  return first + buf[++offset] * 2 ** 8 + last * 2 ** 16;
}

function readUInt16LE(offset = 0) {
  validateNumber(offset, 'offset');
  const first = this[offset];
  const last = this[offset + 1];
  if (first === undefined || last === undefined)
    boundsError(offset, this.length - 2);

  return first + last * 2 ** 8;
}

function readUInt8(offset = 0) {
  validateNumber(offset, 'offset');
  const val = this[offset];
  if (val === undefined)
    boundsError(offset, this.length - 1);

  return val;
}

function readUIntBE(offset, byteLength) {
  if (offset === undefined)
    throw new ERR_INVALID_ARG_TYPE('offset', 'number', offset);
  if (byteLength === 6)
    return readUInt48BE(this, offset);
  if (byteLength === 5)
    return readUInt40BE(this, offset);
  if (byteLength === 3)
    return readUInt24BE(this, offset);
  if (byteLength === 4)
    return this.readUInt32BE(offset);
  if (byteLength === 2)
    return this.readUInt16BE(offset);
  if (byteLength === 1)
    return this.readUInt8(offset);

  boundsError(byteLength, 6, 'byteLength');
}

function readUInt48BE(buf, offset = 0) {
  validateNumber(offset, 'offset');
  const first = buf[offset];
  const last = buf[offset + 5];
  if (first === undefined || last === undefined)
    boundsError(offset, buf.length - 6);

  return (first * 2 ** 8 + buf[++offset]) * 2 ** 32 +
    buf[++offset] * 2 ** 24 +
    buf[++offset] * 2 ** 16 +
    buf[++offset] * 2 ** 8 +
    last;
}

function readUInt40BE(buf, offset = 0) {
  validateNumber(offset, 'offset');
  const first = buf[offset];
  const last = buf[offset + 4];
  if (first === undefined || last === undefined)
    boundsError(offset, buf.length - 5);

  return first * 2 ** 32 +
    buf[++offset] * 2 ** 24 +
    buf[++offset] * 2 ** 16 +
    buf[++offset] * 2 ** 8 +
    last;
}

function readUInt32BE(offset = 0) {
  validateNumber(offset, 'offset');
  const first = this[offset];
  const last = this[offset + 3];
  if (first === undefined || last === undefined)
    boundsError(offset, this.length - 4);

  return first * 2 ** 24 +
    this[++offset] * 2 ** 16 +
    this[++offset] * 2 ** 8 +
    last;
}

function readUInt24BE(buf, offset = 0) {
  validateNumber(offset, 'offset');
  const first = buf[offset];
  const last = buf[offset + 2];
  if (first === undefined || last === undefined)
    boundsError(offset, buf.length - 3);

  return first * 2 ** 16 + buf[++offset] * 2 ** 8 + last;
}

function readUInt16BE(offset = 0) {
  validateNumber(offset, 'offset');
  const first = this[offset];
  const last = this[offset + 1];
  if (first === undefined || last === undefined)
    boundsError(offset, this.length - 2);

  return first * 2 ** 8 + last;
}

function readIntLE(offset, byteLength) {
  if (offset === undefined)
    throw new ERR_INVALID_ARG_TYPE('offset', 'number', offset);
  if (byteLength === 6)
    return readInt48LE(this, offset);
  if (byteLength === 5)
    return readInt40LE(this, offset);
  if (byteLength === 3)
    return readInt24LE(this, offset);
  if (byteLength === 4)
    return this.readInt32LE(offset);
  if (byteLength === 2)
    return this.readInt16LE(offset);
  if (byteLength === 1)
    return this.readInt8(offset);

  boundsError(byteLength, 6, 'byteLength');
}

function readInt48LE(buf, offset = 0) {
  validateNumber(offset, 'offset');
  const first = buf[offset];
  const last = buf[offset + 5];
  if (first === undefined || last === undefined)
    boundsError(offset, buf.length - 6);

  const val = buf[offset + 4] + last * 2 ** 8;
  return (val | (val & 2 ** 15) * 0x1fffe) * 2 ** 32 +
    first +
    buf[++offset] * 2 ** 8 +
    buf[++offset] * 2 ** 16 +
    buf[++offset] * 2 ** 24;
}

function readInt40LE(buf, offset = 0) {
  validateNumber(offset, 'offset');
  const first = buf[offset];
  const last = buf[offset + 4];
  if (first === undefined || last === undefined)
    boundsError(offset, buf.length - 5);

  return (last | (last & 2 ** 7) * 0x1fffffe) * 2 ** 32 +
    first +
    buf[++offset] * 2 ** 8 +
    buf[++offset] * 2 ** 16 +
    buf[++offset] * 2 ** 24;
}

function readInt32LE(offset = 0) {
  validateNumber(offset, 'offset');
  const first = this[offset];
  const last = this[offset + 3];
  if (first === undefined || last === undefined)
    boundsError(offset, this.length - 4);

  return first +
    this[++offset] * 2 ** 8 +
    this[++offset] * 2 ** 16 +
    (last << 24); // Overflow
}

function readInt24LE(buf, offset = 0) {
  validateNumber(offset, 'offset');
  const first = buf[offset];
  const last = buf[offset + 2];
  if (first === undefined || last === undefined)
    boundsError(offset, buf.length - 3);

  const val = first + buf[++offset] * 2 ** 8 + last * 2 ** 16;
  return val | (val & 2 ** 23) * 0x1fe;
}

function readInt16LE(offset = 0) {
  validateNumber(offset, 'offset');
  const first = this[offset];
  const last = this[offset + 1];
  if (first === undefined || last === undefined)
    boundsError(offset, this.length - 2);

  const val = first + last * 2 ** 8;
  return val | (val & 2 ** 15) * 0x1fffe;
}

function readInt8(offset = 0) {
  validateNumber(offset, 'offset');
  const val = this[offset];
  if (val === undefined)
    boundsError(offset, this.length - 1);

  return val | (val & 2 ** 7) * 0x1fffffe;
}

function readIntBE(offset, byteLength) {
  if (offset === undefined)
    throw new ERR_INVALID_ARG_TYPE('offset', 'number', offset);
  if (byteLength === 6)
    return readInt48BE(this, offset);
  if (byteLength === 5)
    return readInt40BE(this, offset);
  if (byteLength === 3)
    return readInt24BE(this, offset);
  if (byteLength === 4)
    return this.readInt32BE(offset);
  if (byteLength === 2)
    return this.readInt16BE(offset);
  if (byteLength === 1)
    return this.readInt8(offset);

  boundsError(byteLength, 6, 'byteLength');
}

function readInt48BE(buf, offset = 0) {
  validateNumber(offset, 'offset');
  const first = buf[offset];
  const last = buf[offset + 5];
  if (first === undefined || last === undefined)
    boundsError(offset, buf.length - 6);

  const val = buf[++offset] + first * 2 ** 8;
  return (val | (val & 2 ** 15) * 0x1fffe) * 2 ** 32 +
    buf[++offset] * 2 ** 24 +
    buf[++offset] * 2 ** 16 +
    buf[++offset] * 2 ** 8 +
    last;
}

function readInt40BE(buf, offset = 0) {
  validateNumber(offset, 'offset');
  const first = buf[offset];
  const last = buf[offset + 4];
  if (first === undefined || last === undefined)
    boundsError(offset, buf.length - 5);

  return (first | (first & 2 ** 7) * 0x1fffffe) * 2 ** 32 +
    buf[++offset] * 2 ** 24 +
    buf[++offset] * 2 ** 16 +
    buf[++offset] * 2 ** 8 +
    last;
}

function readInt32BE(offset = 0) {
  validateNumber(offset, 'offset');
  const first = this[offset];
  const last = this[offset + 3];
  if (first === undefined || last === undefined)
    boundsError(offset, this.length - 4);

  return (first << 24) + // Overflow
    this[++offset] * 2 ** 16 +
    this[++offset] * 2 ** 8 +
    last;
}

function readInt24BE(buf, offset = 0) {
  validateNumber(offset, 'offset');
  const first = buf[offset];
  const last = buf[offset + 2];
  if (first === undefined || last === undefined)
    boundsError(offset, buf.length - 3);

  const val = first * 2 ** 16 + buf[++offset] * 2 ** 8 + last;
  return val | (val & 2 ** 23) * 0x1fe;
}

function readInt16BE(offset = 0) {
  validateNumber(offset, 'offset');
  const first = this[offset];
  const last = this[offset + 1];
  if (first === undefined || last === undefined)
    boundsError(offset, this.length - 2);

  const val = first * 2 ** 8 + last;
  return val | (val & 2 ** 15) * 0x1fffe;
}

// Read floats
function readFloatBackwards(offset = 0) {
  validateNumber(offset, 'offset');
  const first = this[offset];
  const last = this[offset + 3];
  if (first === undefined || last === undefined)
    boundsError(offset, this.length - 4);

  uInt8Float32Array[3] = first;
  uInt8Float32Array[2] = this[++offset];
  uInt8Float32Array[1] = this[++offset];
  uInt8Float32Array[0] = last;
  return float32Array[0];
}

function readFloatForwards(offset = 0) {
  validateNumber(offset, 'offset');
  const first = this[offset];
  const last = this[offset + 3];
  if (first === undefined || last === undefined)
    boundsError(offset, this.length - 4);

  uInt8Float32Array[0] = first;
  uInt8Float32Array[1] = this[++offset];
  uInt8Float32Array[2] = this[++offset];
  uInt8Float32Array[3] = last;
  return float32Array[0];
}

function readDoubleBackwards(offset = 0) {
  validateNumber(offset, 'offset');
  const first = this[offset];
  const last = this[offset + 7];
  if (first === undefined || last === undefined)
    boundsError(offset, this.length - 8);

  uInt8Float64Array[7] = first;
  uInt8Float64Array[6] = this[++offset];
  uInt8Float64Array[5] = this[++offset];
  uInt8Float64Array[4] = this[++offset];
  uInt8Float64Array[3] = this[++offset];
  uInt8Float64Array[2] = this[++offset];
  uInt8Float64Array[1] = this[++offset];
  uInt8Float64Array[0] = last;
  return float64Array[0];
}

function readDoubleForwards(offset = 0) {
  validateNumber(offset, 'offset');
  const first = this[offset];
  const last = this[offset + 7];
  if (first === undefined || last === undefined)
    boundsError(offset, this.length - 8);

  uInt8Float64Array[0] = first;
  uInt8Float64Array[1] = this[++offset];
  uInt8Float64Array[2] = this[++offset];
  uInt8Float64Array[3] = this[++offset];
  uInt8Float64Array[4] = this[++offset];
  uInt8Float64Array[5] = this[++offset];
  uInt8Float64Array[6] = this[++offset];
  uInt8Float64Array[7] = last;
  return float64Array[0];
}

// Write integers.
function writeBigU_Int64LE(buf, value, offset, min, max) {
  checkInt(value, min, max, buf, offset, 7);

  let lo = Number(value & 0xffffffffn);
  buf[offset++] = lo;
  lo = lo >> 8;
  buf[offset++] = lo;
  lo = lo >> 8;
  buf[offset++] = lo;
  lo = lo >> 8;
  buf[offset++] = lo;
  let hi = Number(value >> 32n & 0xffffffffn);
  buf[offset++] = hi;
  hi = hi >> 8;
  buf[offset++] = hi;
  hi = hi >> 8;
  buf[offset++] = hi;
  hi = hi >> 8;
  buf[offset++] = hi;
  return offset;
}

function writeBigUInt64LE(value, offset = 0) {
  return writeBigU_Int64LE(this, value, offset, 0n, 0xffffffffffffffffn);
}

function writeBigU_Int64BE(buf, value, offset, min, max) {
  checkInt(value, min, max, buf, offset, 7);

  let lo = Number(value & 0xffffffffn);
  buf[offset + 7] = lo;
  lo = lo >> 8;
  buf[offset + 6] = lo;
  lo = lo >> 8;
  buf[offset + 5] = lo;
  lo = lo >> 8;
  buf[offset + 4] = lo;
  let hi = Number(value >> 32n & 0xffffffffn);
  buf[offset + 3] = hi;
  hi = hi >> 8;
  buf[offset + 2] = hi;
  hi = hi >> 8;
  buf[offset + 1] = hi;
  hi = hi >> 8;
  buf[offset] = hi;
  return offset + 8;
}

function writeBigUInt64BE(value, offset = 0) {
  return writeBigU_Int64BE(this, value, offset, 0n, 0xffffffffffffffffn);
}

function writeBigInt64LE(value, offset = 0) {
  return writeBigU_Int64LE(
    this, value, offset, -0x8000000000000000n, 0x7fffffffffffffffn);
}

function writeBigInt64BE(value, offset = 0) {
  return writeBigU_Int64BE(
    this, value, offset, -0x8000000000000000n, 0x7fffffffffffffffn);
}

function writeUIntLE(value, offset, byteLength) {
  if (byteLength === 6)
    return writeU_Int48LE(this, value, offset, 0, 0xffffffffffff);
  if (byteLength === 5)
    return writeU_Int40LE(this, value, offset, 0, 0xffffffffff);
  if (byteLength === 3)
    return writeU_Int24LE(this, value, offset, 0, 0xffffff);
  if (byteLength === 4)
    return writeU_Int32LE(this, value, offset, 0, 0xffffffff);
  if (byteLength === 2)
    return writeU_Int16LE(this, value, offset, 0, 0xffff);
  if (byteLength === 1)
    return writeU_Int8(this, value, offset, 0, 0xff);

  boundsError(byteLength, 6, 'byteLength');
}

function writeU_Int48LE(buf, value, offset, min, max) {
  value = +value;
  checkInt(value, min, max, buf, offset, 5);

  const newVal = MathFloor(value * 2 ** -32);
  buf[offset++] = value;
  value = value >>> 8;
  buf[offset++] = value;
  value = value >>> 8;
  buf[offset++] = value;
  value = value >>> 8;
  buf[offset++] = value;
  buf[offset++] = newVal;
  buf[offset++] = (newVal >>> 8);
  return offset;
}

function writeU_Int40LE(buf, value, offset, min, max) {
  value = +value;
  checkInt(value, min, max, buf, offset, 4);

  const newVal = value;
  buf[offset++] = value;
  value = value >>> 8;
  buf[offset++] = value;
  value = value >>> 8;
  buf[offset++] = value;
  value = value >>> 8;
  buf[offset++] = value;
  buf[offset++] = MathFloor(newVal * 2 ** -32);
  return offset;
}

function writeU_Int32LE(buf, value, offset, min, max) {
  value = +value;
  checkInt(value, min, max, buf, offset, 3);

  buf[offset++] = value;
  value = value >>> 8;
  buf[offset++] = value;
  value = value >>> 8;
  buf[offset++] = value;
  value = value >>> 8;
  buf[offset++] = value;
  return offset;
}

function writeUInt32LE(value, offset = 0) {
  return writeU_Int32LE(this, value, offset, 0, 0xffffffff);
}

function writeU_Int24LE(buf, value, offset, min, max) {
  value = +value;
  checkInt(value, min, max, buf, offset, 2);

  buf[offset++] = value;
  value = value >>> 8;
  buf[offset++] = value;
  value = value >>> 8;
  buf[offset++] = value;
  return offset;
}

function writeU_Int16LE(buf, value, offset, min, max) {
  value = +value;
  checkInt(value, min, max, buf, offset, 1);

  buf[offset++] = value;
  buf[offset++] = (value >>> 8);
  return offset;
}

function writeUInt16LE(value, offset = 0) {
  return writeU_Int16LE(this, value, offset, 0, 0xffff);
}

function writeU_Int8(buf, value, offset, min, max) {
  value = +value;
  // `checkInt()` can not be used here because it checks two entries.
  validateNumber(offset, 'offset');
  if (value > max || value < min) {
    throw new ERR_OUT_OF_RANGE('value', `>= ${min} and <= ${max}`, value);
  }
  if (buf[offset] === undefined)
    boundsError(offset, buf.length - 1);

  buf[offset] = value;
  return offset + 1;
}

function writeUInt8(value, offset = 0) {
  return writeU_Int8(this, value, offset, 0, 0xff);
}

function writeUIntBE(value, offset, byteLength) {
  if (byteLength === 6)
    return writeU_Int48BE(this, value, offset, 0, 0xffffffffffff);
  if (byteLength === 5)
    return writeU_Int40BE(this, value, offset, 0, 0xffffffffff);
  if (byteLength === 3)
    return writeU_Int24BE(this, value, offset, 0, 0xffffff);
  if (byteLength === 4)
    return writeU_Int32BE(this, value, offset, 0, 0xffffffff);
  if (byteLength === 2)
    return writeU_Int16BE(this, value, offset, 0, 0xffff);
  if (byteLength === 1)
    return writeU_Int8(this, value, offset, 0, 0xff);

  boundsError(byteLength, 6, 'byteLength');
}

function writeU_Int48BE(buf, value, offset, min, max) {
  value = +value;
  checkInt(value, min, max, buf, offset, 5);

  const newVal = MathFloor(value * 2 ** -32);
  buf[offset++] = (newVal >>> 8);
  buf[offset++] = newVal;
  buf[offset + 3] = value;
  value = value >>> 8;
  buf[offset + 2] = value;
  value = value >>> 8;
  buf[offset + 1] = value;
  value = value >>> 8;
  buf[offset] = value;
  return offset + 4;
}

function writeU_Int40BE(buf, value, offset, min, max) {
  value = +value;
  checkInt(value, min, max, buf, offset, 4);

  buf[offset++] = MathFloor(value * 2 ** -32);
  buf[offset + 3] = value;
  value = value >>> 8;
  buf[offset + 2] = value;
  value = value >>> 8;
  buf[offset + 1] = value;
  value = value >>> 8;
  buf[offset] = value;
  return offset + 4;
}

function writeU_Int32BE(buf, value, offset, min, max) {
  value = +value;
  checkInt(value, min, max, buf, offset, 3);

  buf[offset + 3] = value;
  value = value >>> 8;
  buf[offset + 2] = value;
  value = value >>> 8;
  buf[offset + 1] = value;
  value = value >>> 8;
  buf[offset] = value;
  return offset + 4;
}

function writeUInt32BE(value, offset = 0) {
  return writeU_Int32BE(this, value, offset, 0, 0xffffffff);
}

function writeU_Int24BE(buf, value, offset, min, max) {
  value = +value;
  checkInt(value, min, max, buf, offset, 2);

  buf[offset + 2] = value;
  value = value >>> 8;
  buf[offset + 1] = value;
  value = value >>> 8;
  buf[offset] = value;
  return offset + 3;
}

function writeU_Int16BE(buf, value, offset, min, max) {
  value = +value;
  checkInt(value, min, max, buf, offset, 1);

  buf[offset++] = (value >>> 8);
  buf[offset++] = value;
  return offset;
}

function writeUInt16BE(value, offset = 0) {
  return writeU_Int16BE(this, value, offset, 0, 0xffff);
}

function writeIntLE(value, offset, byteLength) {
  if (byteLength === 6)
    return writeU_Int48LE(this, value, offset, -0x800000000000, 0x7fffffffffff);
  if (byteLength === 5)
    return writeU_Int40LE(this, value, offset, -0x8000000000, 0x7fffffffff);
  if (byteLength === 3)
    return writeU_Int24LE(this, value, offset, -0x800000, 0x7fffff);
  if (byteLength === 4)
    return writeU_Int32LE(this, value, offset, -0x80000000, 0x7fffffff);
  if (byteLength === 2)
    return writeU_Int16LE(this, value, offset, -0x8000, 0x7fff);
  if (byteLength === 1)
    return writeU_Int8(this, value, offset, -0x80, 0x7f);

  boundsError(byteLength, 6, 'byteLength');
}

function writeInt32LE(value, offset = 0) {
  return writeU_Int32LE(this, value, offset, -0x80000000, 0x7fffffff);
}

function writeInt16LE(value, offset = 0) {
  return writeU_Int16LE(this, value, offset, -0x8000, 0x7fff);
}

function writeInt8(value, offset = 0) {
  return writeU_Int8(this, value, offset, -0x80, 0x7f);
}

function writeIntBE(value, offset, byteLength) {
  if (byteLength === 6)
    return writeU_Int48BE(this, value, offset, -0x800000000000, 0x7fffffffffff);
  if (byteLength === 5)
    return writeU_Int40BE(this, value, offset, -0x8000000000, 0x7fffffffff);
  if (byteLength === 3)
    return writeU_Int24BE(this, value, offset, -0x800000, 0x7fffff);
  if (byteLength === 4)
    return writeU_Int32BE(this, value, offset, -0x80000000, 0x7fffffff);
  if (byteLength === 2)
    return writeU_Int16BE(this, value, offset, -0x8000, 0x7fff);
  if (byteLength === 1)
    return writeU_Int8(this, value, offset, -0x80, 0x7f);

  boundsError(byteLength, 6, 'byteLength');
}

function writeInt32BE(value, offset = 0) {
  return writeU_Int32BE(this, value, offset, -0x80000000, 0x7fffffff);
}

function writeInt16BE(value, offset = 0) {
  return writeU_Int16BE(this, value, offset, -0x8000, 0x7fff);
}

// Write floats.
function writeDoubleForwards(val, offset = 0) {
  val = +val;
  checkBounds(this, offset, 7);

  float64Array[0] = val;
  this[offset++] = uInt8Float64Array[0];
  this[offset++] = uInt8Float64Array[1];
  this[offset++] = uInt8Float64Array[2];
  this[offset++] = uInt8Float64Array[3];
  this[offset++] = uInt8Float64Array[4];
  this[offset++] = uInt8Float64Array[5];
  this[offset++] = uInt8Float64Array[6];
  this[offset++] = uInt8Float64Array[7];
  return offset;
}

function writeDoubleBackwards(val, offset = 0) {
  val = +val;
  checkBounds(this, offset, 7);

  float64Array[0] = val;
  this[offset++] = uInt8Float64Array[7];
  this[offset++] = uInt8Float64Array[6];
  this[offset++] = uInt8Float64Array[5];
  this[offset++] = uInt8Float64Array[4];
  this[offset++] = uInt8Float64Array[3];
  this[offset++] = uInt8Float64Array[2];
  this[offset++] = uInt8Float64Array[1];
  this[offset++] = uInt8Float64Array[0];
  return offset;
}

function writeFloatForwards(val, offset = 0) {
  val = +val;
  checkBounds(this, offset, 3);

  float32Array[0] = val;
  this[offset++] = uInt8Float32Array[0];
  this[offset++] = uInt8Float32Array[1];
  this[offset++] = uInt8Float32Array[2];
  this[offset++] = uInt8Float32Array[3];
  return offset;
}

function writeFloatBackwards(val, offset = 0) {
  val = +val;
  checkBounds(this, offset, 3);

  float32Array[0] = val;
  this[offset++] = uInt8Float32Array[3];
  this[offset++] = uInt8Float32Array[2];
  this[offset++] = uInt8Float32Array[1];
  this[offset++] = uInt8Float32Array[0];
  return offset;
}

class FastBuffer extends Uint8Array {
  // Using an explicit constructor here is necessary to avoid relying on
  // `Array.prototype[Symbol.iterator]`, which can be mutated by users.
  // eslint-disable-next-line no-useless-constructor
  constructor(bufferOrLength, byteOffset, length) {
    super(bufferOrLength, byteOffset, length);
  }
}

function addBufferPrototypeMethods(proto) {
  proto.readBigUInt64LE = readBigUInt64LE;
  proto.readBigUInt64BE = readBigUInt64BE;
  proto.readBigUint64LE = readBigUInt64LE;
  proto.readBigUint64BE = readBigUInt64BE;
  proto.readBigInt64LE = readBigInt64LE;
  proto.readBigInt64BE = readBigInt64BE;
  proto.writeBigUInt64LE = writeBigUInt64LE;
  proto.writeBigUInt64BE = writeBigUInt64BE;
  proto.writeBigUint64LE = writeBigUInt64LE;
  proto.writeBigUint64BE = writeBigUInt64BE;
  proto.writeBigInt64LE = writeBigInt64LE;
  proto.writeBigInt64BE = writeBigInt64BE;

  proto.readUIntLE = readUIntLE;
  proto.readUInt32LE = readUInt32LE;
  proto.readUInt16LE = readUInt16LE;
  proto.readUInt8 = readUInt8;
  proto.readUIntBE = readUIntBE;
  proto.readUInt32BE = readUInt32BE;
  proto.readUInt16BE = readUInt16BE;
  proto.readUintLE = readUIntLE;
  proto.readUint32LE = readUInt32LE;
  proto.readUint16LE = readUInt16LE;
  proto.readUint8 = readUInt8;
  proto.readUintBE = readUIntBE;
  proto.readUint32BE = readUInt32BE;
  proto.readUint16BE = readUInt16BE;
  proto.readIntLE = readIntLE;
  proto.readInt32LE = readInt32LE;
  proto.readInt16LE = readInt16LE;
  proto.readInt8 = readInt8;
  proto.readIntBE = readIntBE;
  proto.readInt32BE = readInt32BE;
  proto.readInt16BE = readInt16BE;

  proto.writeUIntLE = writeUIntLE;
  proto.writeUInt32LE = writeUInt32LE;
  proto.writeUInt16LE = writeUInt16LE;
  proto.writeUInt8 = writeUInt8;
  proto.writeUIntBE = writeUIntBE;
  proto.writeUInt32BE = writeUInt32BE;
  proto.writeUInt16BE = writeUInt16BE;
  proto.writeUintLE = writeUIntLE;
  proto.writeUint32LE = writeUInt32LE;
  proto.writeUint16LE = writeUInt16LE;
  proto.writeUint8 = writeUInt8;
  proto.writeUintBE = writeUIntBE;
  proto.writeUint32BE = writeUInt32BE;
  proto.writeUint16BE = writeUInt16BE;
  proto.writeIntLE = writeIntLE;
  proto.writeInt32LE = writeInt32LE;
  proto.writeInt16LE = writeInt16LE;
  proto.writeInt8 = writeInt8;
  proto.writeIntBE = writeIntBE;
  proto.writeInt32BE = writeInt32BE;
  proto.writeInt16BE = writeInt16BE;

  proto.readFloatLE = bigEndian ? readFloatBackwards : readFloatForwards;
  proto.readFloatBE = bigEndian ? readFloatForwards : readFloatBackwards;
  proto.readDoubleLE = bigEndian ? readDoubleBackwards : readDoubleForwards;
  proto.readDoubleBE = bigEndian ? readDoubleForwards : readDoubleBackwards;
  proto.writeFloatLE = bigEndian ? writeFloatBackwards : writeFloatForwards;
  proto.writeFloatBE = bigEndian ? writeFloatForwards : writeFloatBackwards;
  proto.writeDoubleLE = bigEndian ? writeDoubleBackwards : writeDoubleForwards;
  proto.writeDoubleBE = bigEndian ? writeDoubleForwards : writeDoubleBackwards;

  proto.asciiSlice = asciiSlice;
  proto.base64Slice = base64Slice;
  proto.base64urlSlice = base64urlSlice;
  proto.latin1Slice = latin1Slice;
  proto.hexSlice = hexSlice;
  proto.ucs2Slice = ucs2Slice;
  proto.utf8Slice = utf8Slice;
  proto.asciiWrite = function(string, offset = 0, length = this.byteLength - offset) {
    if (offset < 0 || offset > this.byteLength) {
      throw new ERR_BUFFER_OUT_OF_BOUNDS('offset');
    }
    if (length < 0 || length > this.byteLength - offset) {
      throw new ERR_BUFFER_OUT_OF_BOUNDS('length');
    }
    return asciiWriteStatic(this, string, offset, length);
  };
  proto.base64Write = base64Write;
  proto.base64urlWrite = base64urlWrite;
  proto.latin1Write = function(string, offset = 0, length = this.byteLength - offset) {
    if (offset < 0 || offset > this.byteLength) {
      throw new ERR_BUFFER_OUT_OF_BOUNDS('offset');
    }
    if (length < 0 || length > this.byteLength - offset) {
      throw new ERR_BUFFER_OUT_OF_BOUNDS('length');
    }
    return latin1WriteStatic(this, string, offset, length);
  };
  proto.hexWrite = hexWrite;
  proto.ucs2Write = ucs2Write;
  proto.utf8Write = function(string, offset = 0, length = this.byteLength - offset) {
    if (offset < 0 || offset > this.byteLength) {
      throw new ERR_BUFFER_OUT_OF_BOUNDS('offset');
    }
    if (length < 0 || length > this.byteLength - offset) {
      throw new ERR_BUFFER_OUT_OF_BOUNDS('length');
    }
    return utf8WriteStatic(this, string, offset, length);
  };
}

// This would better be placed in internal/worker/io.js, but that doesn't work
// because Buffer needs this and that would introduce a cyclic dependency.
function markAsUntransferable(obj) {
  if ((typeof obj !== 'object' && typeof obj !== 'function') || obj === null)
    return;  // This object is a primitive and therefore already untransferable.
  obj[untransferable_object_private_symbol] = true;
}

// This simply checks if the object is marked as untransferable and doesn't
// check whether we are able to transfer it.
function isMarkedAsUntransferable(obj) {
  if (obj == null)
    return false;
  // Private symbols are not inherited.
  return obj[untransferable_object_private_symbol] !== undefined;
}

// A toggle used to access the zero fill setting of the array buffer allocator
// in C++.
// |zeroFill| can be undefined when running inside an isolate where we
// do not own the ArrayBuffer allocator.  Zero fill is always on in that case.
let zeroFill = getZeroFillToggle();
function createUnsafeBuffer(size) {
  zeroFill[0] = 0;
  try {
    return new FastBuffer(size);
  } finally {
    zeroFill[0] = 1;
  }
}

// The connection between the JS land zero fill toggle and the
// C++ one in the NodeArrayBufferAllocator gets lost if the toggle
// is deserialized from the snapshot, because V8 owns the underlying
// memory of this toggle. This resets the connection.
function reconnectZeroFillToggle() {
  zeroFill = getZeroFillToggle();
}

module.exports = {
  FastBuffer,
  addBufferPrototypeMethods,
  markAsUntransferable,
  isMarkedAsUntransferable,
  createUnsafeBuffer,
  readUInt16BE,
  readUInt32BE,
  reconnectZeroFillToggle,
};
