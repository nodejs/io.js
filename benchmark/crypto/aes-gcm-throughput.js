'use strict';
const common = require('../common.js');
const crypto = require('crypto');
const keylen = { 'aes-128-gcm': 16, 'aes-192-gcm': 24, 'aes-256-gcm': 32 };
const bench = common.createBenchmark(main, {
  n: [2500],
  cipher: ['aes-128-gcm', 'aes-192-gcm', 'aes-256-gcm'],
  len: [1024, 4 * 1024, 16 * 1024, 64 * 1024, 256 * 1024, 1024 * 1024],
});

function main({ n, len, cipher }) {
  const message = Buffer.alloc(len, 'b');
  const key = crypto.randomBytes(keylen[cipher]);
  const iv = crypto.randomBytes(12);
  const associate_data = Buffer.alloc(16, 'z');
  bench.start();
  AEAD_Bench(cipher, message, associate_data, key, iv, n, len);
}

function AEAD_Bench(cipher, message, associate_data, key, iv, n, len) {
  const written = n * len;
  const bits = written * 8;
  const mbits = bits / (1024 * 1024);

  for (let i = 0; i < n; i++) {
    const alice = crypto.createCipheriv(cipher, key, iv);
    alice.setAAD(associate_data);
    const enc = alice.update(message);
    alice.final();
    const tag = alice.getAuthTag();
    const bob = crypto.createDecipheriv(cipher, key, iv);
    bob.setAuthTag(tag);
    bob.setAAD(associate_data);
    bob.update(enc);
    bob.final();
  }

  bench.end(mbits);
}
