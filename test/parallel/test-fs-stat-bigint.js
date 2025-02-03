'use strict';

const common = require('../common');
const assert = require('assert');
const fs = require('fs');
const promiseFs = require('fs').promises;
const tmpdir = require('../common/tmpdir');
const { isDate } = require('util').types;
const { inspect } = require('util');

tmpdir.refresh();

let testIndex = 0;

function getFilename() {
  const filename = tmpdir.resolve(`test-file-${++testIndex}`);
  fs.writeFileSync(filename, 'test');
  return filename;
}

function verifyStats(bigintStats, numStats, allowableDelta) {
  // allowableDelta: It's possible that the file stats are updated between the
  // two stat() calls so allow for a small difference.
  for (const key of Object.keys(numStats)) {
    const val = numStats[key];
    if (isDate(val)) {
      const time = val.getTime();
      const time2 = bigintStats[key].getTime();
      assert(
        time - time2 <= allowableDelta,
        `difference of ${key}.getTime() should <= ${allowableDelta}.\n` +
        `Number version ${time}, BigInt version ${time2}n`);
    } else if (key === 'mode') {
      assert.strictEqual(bigintStats[key], BigInt(val));
      assert.strictEqual(
        bigintStats.isBlockDevice(),
        numStats.isBlockDevice()
      );
      assert.strictEqual(
        bigintStats.isCharacterDevice(),
        numStats.isCharacterDevice()
      );
      assert.strictEqual(
        bigintStats.isDirectory(),
        numStats.isDirectory()
      );
      assert.strictEqual(
        bigintStats.isFIFO(),
        numStats.isFIFO()
      );
      assert.strictEqual(
        bigintStats.isFile(),
        numStats.isFile()
      );
      assert.strictEqual(
        bigintStats.isSocket(),
        numStats.isSocket()
      );
      assert.strictEqual(
        bigintStats.isSymbolicLink(),
        numStats.isSymbolicLink()
      );
    } else if (key.endsWith('Ms')) {
      const nsKey = key.replace('Ms', 'Ns');
      const msFromBigInt = bigintStats[key];
      const nsFromBigInt = bigintStats[nsKey];
      const msFromBigIntNs = Number(nsFromBigInt / (10n ** 6n));
      const msFromNum = numStats[key];

      assert(
        msFromNum - Number(msFromBigInt) <= allowableDelta,
        `Number version ${key} = ${msFromNum}, ` +
        `BigInt version ${key} = ${msFromBigInt}n, ` +
        `Allowable delta = ${allowableDelta}`);

      assert(
        msFromNum - Number(msFromBigIntNs) <= allowableDelta,
        `Number version ${key} = ${msFromNum}, ` +
        `BigInt version ${nsKey} = ${nsFromBigInt}n` +
        ` = ${msFromBigIntNs}ms, Allowable delta = ${allowableDelta}`);
    } else if (Number.isSafeInteger(val)) {
      assert.strictEqual(
        bigintStats[key], BigInt(val),
        `${inspect(bigintStats[key])} !== ${inspect(BigInt(val))}\n` +
        `key=${key}, val=${val}`
      );
    } else {
      assert(
        Number(bigintStats[key]) - val < 1,
        `${key} is not a safe integer, difference should < 1.\n` +
        `Number version ${val}, BigInt version ${bigintStats[key]}n`);
    }
  }
}

const runSyncTest = (func, arg) => {
  const startTime = process.hrtime.bigint();
  const bigintStats = func(arg, common.mustNotMutateObjectDeep({ bigint: true }));
  const numStats = func(arg);
  const endTime = process.hrtime.bigint();
  const allowableDelta = Math.ceil(Number(endTime - startTime) / 1e6);
  verifyStats(bigintStats, numStats, allowableDelta);
};

{
  const filename = getFilename();
  runSyncTest(fs.statSync, filename);
}

if (!common.isWindows) {
  const filename = getFilename();
  const link = `${filename}-link`;
  fs.symlinkSync(filename, link);
  runSyncTest(fs.lstatSync, link);
}

{
  const filename = getFilename();
  const fd = fs.openSync(filename, 'r');
  runSyncTest(fs.fstatSync, fd);
  fs.closeSync(fd);
}

{
  assert.throws(
    () => fs.statSync('does_not_exist'),
    { code: 'ENOENT' });
  assert.strictEqual(
    fs.statSync('does_not_exist', common.mustNotMutateObjectDeep({ throwIfNoEntry: false })),
    undefined);
}

{
  assert.throws(
    () => fs.lstatSync('does_not_exist'),
    { code: 'ENOENT' });
  assert.strictEqual(
    fs.lstatSync('does_not_exist', common.mustNotMutateObjectDeep({ throwIfNoEntry: false })),
    undefined);
}

{
  assert.throws(
    () => fs.fstatSync(9999),
    { code: 'EBADF' });
  assert.throws(
    () => fs.fstatSync(9999, common.mustNotMutateObjectDeep({ throwIfNoEntry: false })),
    { code: 'EBADF' });
}

const runCallbackTest = (func, arg, done) => {
  const startTime = process.hrtime.bigint();
  func(arg, common.mustNotMutateObjectDeep({ bigint: true }), common.mustCall((err, bigintStats) => {
    func(arg, common.mustCall((err, numStats) => {
      const endTime = process.hrtime.bigint();
      const allowableDelta = Math.ceil(Number(endTime - startTime) / 1e6);
      verifyStats(bigintStats, numStats, allowableDelta);
      if (done) {
        done();
      }
    }));
  }));
};

{
  const filename = getFilename();
  runCallbackTest(fs.stat, filename);
}

if (!common.isWindows) {
  const filename = getFilename();
  const link = `${filename}-link`;
  fs.symlinkSync(filename, link);
  runCallbackTest(fs.lstat, link);
}

{
  const filename = getFilename();
  const fd = fs.openSync(filename, 'r');
  runCallbackTest(fs.fstat, fd, () => { fs.closeSync(fd); });
}

const runPromiseTest = async (func, arg) => {
  const startTime = process.hrtime.bigint();
  const bigintStats = await func(arg, common.mustNotMutateObjectDeep({ bigint: true }));
  const numStats = await func(arg);
  const endTime = process.hrtime.bigint();
  const allowableDelta = Math.ceil(Number(endTime - startTime) / 1e6);
  verifyStats(bigintStats, numStats, allowableDelta);
};

{
  const filename = getFilename();
  runPromiseTest(promiseFs.stat, filename);
}

if (!common.isWindows) {
  const filename = getFilename();
  const link = `${filename}-link`;
  fs.symlinkSync(filename, link);
  runPromiseTest(promiseFs.lstat, link);
}

(async function() {
  const filename = getFilename();
  const handle = await promiseFs.open(filename, 'r');
  const startTime = process.hrtime.bigint();
  const bigintStats = await handle.stat(common.mustNotMutateObjectDeep({ bigint: true }));
  const numStats = await handle.stat();
  const endTime = process.hrtime.bigint();
  const allowableDelta = Math.ceil(Number(endTime - startTime) / 1e6);
  verifyStats(bigintStats, numStats, allowableDelta);
  await handle.close();
})().then(common.mustCall());

{
  // These two tests have an equivalent in ./test-fs-stat.js

  // BigIntStats Date properties can be set before reading them
  fs.stat(__filename, { bigint: true }, common.mustSucceed((s) => {
    s.atime = 2;
    s.mtime = 3;
    s.ctime = 4;
    s.birthtime = 5;

    assert.strictEqual(s.atime, 2);
    assert.strictEqual(s.mtime, 3);
    assert.strictEqual(s.ctime, 4);
    assert.strictEqual(s.birthtime, 5);
  }));

  // BigIntStats Date properties can be set after reading them
  fs.stat(__filename, { bigint: true }, common.mustSucceed((s) => {
    // eslint-disable-next-line no-unused-expressions
    s.atime, s.mtime, s.ctime, s.birthtime;

    s.atime = 2;
    s.mtime = 3;
    s.ctime = 4;
    s.birthtime = 5;

    assert.strictEqual(s.atime, 2);
    assert.strictEqual(s.mtime, 3);
    assert.strictEqual(s.ctime, 4);
    assert.strictEqual(s.birthtime, 5);
  }));
}
