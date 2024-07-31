'use strict'

const common = require('../../common');

const assert = require('assert');
const fs = require('fs');
const path = require('path');

const blockedFolder = process.env.BLOCKEDFOLDER;
const blockedFile = process.env.BLOCKEDFILE;
const regularFile = __filename;
const symlinkFromBlockedFile = process.env.EXISTINGSYMLINK;

{
  assert.ok(!process.permission.has('fs.read', blockedFile))
  assert.ok(!process.permission.has('fs.read', blockedFolder))
  assert.ok(!process.permission.has('fs.write', blockedFile))
  assert.ok(!process.permission.has('fs.write', blockedFolder))
}

{
  // Previously created symlink are NOT affected by the permission model
  const linkData = fs.readlinkSync(symlinkFromBlockedFile);
  assert.ok(linkData);
  const fileData = fs.readFileSync(symlinkFromBlockedFile);
  assert.ok(fileData);
  // cleanup
  fs.unlink(symlinkFromBlockedFile, (err) => {
    assert.ifError(
      err,
      `Error while removing the symlink: ${symlinkFromBlockedFile}.
      You may need to remove it manually to re-run the tests`
    );
  });
}

{
  // App doesn’t have access to the BLOCKFOLDER
  assert.throws(() => {
    fs.opendirSync(blockedFolder);
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
  }));
  assert.throws(() => {
    fs.writeFileSync(blockedFolder + '/new-file', 'data');
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  }));

  // App doesn’t have access to the BLOCKEDFILE folder
  assert.throws(() => {
    fs.readFileSync(blockedFile);
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
  }));
  assert.throws(() => {
    fs.appendFileSync(blockedFile, 'data');
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  }));

  // App won't be able to symlink REGULARFILE to BLOCKFOLDER/asdf
  assert.throws(() => {
    fs.symlinkSync(regularFile, blockedFolder + '/asdf', 'file');
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  }));
  assert.throws(() => {
    fs.linkSync(regularFile, blockedFolder + '/asdf');
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  }));

  // App won't be able to symlink BLOCKEDFILE to REGULARDIR
  assert.throws(() => {
    fs.symlinkSync(blockedFile, path.join(__dirname, '/asdf'), 'file');
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
  }));
  assert.throws(() => {
    fs.linkSync(blockedFile, path.join(__dirname, '/asdf'));
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
  }));
}