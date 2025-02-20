/* eslint-disable node-core/crypto-check */

'use strict';
const crypto = require('crypto');
const net = require('net');
const assert = require('assert');

exports.ccs = Buffer.from('140303000101', 'hex');

class TestTLSSocket extends net.Socket {
  constructor(server_cert) {
    super();
    this.server_cert = server_cert;
    this.version = Buffer.from('0303', 'hex');
    this.handshake_list = [];
    // AES128-GCM-SHA256
    this.ciphers = Buffer.from('000002009c0', 'hex');
    this.pre_primary_secret =
      Buffer.concat([this.version, crypto.randomBytes(46)]);
    this.primary_secret = null;
    this.write_seq = 0;
    this.client_random = crypto.randomBytes(32);

    this.on('handshake', (msg) => {
      this.handshake_list.push(msg);
    });

    this.on('server_random', (server_random) => {
      this.primary_secret = PRF12('sha256', this.pre_primary_secret,
                                  'primary secret',
                                  Buffer.concat([this.client_random,
                                                 server_random]),
                                  48);
      const key_block = PRF12('sha256', this.primary_secret,
                              'key expansion',
                              Buffer.concat([server_random,
                                             this.client_random]),
                              40);
      this.client_writeKey = key_block.slice(0, 16);
      this.client_writeIV = key_block.slice(32, 36);
    });
  }

  createClientHello() {
    const compressions = Buffer.from('0100', 'hex'); // null
    const msg = addHandshakeHeader(0x01, Buffer.concat([
      this.version, this.client_random, this.ciphers, compressions,
    ]));
    this.emit('handshake', msg);
    return addRecordHeader(0x16, msg);
  }

  createClientKeyExchange() {
    const encrypted_pre_primary_secret = crypto.publicEncrypt({
      key: this.server_cert,
      padding: crypto.constants.RSA_PKCS1_PADDING,
    }, this.pre_primary_secret);
    const length = Buffer.alloc(2);
    length.writeUIntBE(encrypted_pre_primary_secret.length, 0, 2);
    const msg = addHandshakeHeader(0x10, Buffer.concat([
      length, encrypted_pre_primary_secret]));
    this.emit('handshake', msg);
    return addRecordHeader(0x16, msg);
  }

  createFinished() {
    const shasum = crypto.createHash('sha256');
    shasum.update(Buffer.concat(this.handshake_list));
    const message_hash = shasum.digest();
    const r = PRF12('sha256', this.primary_secret,
                    'client finished', message_hash, 12);
    const msg = addHandshakeHeader(0x14, r);
    this.emit('handshake', msg);
    return addRecordHeader(0x16, msg);
  }

  createIllegalHandshake() {
    const illegal_handshake = Buffer.alloc(5);
    return addRecordHeader(0x16, illegal_handshake);
  }

  parseTLSFrame(buf) {
    let offset = 0;
    const record = buf.slice(offset, 5);
    const type = record[0];
    const length = record.slice(3, 5).readUInt16BE(0);
    offset += 5;
    let remaining = buf.slice(offset, offset + length);
    if (type === 0x16) {
      do {
        remaining = this.parseTLSHandshake(remaining);
      } while (remaining.length > 0);
    }
    offset += length;
    return buf.slice(offset);
  }

  parseTLSHandshake(buf) {
    let offset = 0;
    const handshake_type = buf[offset];
    if (handshake_type === 0x02) {
      const server_random = buf.slice(6, 6 + 32);
      this.emit('server_random', server_random);
    }
    offset += 1;
    const length = buf.readUIntBE(offset, 3);
    offset += 3;
    const handshake = buf.slice(0, offset + length);
    this.emit('handshake', handshake);
    offset += length;
    const remaining = buf.slice(offset);
    return remaining;
  }

  encrypt(plain) {
    const type = plain.slice(0, 1);
    const version = plain.slice(1, 3);
    const nonce = crypto.randomBytes(8);
    const iv = Buffer.concat([this.client_writeIV.slice(0, 4), nonce]);
    const bob = crypto.createCipheriv('aes-128-gcm', this.client_writeKey, iv);
    const write_seq = Buffer.alloc(8);
    write_seq.writeUInt32BE(this.write_seq++, 4);
    const aad = Buffer.concat([write_seq, plain.slice(0, 5)]);
    bob.setAAD(aad);
    const encrypted1 = bob.update(plain.slice(5));
    const encrypted = Buffer.concat([encrypted1, bob.final()]);
    const tag = bob.getAuthTag();
    const length = Buffer.alloc(2);
    length.writeUInt16BE(nonce.length + encrypted.length + tag.length, 0);
    return Buffer.concat([type, version, length, nonce, encrypted, tag]);
  }
}

function addRecordHeader(type, frame) {
  const record_layer = Buffer.from('0003030000', 'hex');
  record_layer[0] = type;
  record_layer.writeUInt16BE(frame.length, 3);
  return Buffer.concat([record_layer, frame]);
}

function addHandshakeHeader(type, msg) {
  const handshake_header = Buffer.alloc(4);
  handshake_header[0] = type;
  handshake_header.writeUIntBE(msg.length, 1, 3);
  return Buffer.concat([handshake_header, msg]);
}

function PRF12(algo, secret, label, seed, size) {
  const newSeed = Buffer.concat([Buffer.from(label, 'utf8'), seed]);
  return P_hash(algo, secret, newSeed, size);
}

function P_hash(algo, secret, seed, size) {
  const result = Buffer.alloc(size);
  let hmac = crypto.createHmac(algo, secret);
  hmac.update(seed);
  let a = hmac.digest();
  let j = 0;
  while (j < size) {
    hmac = crypto.createHmac(algo, secret);
    hmac.update(a);
    hmac.update(seed);
    const b = hmac.digest();
    let todo = b.length;
    if (j + todo > size) {
      todo = size - j;
    }
    b.copy(result, j, 0, todo);
    j += todo;
    hmac = crypto.createHmac(algo, secret);
    hmac.update(a);
    a = hmac.digest();
  }
  return result;
}

exports.assertIsCAArray = function assertIsCAArray(certs) {
  assert(Array.isArray(certs));
  assert(certs.length > 0);

  // The certificates looks PEM-encoded.
  for (const cert of certs) {
    const trimmed = cert.trim();
    assert.match(trimmed, /^-----BEGIN CERTIFICATE-----/);
    assert.match(trimmed, /-----END CERTIFICATE-----$/);
  }
};

exports.TestTLSSocket = TestTLSSocket;
