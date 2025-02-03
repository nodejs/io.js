// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

'use strict';
const common = require('../common');
const assert = require('assert');
const cluster = require('cluster');
const net = require('net');
const { readFileSync } = require('fs');

if (common.isLinux) {
  try {
    const unprivilegedPortStart = parseInt(readFileSync('/proc/sys/net/ipv4/ip_unprivileged_port_start'));
    if (unprivilegedPortStart <= 42) {
      common.skip('Port 42 is unprivileged');
    }
  } catch {
    // Do nothing, feature doesn't exist, minimum is 1024 so 42 is usable.
    // Continue...
  }
}

// Skip on macOS Mojave. https://github.com/nodejs/node/issues/21679
if (common.isMacOS)
  common.skip('macOS may allow ordinary processes to use any port');

if (common.isIBMi)
  common.skip('IBMi may allow ordinary processes to use any port');

if (common.isWindows)
  common.skip('not reliable on Windows.');

if (process.getuid() === 0)
  common.skip('Test is not supposed to be run as root.');

if (cluster.isPrimary) {
  cluster.fork().on('exit', common.mustCall((exitCode) => {
    assert.strictEqual(exitCode, 0);
  }));
} else {
  const s = net.createServer(common.mustNotCall());
  s.listen(42, common.mustNotCall('listen should have failed'));
  s.on('error', common.mustCall((err) => {
    assert.strictEqual(err.code, 'EACCES');
    process.disconnect();
  }));
}
