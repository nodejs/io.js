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
const os = require('os');
const path = require('path');
const { inspect } = require('util');

const is = {
  number: (value, key) => {
    assert(!Number.isNaN(value), `${key} should not be NaN`);
    assert.strictEqual(typeof value, 'number');
  },
  string: (value) => { assert.strictEqual(typeof value, 'string'); },
  array: (value) => { assert.ok(Array.isArray(value)); },
  object: (value) => {
    assert.strictEqual(typeof value, 'object');
    assert.notStrictEqual(value, null);
  }
};

process.env.TMPDIR = '/tmpdir';
process.env.TMP = '/tmp';
process.env.TEMP = '/temp';
if (common.isWindows) {
  assert.strictEqual(os.tmpdir(), '/temp');
  process.env.TEMP = '';
  assert.strictEqual(os.tmpdir(), '/tmp');
  process.env.TMP = '';
  const expected = `${process.env.SystemRoot || process.env.windir}\\temp`;
  assert.strictEqual(os.tmpdir(), expected);
  process.env.TEMP = '\\temp\\';
  assert.strictEqual(os.tmpdir(), '\\temp');
  process.env.TEMP = '\\tmpdir/';
  assert.strictEqual(os.tmpdir(), '\\tmpdir/');
  process.env.TEMP = '\\';
  assert.strictEqual(os.tmpdir(), '\\');
  process.env.TEMP = 'C:\\';
  assert.strictEqual(os.tmpdir(), 'C:\\');
} else {
  assert.strictEqual(os.tmpdir(), '/tmpdir');
  process.env.TMPDIR = '';
  assert.strictEqual(os.tmpdir(), '/tmp');
  process.env.TMP = '';
  assert.strictEqual(os.tmpdir(), '/temp');
  process.env.TEMP = '';
  assert.strictEqual(os.tmpdir(), '/tmp');
  process.env.TMPDIR = '/tmpdir/';
  assert.strictEqual(os.tmpdir(), '/tmpdir');
  process.env.TMPDIR = '/tmpdir\\';
  assert.strictEqual(os.tmpdir(), '/tmpdir\\');
  process.env.TMPDIR = '/';
  assert.strictEqual(os.tmpdir(), '/');
}

const endianness = os.endianness();
is.string(endianness);
assert.match(endianness, /[BL]E/);

const hostname = os.hostname();
is.string(hostname);
assert.ok(hostname.length > 0);

// IBMi process priority is different.
if (!common.isIBMi) {
  const { PRIORITY_BELOW_NORMAL, PRIORITY_LOW } = os.constants.priority;
  // Priority means niceness: higher numeric value <=> lower priority
  const LOWER_PRIORITY = os.getPriority() < PRIORITY_BELOW_NORMAL ? PRIORITY_BELOW_NORMAL : PRIORITY_LOW;
  os.setPriority(LOWER_PRIORITY);
  const priority = os.getPriority();
  is.number(priority);
  assert.strictEqual(priority, LOWER_PRIORITY);
}

// On IBMi, os.uptime() returns 'undefined'
if (!common.isIBMi) {
  const uptime = os.uptime();
  is.number(uptime);
  assert.ok(uptime > 0);
}

const cpus = os.cpus();
is.array(cpus);
assert.ok(cpus.length > 0);
for (const cpu of cpus) {
  assert.strictEqual(typeof cpu.model, 'string');
  assert.strictEqual(typeof cpu.speed, 'number');
  assert.strictEqual(typeof cpu.times.user, 'number');
  assert.strictEqual(typeof cpu.times.nice, 'number');
  assert.strictEqual(typeof cpu.times.sys, 'number');
  assert.strictEqual(typeof cpu.times.idle, 'number');
  assert.strictEqual(typeof cpu.times.irq, 'number');
}

const type = os.type();
is.string(type);
assert.ok(type.length > 0);

const release = os.release();
is.string(release);
assert.ok(release.length > 0);
// TODO: Check format on more than just AIX
if (common.isAIX)
  assert.match(release, /^\d+\.\d+$/);

const platform = os.platform();
is.string(platform);
assert.ok(platform.length > 0);

const arch = os.arch();
is.string(arch);
assert.ok(arch.length > 0);

if (!common.isSunOS) {
  // not implemented yet
  assert.ok(os.loadavg().length > 0);
  assert.ok(os.freemem() > 0);
  assert.ok(os.totalmem() > 0);
}

const interfaces = os.networkInterfaces();
switch (platform) {
  case 'linux': {
    const filter = (e) =>
      e.address === '127.0.0.1' &&
      e.netmask === '255.0.0.0';

    const actual = interfaces.lo.filter(filter);
    const expected = [{
      address: '127.0.0.1',
      netmask: '255.0.0.0',
      family: 'IPv4',
      mac: '00:00:00:00:00:00',
      internal: true,
      cidr: '127.0.0.1/8'
    }];
    assert.deepStrictEqual(actual, expected);
    break;
  }
  case 'win32': {
    const filter = (e) =>
      e.address === '127.0.0.1';

    const actual = interfaces['Loopback Pseudo-Interface 1'].filter(filter);
    const expected = [{
      address: '127.0.0.1',
      netmask: '255.0.0.0',
      family: 'IPv4',
      mac: '00:00:00:00:00:00',
      internal: true,
      cidr: '127.0.0.1/8'
    }];
    assert.deepStrictEqual(actual, expected);
    break;
  }
}
const netmaskToCIDRSuffixMap = new Map(Object.entries({
  '255.0.0.0': 8,
  '255.255.255.0': 24,
  'ffff:ffff:ffff:ffff::': 64,
  'ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff': 128
}));

Object.values(interfaces)
  .flat(Infinity)
  .map((v) => ({ v, mask: netmaskToCIDRSuffixMap.get(v.netmask) }))
  .forEach(({ v, mask }) => {
    assert.ok('cidr' in v, `"cidr" prop not found in ${inspect(v)}`);
    if (mask) {
      assert.strictEqual(v.cidr, `${v.address}/${mask}`);
    }
  });

const EOL = os.EOL;
if (common.isWindows) {
  assert.strictEqual(EOL, '\r\n');
} else {
  assert.strictEqual(EOL, '\n');
}

const home = os.homedir();
is.string(home);
assert.ok(home.includes(path.sep));

const version = os.version();
assert.strictEqual(typeof version, 'string');
assert(version);

if (common.isWindows && process.env.USERPROFILE) {
  assert.strictEqual(home, process.env.USERPROFILE);
  delete process.env.USERPROFILE;
  assert.ok(os.homedir().includes(path.sep));
  process.env.USERPROFILE = home;
} else if (!common.isWindows && process.env.HOME) {
  assert.strictEqual(home, process.env.HOME);
  delete process.env.HOME;
  assert.ok(os.homedir().includes(path.sep));
  process.env.HOME = home;
}

const pwd = os.userInfo();
is.object(pwd);
const pwdBuf = os.userInfo({ encoding: 'buffer' });

if (common.isWindows) {
  assert.strictEqual(pwd.uid, -1);
  assert.strictEqual(pwd.gid, -1);
  assert.strictEqual(pwd.shell, null);
  assert.strictEqual(pwdBuf.uid, -1);
  assert.strictEqual(pwdBuf.gid, -1);
  assert.strictEqual(pwdBuf.shell, null);
} else {
  is.number(pwd.uid);
  is.number(pwd.gid);
  assert.strictEqual(typeof pwd.shell, 'string');
  // It's possible for /etc/passwd to leave the user's shell blank.
  if (pwd.shell.length > 0) {
    assert(pwd.shell.includes(path.sep));
  }
  assert.strictEqual(pwd.uid, pwdBuf.uid);
  assert.strictEqual(pwd.gid, pwdBuf.gid);
  assert.strictEqual(pwd.shell, pwdBuf.shell.toString('utf8'));
}

is.string(pwd.username);
assert.ok(pwd.homedir.includes(path.sep));
assert.strictEqual(pwd.username, pwdBuf.username.toString('utf8'));
assert.strictEqual(pwd.homedir, pwdBuf.homedir.toString('utf8'));

assert.strictEqual(`${os.hostname}`, os.hostname());
assert.strictEqual(`${os.homedir}`, os.homedir());
assert.strictEqual(`${os.release}`, os.release());
assert.strictEqual(`${os.type}`, os.type());
assert.strictEqual(`${os.endianness}`, os.endianness());
assert.strictEqual(`${os.tmpdir}`, os.tmpdir());
assert.strictEqual(`${os.arch}`, os.arch());
assert.strictEqual(`${os.platform}`, os.platform());
assert.strictEqual(`${os.version}`, os.version());
assert.strictEqual(`${os.machine}`, os.machine());
assert.strictEqual(+os.totalmem, os.totalmem());

// Assert that the following values are coercible to numbers.
// On IBMi, os.uptime() returns 'undefined'
if (!common.isIBMi) {
  is.number(+os.uptime, 'uptime');
  is.number(os.uptime(), 'uptime');
}

is.number(+os.availableParallelism, 'availableParallelism');
is.number(os.availableParallelism(), 'availableParallelism');
is.number(+os.freemem, 'freemem');
is.number(os.freemem(), 'freemem');

const devNull = os.devNull;
if (common.isWindows) {
  assert.strictEqual(devNull, '\\\\.\\nul');
} else {
  assert.strictEqual(devNull, '/dev/null');
}

assert.ok(os.availableParallelism() > 0);
