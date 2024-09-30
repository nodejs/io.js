'use strict';

const common = require('../common');

const { spawnSync } = require('child_process');
const assert = require('assert');

common.skipIfWorker();

{
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--experimental-permission',
      '-e',
      `
        const assert = require('assert');
        assert.ok(!process.permission.has('net.udp'));
        assert.ok(!process.permission.has('net.udp', 'localhost'));
        assert.ok(!process.permission.has('net.udp', '127.0.0.1/*'));
        assert.ok(!process.permission.has('net.udp', '127.0.0.1/9999'));
        assert.ok(!process.permission.has('net.udp', '*/9999'));
        assert.ok(!process.permission.has('net.udp', '::1/*'));
        assert.ok(!process.permission.has('net.udp', '::1/9999'));
      `,
    ]
  );
  if (status !== 0) {
    console.error(stderr.toString());
  }
  assert.strictEqual(status, 0);
}

{
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--experimental-permission',
      '--allow-net-udp=*',
      '-e',
      `
        const assert = require('assert');
        assert.ok(process.permission.has('net.udp'));
        assert.ok(process.permission.has('net.udp', 'localhost'));
        assert.ok(process.permission.has('net.udp', '127.0.0.1/*'));
        assert.ok(process.permission.has('net.udp', '127.0.0.1/9999'));
        assert.ok(process.permission.has('net.udp', '*/9999'));
        assert.ok(process.permission.has('net.udp', '::1/*'));
        assert.ok(process.permission.has('net.udp', '::1/9999'));
      `,
    ]
  );
  if (status !== 0) {
    console.error(stderr.toString());
  }
  assert.strictEqual(status, 0);
}

{
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--experimental-permission',
      '--allow-net-udp=*:9999',
      '-e',
      `
        const assert = require('assert');
        assert.ok(process.permission.has('net.udp', '127.0.0.1/9999'));
        assert.ok(process.permission.has('net.udp', '*/9999'));
        assert.ok(process.permission.has('net.udp', '::1/9999'));
        assert.ok(!process.permission.has('net.udp', '127.0.0.1/8888'));
      `,
    ]
  );
  if (status !== 0) {
    console.error(stderr.toString());
  }
  assert.strictEqual(status, 0);
}

{
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--experimental-permission',
      '--allow-net-udp=127.0.0.1:*',
      '--allow-net-udp=[::1]:*',
      '-e',
      `
        const assert = require('assert');
        assert.ok(process.permission.has('net.udp', '127.0.0.1/9999'));
        assert.ok(process.permission.has('net.udp', '127.0.0.1/*'));
        assert.ok(process.permission.has('net.udp', '::1/9999'));
        assert.ok(process.permission.has('net.udp', '::1/*'));
        assert.ok(!process.permission.has('net.udp', '127.0.0.2/9999'));
        assert.ok(!process.permission.has('net.udp', '::2/9999'));
      `,
    ]
  );
  if (status !== 0) {
    console.error(stderr.toString());
  }
  assert.strictEqual(status, 0);
}

{
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--experimental-permission',
      '--allow-net-udp=127.0.0.1:9999',
      '--allow-net-udp=[::1]:9999',
      '-e',
      `
        const assert = require('assert');
        assert.ok(process.permission.has('net.udp', '127.0.0.1/9999'));
        assert.ok(process.permission.has('net.udp', '::1/9999'));
        assert.ok(!process.permission.has('net.udp', '127.0.0.2/9999'));
        assert.ok(!process.permission.has('net.udp', '127.0.0.1/8888'));
        assert.ok(!process.permission.has('net.udp', '::2/9999'));
        assert.ok(!process.permission.has('net.udp', '::1/8888'));
      `,
    ]
  );
  if (status !== 0) {
    console.error(stderr.toString());
  }
  assert.strictEqual(status, 0);
}

{
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--experimental-permission',
      '--allow-net-udp=127.0.0.1:5555,[::1]:6666',
      '--allow-net-udp=127.0.0.2:8888,[::1]:9999',
      '-e',
      `
        const assert = require('assert');
        assert.ok(process.permission.has('net.udp', '127.0.0.1/5555'));
        assert.ok(process.permission.has('net.udp', '::1/6666'));
        assert.ok(process.permission.has('net.udp', '127.0.0.2/8888'));
        assert.ok(process.permission.has('net.udp', '::1/9999'));
        assert.ok(!process.permission.has('net.udp', '127.0.0.1/7777'));
        assert.ok(!process.permission.has('net.udp', '::2/9999'));
      `,
    ]
  );
  if (status !== 0) {
    console.error(stderr.toString());
  }
  assert.strictEqual(status, 0);
}

{
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--experimental-permission',
      '--allow-net-udp=127.255.255.1/8:9999',
      '--allow-net-udp=128.255.255.1/255.255.0.0:*',
      '--allow-net-udp=[1:1:1:1:1:1:1:1]/16',
      '-e',
      `
        const assert = require('assert');
        assert.ok(process.permission.has('net.udp', '127.1.1.1/9999'));
        assert.ok(process.permission.has('net.udp', '127.1.1.2/9999'));
        assert.ok(!process.permission.has('net.udp', '127.1.1.2/8888'));
        assert.ok(process.permission.has('net.udp', '128.255.0.0/9999'));
        assert.ok(!process.permission.has('net.udp', '128.254.0.0/9999'));
        assert.ok(process.permission.has('net.udp', '1:2:1:1:1:1:1:1/6666'));
        assert.ok(!process.permission.has('net.udp', '0:1:1:1:1:1:1:1/6666'));
      `,
    ]
  );
  if (status !== 0) {
    console.error(stderr.toString());
  }
  assert.strictEqual(status, 0);
}
