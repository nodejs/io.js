'use strict';
const { escapePOSIXShell } = require('../common');

// This test checks that the maxBuffer option for child_process.spawnSync()
// works as expected.

const assert = require('assert');
const { getSystemErrorName } = require('util');
const { execSync } = require('child_process');
const msgOut = 'this is stdout';
const msgOutBuf = Buffer.from(`${msgOut}\n`);

const [cmd, opts] = escapePOSIXShell`"${process.execPath}" -e "${`console.log('${msgOut}')`}"`;

// Verify that an error is returned if maxBuffer is surpassed.
{
  assert.throws(() => {
    execSync(cmd, { ...opts, maxBuffer: 1 });
  }, (e) => {
    assert.ok(e, 'maxBuffer should error');
    assert.strictEqual(e.code, 'ENOBUFS');
    assert.strictEqual(getSystemErrorName(e.errno), 'ENOBUFS');
    // We can have buffers larger than maxBuffer because underneath we alloc 64k
    // that matches our read sizes.
    assert.deepStrictEqual(e.stdout, msgOutBuf);
    return true;
  });
}

// Verify that a maxBuffer size of Infinity works.
{
  const ret = execSync(
    cmd,
    { ...opts, maxBuffer: Infinity },
  );

  assert.deepStrictEqual(ret, msgOutBuf);
}

// Default maxBuffer size is 1024 * 1024.
{
  assert.throws(() => {
    execSync(...escapePOSIXShell`"${process.execPath}" -e "console.log('a'.repeat(1024 * 1024))"`);
  }, (e) => {
    assert.ok(e, 'maxBuffer should error');
    assert.strictEqual(e.code, 'ENOBUFS');
    assert.strictEqual(getSystemErrorName(e.errno), 'ENOBUFS');
    return true;
  });
}

// Default maxBuffer size is 1024 * 1024.
{
  const ret = execSync(...escapePOSIXShell`"${process.execPath}" -e "console.log('a'.repeat(1024 * 1024 - 1))"`);

  assert.deepStrictEqual(
    ret.toString().trim(),
    'a'.repeat(1024 * 1024 - 1)
  );
}
