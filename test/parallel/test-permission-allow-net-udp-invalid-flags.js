'use strict';

const common = require('../common');

const { spawnSync } = require('child_process');
const assert = require('assert');

common.skipIfWorker();

const tests = [
  {
    cmd: '--allow-net-udp=localhost:99999',
    message: 'invalid port: 99999',
  },
  {
    cmd: '--allow-net-udp=localhost:xxxxx',
    message: 'invalid port: xxxxx',
  },
  {
    cmd: '--allow-net-udp=localhost/22',
    message: 'can not use netmask when IP is not an IPV4 or IPV6 address',
  },
  {
    cmd: '--allow-net-udp=127.0.0.1/33',
    message: 'invalid netmask',
  },
  {
    cmd: '--allow-net-udp=[::1]/129',
    message: 'invalid netmask',
  },
  {
    cmd: '--allow-net-udp=127.0.0.1/xxxxx',
    message: 'invalid netmask',
  },
  {
    cmd: '--allow-net-udp=[::1]/255.0.0.0',
    message: 'invalid netmask',
  },
];

for (const item of tests) {
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--experimental-permission',
      item.cmd,
      '-e',
      '',
    ]
  );
  assert.ok(status !== 0);
  assert.ok(stderr.toString().includes(item.message));
}
