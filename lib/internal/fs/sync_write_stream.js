'use strict';

const {
  ObjectSetPrototypeOf,
  ReflectApply,
} = primordials;
const { kEmptyObject } = require('internal/util');

const { Writable } = require('stream');
const { closeSync, writeSync } = require('fs');

function SyncWriteStream(fd, options) {
  ReflectApply(Writable, this, [{ autoDestroy: true }]);

  options ||= kEmptyObject;

  this.fd = fd;
  this.readable = false;
  this.autoClose = options.autoClose === undefined ? true : options.autoClose;
}

ObjectSetPrototypeOf(SyncWriteStream.prototype, Writable.prototype);
ObjectSetPrototypeOf(SyncWriteStream, Writable);

SyncWriteStream.prototype._write = function(chunk, encoding, cb) {
  try {
    writeSync(this.fd, chunk, 0, chunk.length);
  } catch (e) {
    cb(e);
    return;
  }
  cb();
};

SyncWriteStream.prototype._destroy = function(err, cb) {
  if (this.fd === null) // already destroy()ed
    return cb(err);

  if (this.autoClose)
    closeSync(this.fd);

  this.fd = null;
  cb(err);
};

SyncWriteStream.prototype.destroySoon =
  SyncWriteStream.prototype.destroy;

module.exports = SyncWriteStream;
