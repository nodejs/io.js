'use strict';

const {
  ObjectDefineProperties,
  PromiseResolve,
  PromiseWithResolvers,
  ReflectConstruct,
} = primordials;

const {
  kState,
  setPromiseHandled,
} = require('internal/webstreams/util');

const {
  DOMException,
} = internalBinding('messaging');

const {
  ReadableStream,
  readableStreamDefaultControllerEnqueue,
  readableStreamDefaultControllerClose,
  readableStreamDefaultControllerError,
  readableStreamPipeTo,
} = require('internal/webstreams/readablestream');

const {
  WritableStream,
  writableStreamDefaultControllerErrorIfNeeded,
} = require('internal/webstreams/writablestream');

const assert = require('internal/assert');

const {
  markTransferMode,
  kClone,
  kDeserialize,
} = require('internal/worker/js_transferable');

// This class is a bit of a hack. The Node.js implementation of
// DOMException is not transferable/cloneable. This provides us
// with a variant that is. Unfortunately, it means playing around
// a bit with the message, name, and code properties and the
// prototype. We can revisit this if DOMException is ever made
// properly cloneable.
class CloneableDOMException extends DOMException {
  constructor(message, name) {
    super(message, name);
    markTransferMode(this, true, false);
    this[kDeserialize]({
      message: this.message,
      name: this.name,
      code: this.code,
    });
  }

  [kClone]() {
    return {
      data: {
        message: this.message,
        name: this.name,
        code: this.code,
      },
      deserializeInfo:
        'internal/webstreams/transfer:InternalCloneableDOMException',
    };
  }

  [kDeserialize]({ message, name, code }) {
    ObjectDefineProperties(this, {
      message: {
        __proto__: null,
        configurable: true,
        enumerable: true,
        get() { return message; },
      },
      name: {
        __proto__: null,
        configurable: true,
        enumerable: true,
        get() { return name; },
      },
      code: {
        __proto__: null,
        configurable: true,
        enumerable: true,
        get() { return code; },
      },
    });
  }
}

function InternalCloneableDOMException() {
  return ReflectConstruct(
    CloneableDOMException,
    [],
    DOMException);
}
InternalCloneableDOMException[kDeserialize] = () => {};

class CrossRealmTransformReadableSource {
  constructor(port, unref) {
    this[kState] = {
      port,
      controller: undefined,
      unref,
    };

    port.onmessage = ({ data }) => {
      const {
        controller,
      } = this[kState];
      const {
        type,
        value,
      } = data;
      switch (type) {
        case 'chunk':
          readableStreamDefaultControllerEnqueue(
            controller,
            value);
          break;
        case 'close':
          readableStreamDefaultControllerClose(controller);
          port.close();
          break;
        case 'error':
          readableStreamDefaultControllerError(controller, value);
          port.close();
          break;
      }
    };

    port.onmessageerror = () => {
      const error = new CloneableDOMException(
        'Internal transferred ReadableStream error',
        'DataCloneError');
      port.postMessage({ type: 'error', value: error });
      readableStreamDefaultControllerError(
        this[kState].controller,
        error);
      port.close();
    };

    port.unref();
  }

  start(controller) {
    this[kState].controller = controller;
  }

  async pull() {
    if (this[kState].unref) {
      this[kState].unref = false;
      this[kState].port.ref();
    }
    this[kState].port.postMessage({ type: 'pull' });
  }

  async cancel(reason) {
    try {
      this[kState].port.postMessage({ type: 'error', value: reason });
    } catch (error) {
      if (error instanceof DOMException) {
        // eslint-disable-next-line no-ex-assign
        error = new CloneableDOMException(error.message, error.name);
      }
      this[kState].port.postMessage({ type: 'error', value: error });
      throw error;
    } finally {
      this[kState].port.close();
    }
  }
}

class CrossRealmTransformWritableSink {
  constructor(port, unref) {
    this[kState] = {
      port,
      controller: undefined,
      backpressurePromise: PromiseWithResolvers(),
      unref,
    };

    port.onmessage = ({ data }) => {
      assert(typeof data === 'object');
      const {
        type,
        value,
      } = { ...data };
      assert(typeof type === 'string');
      switch (type) {
        case 'pull':
          if (this[kState].backpressurePromise !== undefined)
            this[kState].backpressurePromise.resolve?.();
          this[kState].backpressurePromise = undefined;
          break;
        case 'error':
          writableStreamDefaultControllerErrorIfNeeded(
            this[kState].controller,
            value);
          if (this[kState].backpressurePromise !== undefined)
            this[kState].backpressurePromise.resolve?.();
          this[kState].backpressurePromise = undefined;
          break;
      }
    };
    port.onmessageerror = () => {
      const error = new CloneableDOMException(
        'Internal transferred ReadableStream error',
        'DataCloneError');
      port.postMessage({ type: 'error', value: error });
      writableStreamDefaultControllerErrorIfNeeded(
        this[kState].controller,
        error);
      port.close();
    };

    port.unref();
  }

  start(controller) {
    this[kState].controller = controller;
  }

  async write(chunk) {
    if (this[kState].unref) {
      this[kState].unref = false;
      this[kState].port.ref();
    }
    if (this[kState].backpressurePromise === undefined) {
      this[kState].backpressurePromise = {
        promise: PromiseResolve(),
        resolve: undefined,
        reject: undefined,
      };
    }
    await this[kState].backpressurePromise.promise;
    this[kState].backpressurePromise = PromiseWithResolvers();
    try {
      this[kState].port.postMessage({ type: 'chunk', value: chunk });
    } catch (error) {
      if (error instanceof DOMException) {
        // eslint-disable-next-line no-ex-assign
        error = new CloneableDOMException(error.message, error.name);
      }
      this[kState].port.postMessage({ type: 'error', value: error });
      this[kState].port.close();
      throw error;
    }
  }

  close() {
    this[kState].port.postMessage({ type: 'close' });
    this[kState].port.close();
  }

  abort(reason) {
    try {
      this[kState].port.postMessage({ type: 'error', value: reason });
    } catch (error) {
      if (error instanceof DOMException) {
        // eslint-disable-next-line no-ex-assign
        error = new CloneableDOMException(error.message, error.name);
      }
      this[kState].port.postMessage({ type: 'error', value: error });
      throw error;
    } finally {
      this[kState].port.close();
    }
  }
}

function newCrossRealmReadableStream(writable, port) {
  // MessagePort should always be unref.
  // There is a problem with the process not terminating.
  // https://github.com/nodejs/node/issues/44985
  const readable = new ReadableStream(new CrossRealmTransformReadableSource(port, false));

  const promise = readableStreamPipeTo(readable, writable, false, false, false);

  setPromiseHandled(promise);

  return {
    readable,
    promise,
  };
}

function newCrossRealmWritableSink(readable, port) {
  // MessagePort should always be unref.
  // There is a problem with the process not terminating.
  // https://github.com/nodejs/node/issues/44985
  const writable = new WritableStream(new CrossRealmTransformWritableSink(port, false));

  const promise = readableStreamPipeTo(readable, writable, false, false, false);

  setPromiseHandled(promise);

  return {
    writable,
    promise,
  };
}

module.exports = {
  newCrossRealmReadableStream,
  newCrossRealmWritableSink,
  CrossRealmTransformWritableSink,
  CrossRealmTransformReadableSource,
  CloneableDOMException,
  InternalCloneableDOMException,
};
