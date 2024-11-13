'use strict';

const {
  ArrayPrototypePush,
  MathMin,
  ReflectApply,
} = primordials;

const {
  constants: {
    kReadFileBufferLength,
    kReadFileUnknownBufferLength,
  },
} = require('internal/fs/utils');

const { Buffer } = require('buffer');

const { FSReqCallback, close, read } = internalBinding('fs');

const {
  AbortError,
  aggregateTwoErrors,
} = require('internal/errors');

function readFileAfterRead(err, bytesRead) {
  const context = this.context;

  if (err)
    return context.close(err);

  context.pos += bytesRead;

  if (context.pos === context.size || bytesRead === 0) {
    context.close();
  } else {
    if (context.size === 0) {
      // Unknown size, just read until we don't get bytes.
      const buffer = bytesRead === kReadFileUnknownBufferLength ?
        context.buffer : context.buffer.slice(0, bytesRead);
      ArrayPrototypePush(context.buffers, buffer);
    }
    context.read();
  }
}

function readFileAfterClose(err) {
  const context = this.context;
  const callback = context.callback;
  let buffer = null;

  if (context.err || err)
    return callback(aggregateTwoErrors(err, context.err));

  try {
    if (context.size === 0)
      buffer = Buffer.concat(context.buffers, context.pos);
    else if (context.pos < context.size)
      buffer = context.buffer.slice(0, context.pos);
    else
      buffer = context.buffer;

    if (context.encoding)
      buffer = buffer.toString(context.encoding);
  } catch (err) {
    return callback(err);
  }

  callback(null, buffer);
}

class ReadFileContext {
  constructor(callback, encoding) {
    this.fd = undefined;
    this.isUserFd = undefined;
    this.size = 0;
    this.callback = callback;
    this.buffers = null;
    this.buffer = null;
    this.pos = 0;
    this.encoding = encoding;
    this.err = null;
    this.signal = undefined;
  }

  read() {
    let buffer;
    let offset;
    let length;

    if (this.signal?.aborted) {
      return this.close(
        new AbortError(undefined, { cause: this.signal.reason }));
    }
    if (this.size === 0) {
      buffer = Buffer.allocUnsafeSlow(kReadFileUnknownBufferLength);
      offset = 0;
      length = kReadFileUnknownBufferLength;
      this.buffer = buffer;
    } else {
      buffer = this.buffer;
      offset = this.pos;
      length = MathMin(kReadFileBufferLength, this.size - this.pos);
    }

    const req = new FSReqCallback();
    req.oncomplete = readFileAfterRead;
    req.context = this;

    read(this.fd, buffer, offset, length, -1, req);
  }

  close(err) {
    if (this.isUserFd) {
      process.nextTick(function tick(context) {
        ReflectApply(readFileAfterClose, { context }, [null]);
      }, this);
      return;
    }

    const req = new FSReqCallback();
    req.oncomplete = readFileAfterClose;
    req.context = this;
    this.err = err;

    close(this.fd, req);
  }
}

module.exports = ReadFileContext;
