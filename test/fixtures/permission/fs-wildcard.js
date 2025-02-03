'use strict'

const common = require('../../common');

const assert = require('assert');
const fs = require('fs');

{
  assert.throws(() => {
    fs.readFileSync('/test.txt');
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
  }));
  // doesNotThrow
  fs.readFile('/tmp/foo/file', (err) => {
    // Will throw ENOENT
    assert.notEqual(err.code, 'ERR_ACCESS_DENIED');
  });
}

{
  // doesNotThrow
  fs.readFile('/example/foo/file', (err) => {
    assert.notEqual(err.code, 'ERR_ACCESS_DENIED');
  });

  fs.readFile('/example/foo2/file', (err) => {
    assert.notEqual(err.code, 'ERR_ACCESS_DENIED');
  });

  fs.readFile('/example/foo2', (err) => {
    assert.notEqual(err.code, 'ERR_ACCESS_DENIED');
  });

  assert.throws(() => {
    fs.readFileSync('/example/fo/foo2.js');
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
  }));
  assert.throws(() => {
    fs.readFileSync('/example/for');
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
  }));
}

{
  // doesNotThrow
  fs.readFile('/example/bar/file', (err) => {
    assert.notEqual(err.code, 'ERR_ACCESS_DENIED');
  });
  fs.readFile('/example/bar2/file', (err) => {
    assert.notEqual(err.code, 'ERR_ACCESS_DENIED');
  });
  fs.readFile('/example/bar', (err) => {
    assert.notEqual(err.code, 'ERR_ACCESS_DENIED');
  });

  assert.throws(() => {
    fs.readFileSync('/example/ba/foo2.js');
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
  }));
}

{
  fs.readFile('/folder/a/subfolder/b', (err) => {
    assert.notEqual(err.code, 'ERR_ACCESS_DENIED');
  });
  fs.readFile('/folder/a/subfolder/b/c.txt', (err) => {
    assert.notEqual(err.code, 'ERR_ACCESS_DENIED');
  });
  fs.readFile('/folder/a/foo2.js', (err) => {
    assert.notEqual(err.code, 'ERR_ACCESS_DENIED');
  });
}