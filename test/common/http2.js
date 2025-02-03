'use strict';

// An HTTP/2 testing tool used to create mock frames for direct testing
// of HTTP/2 endpoints.

const kFrameData = Symbol('frame-data');
const FLAG_EOS = 0x1;
const FLAG_ACK = 0x1;
const FLAG_EOH = 0x4;
const FLAG_PADDED = 0x8;
const PADDING = Buffer.alloc(255);

const kClientMagic = Buffer.from('505249202a20485454502f322' +
                                 'e300d0a0d0a534d0d0a0d0a', 'hex');

const kFakeRequestHeaders = Buffer.from('828684410f7777772e65' +
                                        '78616d706c652e636f6d', 'hex');


const kFakeResponseHeaders = Buffer.from('4803333032580770726976617465611d' +
                                         '4d6f6e2c203231204f63742032303133' +
                                         '2032303a31333a323120474d546e1768' +
                                         '747470733a2f2f7777772e6578616d70' +
                                         '6c652e636f6d', 'hex');

function isUint32(val) {
  return val >>> 0 === val;
}

function isUint24(val) {
  return val >>> 0 === val && val <= 0xFFFFFF;
}

function isUint8(val) {
  return val >>> 0 === val && val <= 0xFF;
}

function write32BE(array, pos, val) {
  if (!isUint32(val))
    throw new RangeError('val is not a 32-bit number');
  array[pos++] = (val >> 24) & 0xff;
  array[pos++] = (val >> 16) & 0xff;
  array[pos++] = (val >> 8) & 0xff;
  array[pos++] = val & 0xff;
}

function write24BE(array, pos, val) {
  if (!isUint24(val))
    throw new RangeError('val is not a 24-bit number');
  array[pos++] = (val >> 16) & 0xff;
  array[pos++] = (val >> 8) & 0xff;
  array[pos++] = val & 0xff;
}

function write8(array, pos, val) {
  if (!isUint8(val))
    throw new RangeError('val is not an 8-bit number');
  array[pos] = val;
}

class Frame {
  constructor(length, type, flags, id) {
    this[kFrameData] = Buffer.alloc(9);
    write24BE(this[kFrameData], 0, length);
    write8(this[kFrameData], 3, type);
    write8(this[kFrameData], 4, flags);
    write32BE(this[kFrameData], 5, id);
  }

  get data() {
    return this[kFrameData];
  }
}

class SettingsFrame extends Frame {
  constructor(ack = false) {
    let flags = 0;
    if (ack)
      flags |= FLAG_ACK;
    super(0, 4, flags, 0);
  }
}

class HeadersFrame extends Frame {
  constructor(id, payload, padlen = 0, final = false) {
    let len = payload.length;
    let flags = FLAG_EOH;
    if (final) flags |= FLAG_EOS;
    const buffers = [payload];
    if (padlen > 0) {
      buffers.unshift(Buffer.from([padlen]));
      buffers.push(PADDING.slice(0, padlen));
      len += padlen + 1;
      flags |= FLAG_PADDED;
    }
    super(len, 1, flags, id);
    buffers.unshift(this[kFrameData]);
    this[kFrameData] = Buffer.concat(buffers);
  }
}

class PingFrame extends Frame {
  constructor(ack = false) {
    const buffers = [Buffer.alloc(8)];
    super(8, 6, ack ? 1 : 0, 0);
    buffers.unshift(this[kFrameData]);
    this[kFrameData] = Buffer.concat(buffers);
  }
}

class AltSvcFrame extends Frame {
  constructor(size) {
    const buffers = [Buffer.alloc(size)];
    super(size, 10, 0, 0);
    buffers.unshift(this[kFrameData]);
    this[kFrameData] = Buffer.concat(buffers);
  }
}

module.exports = {
  Frame,
  AltSvcFrame,
  HeadersFrame,
  SettingsFrame,
  PingFrame,
  kFakeRequestHeaders,
  kFakeResponseHeaders,
  kClientMagic,
};
