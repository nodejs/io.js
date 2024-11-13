// This file is a modified version of the rimraf module on npm. It has been
// modified in the following ways:
// - Use of the assert module has been replaced with core's error system.
// - All code related to the glob dependency has been removed.
// - Bring your own custom fs module is not currently supported.
// - Some basic code cleanup.
'use strict';

const {
  Promise,
  SafeSet,
} = primordials;

const { Buffer } = require('buffer');
const fs = require('fs');
const {
  chmod,
  lstat,
  readdir,
  rmdir,
  stat,
  unlink,
} = fs;
const { sep } = require('path');
const { setTimeout } = require('timers');
const { isWindows } = require('internal/util');
const notEmptyErrorCodes = new SafeSet(['ENOTEMPTY', 'EEXIST', 'EPERM']);
const retryErrorCodes = new SafeSet(
  ['EBUSY', 'EMFILE', 'ENFILE', 'ENOTEMPTY', 'EPERM']);
const epermHandler = isWindows ? fixWinEPERM : _rmdir;
const readdirEncoding = 'buffer';
const separator = Buffer.from(sep);


function rimraf(path, options, callback) {
  let retries = 0;

  _rimraf(path, options, function CB(err) {
    if (err) {
      if (retryErrorCodes.has(err.code) && retries < options.maxRetries) {
        retries++;
        const delay = retries * options.retryDelay;
        return setTimeout(_rimraf, delay, path, options, CB);
      }

      // The file is already gone.
      if (err.code === 'ENOENT')
        err = null;
    }

    callback(err);
  });
}


function _rimraf(path, options, callback) {
  // SunOS lets the root user unlink directories. Use lstat here to make sure
  // it's not a directory.
  lstat(path, (err, stats) => {
    if (err) {
      if (err.code === 'ENOENT')
        return callback(null);

      // Windows can EPERM on stat.
      if (isWindows && err.code === 'EPERM')
        return fixWinEPERM(path, options, err, callback);
    } else if (stats.isDirectory()) {
      return _rmdir(path, options, err, callback);
    }

    unlink(path, (err) => {
      if (err) {
        if (err.code === 'ENOENT')
          return callback(null);
        if (err.code === 'EISDIR')
          return _rmdir(path, options, err, callback);
        if (err.code === 'EPERM') {
          return epermHandler(path, options, err, callback);
        }
      }

      return callback(err);
    });
  });
}


function fixWinEPERM(path, options, originalErr, callback) {
  chmod(path, 0o666, (err) => {
    if (err)
      return callback(err.code === 'ENOENT' ? null : originalErr);

    stat(path, (err, stats) => {
      if (err)
        return callback(err.code === 'ENOENT' ? null : originalErr);

      if (stats.isDirectory())
        _rmdir(path, options, originalErr, callback);
      else
        unlink(path, callback);
    });
  });
}


function _rmdir(path, options, originalErr, callback) {
  rmdir(path, (err) => {
    if (err) {
      if (notEmptyErrorCodes.has(err.code))
        return _rmchildren(path, options, callback);
      if (err.code === 'ENOTDIR')
        return callback(originalErr);
    }

    callback(err);
  });
}


function _rmchildren(path, options, callback) {
  const pathBuf = Buffer.from(path);

  readdir(pathBuf, readdirEncoding, (err, files) => {
    if (err)
      return callback(err);

    let numFiles = files.length;

    if (numFiles === 0)
      return rmdir(path, callback);

    let done = false;

    const childPathPrefix = Buffer.concat([pathBuf, separator]);

    for (let i = 0; i < files.length; i++) {
      const childPath = Buffer.concat([childPathPrefix, files[i]]);

      rimraf(childPath, options, (err) => {
        if (done)
          return;

        if (err) {
          done = true;
          return callback(err);
        }

        numFiles--;
        if (numFiles === 0)
          rmdir(path, callback);
      });
    }
  });
}


function rimrafPromises(path, options) {
  return new Promise((resolve, reject) => {
    rimraf(path, options, (err) => {
      if (err)
        return reject(err);

      resolve();
    });
  });
}


module.exports = { rimraf, rimrafPromises };
