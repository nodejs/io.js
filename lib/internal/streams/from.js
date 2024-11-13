'use strict';

const {
  PromisePrototypeThen,
  SymbolAsyncIterator,
  SymbolIterator,
} = primordials;
const { Buffer } = require('buffer');

const {
  ERR_INVALID_ARG_TYPE,
  ERR_STREAM_NULL_VALUES,
} = require('internal/errors').codes;

function from(Readable, iterable, opts) {
  let iterator;
  if (typeof iterable === 'string' || iterable instanceof Buffer) {
    return new Readable({
      objectMode: true,
      ...opts,
      read() {
        this.push(iterable);
        this.push(null);
      },
    });
  }

  let isAsync;
  if (iterable?.[SymbolAsyncIterator]) {
    isAsync = true;
    iterator = iterable[SymbolAsyncIterator]();
  } else if (iterable?.[SymbolIterator]) {
    isAsync = false;
    iterator = iterable[SymbolIterator]();
  } else {
    throw new ERR_INVALID_ARG_TYPE('iterable', ['Iterable'], iterable);
  }


  const readable = new Readable({
    objectMode: true,
    highWaterMark: 1,
    // TODO(ronag): What options should be allowed?
    ...opts,
  });

  // Flag to protect against _read
  // being called before last iteration completion.
  let reading = false;
  let isAsyncValues = false;

  readable._read = function() {
    if (!reading) {
      reading = true;

      if (isAsync) {
        nextAsync();
      } else if (isAsyncValues) {
        nextSyncWithAsyncValues();
      } else {
        nextSyncWithSyncValues();
      }
    }
  };

  readable._destroy = function(error, cb) {
    PromisePrototypeThen(
      close(error),
      () => process.nextTick(cb, error), // nextTick is here in case cb throws
      (e) => process.nextTick(cb, e || error),
    );
  };

  async function close(error) {
    const hadError = (error !== undefined) && (error !== null);
    const hasThrow = typeof iterator.throw === 'function';
    if (hadError && hasThrow) {
      const { value, done } = await iterator.throw(error);
      await value;
      if (done) {
        return;
      }
    }
    if (typeof iterator.return === 'function') {
      const { value } = await iterator.return();
      await value;
    }
  }

  // There are a lot of duplication here, it's done on purpose for performance
  // reasons - avoid await when not needed.

  function nextSyncWithSyncValues() {
    for (;;) {
      try {
        const { value, done } = iterator.next();

        if (done) {
          readable.push(null);
          return;
        }

        if (value &&
          typeof value.then === 'function') {
          return changeToAsyncValues(value);
        }

        if (value === null) {
          reading = false;
          throw new ERR_STREAM_NULL_VALUES();
        }

        if (readable.push(value)) {
          continue;
        }

        reading = false;
      } catch (err) {
        readable.destroy(err);
      }
      break;
    }
  }

  async function changeToAsyncValues(value) {
    isAsyncValues = true;

    try {
      const res = await value;

      if (res === null) {
        reading = false;
        throw new ERR_STREAM_NULL_VALUES();
      }

      if (readable.push(res)) {
        nextSyncWithAsyncValues();
        return;
      }

      reading = false;
    } catch (err) {
      readable.destroy(err);
    }
  }

  async function nextSyncWithAsyncValues() {
    for (;;) {
      try {
        const { value, done } = iterator.next();

        if (done) {
          readable.push(null);
          return;
        }

        const res = (value &&
          typeof value.then === 'function') ?
          await value :
          value;

        if (res === null) {
          reading = false;
          throw new ERR_STREAM_NULL_VALUES();
        }

        if (readable.push(res)) {
          continue;
        }

        reading = false;
      } catch (err) {
        readable.destroy(err);
      }
      break;
    }
  }

  async function nextAsync() {
    for (;;) {
      try {
        const { value, done } = await iterator.next();

        if (done) {
          readable.push(null);
          return;
        }

        if (value === null) {
          reading = false;
          throw new ERR_STREAM_NULL_VALUES();
        }

        if (readable.push(value)) {
          continue;
        }

        reading = false;
      } catch (err) {
        readable.destroy(err);
      }
      break;
    }
  }
  return readable;
}

module.exports = from;
