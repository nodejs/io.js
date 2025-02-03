'use strict';

const {
  ObjectDefineProperty,
} = primordials;

const rawMethods = internalBinding('process_methods');
const {
  namespace: {
    addSerializeCallback,
    isBuildingSnapshot,
  },
} = require('internal/v8/startup_snapshot');
// TODO(joyeecheung): deprecate and remove these underscore methods
process._debugProcess = rawMethods._debugProcess;
process._debugEnd = rawMethods._debugEnd;

// See the discussion in https://github.com/nodejs/node/issues/19009 and
// https://github.com/nodejs/node/pull/34010 for why these are no-ops.
// Five word summary: they were broken beyond repair.
process._startProfilerIdleNotifier = () => {};
process._stopProfilerIdleNotifier = () => {};

function defineStream(name, getter) {
  ObjectDefineProperty(process, name, {
    __proto__: null,
    configurable: true,
    enumerable: true,
    get: getter,
  });
}

defineStream('stdout', getStdout);
defineStream('stdin', getStdin);
defineStream('stderr', getStderr);

// Worker threads don't receive signals.
const {
  startListeningIfSignal,
  stopListeningIfSignal,
} = require('internal/process/signal');
process.on('newListener', startListeningIfSignal);
process.on('removeListener', stopListeningIfSignal);

// ---- keep the attachment of the wrappers above so that it's easier to ----
// ----              compare the setups side-by-side                    -----

const { guessHandleType } = require('internal/util');

function createWritableStdioStream(fd) {
  let stream;
  // Note stream._type is used for test-module-load-list.js
  switch (guessHandleType(fd)) {
    case 'TTY': {
      const tty = require('tty');
      stream = new tty.WriteStream(fd);
      stream._type = 'tty';
      break;
    }

    case 'FILE': {
      const SyncWriteStream = require('internal/fs/sync_write_stream');
      stream = new SyncWriteStream(fd, { autoClose: false });
      stream._type = 'fs';
      break;
    }

    case 'PIPE':
    case 'TCP': {
      const net = require('net');

      // If fd is already being used for the IPC channel, libuv will return
      // an error when trying to use it again. In that case, create the socket
      // using the existing handle instead of the fd.
      if (process.channel && process.channel.fd === fd) {
        const { kChannelHandle } = require('internal/child_process');
        stream = new net.Socket({
          handle: process[kChannelHandle],
          readable: false,
          writable: true,
        });
      } else {
        stream = new net.Socket({
          fd,
          readable: false,
          writable: true,
        });
      }

      stream._type = 'pipe';
      break;
    }

    default: {
      // Provide a dummy black-hole output for e.g. non-console
      // Windows applications.
      const { Writable } = require('stream');
      stream = new Writable({
        write(buf, enc, cb) {
          cb();
        },
      });
    }
  }

  // For supporting legacy API we put the FD here.
  stream.fd = fd;

  stream._isStdio = true;

  return stream;
}

function dummyDestroy(err, cb) {
  cb(err);
  this._undestroy();

  // We need to emit 'close' anyway so that the closing
  // of the stream is observable. We just make sure we
  // are not going to do it twice.
  // The 'close' event is needed so that finished and
  // pipeline work correctly.
  if (!this._writableState.emitClose) {
    process.nextTick(() => {
      this.emit('close');
    });
  }
}

let stdin;
let stdout;
let stderr;

let stdoutDestroy;
let stderrDestroy;

function refreshStdoutOnSigWinch() {
  stdout._refreshSize();
}

function refreshStderrOnSigWinch() {
  stderr._refreshSize();
}

function addCleanup(fn) {
  if (isBuildingSnapshot()) {
    addSerializeCallback(fn);
  }
}

function getStdout() {
  if (stdout) return stdout;
  stdout = createWritableStdioStream(1);
  stdout.destroySoon = stdout.destroy;
  // Override _destroy so that the fd is never actually closed.
  stdoutDestroy = stdout._destroy;
  stdout._destroy = dummyDestroy;
  if (stdout.isTTY) {
    process.on('SIGWINCH', refreshStdoutOnSigWinch);
  }

  addCleanup(function cleanupStdout() {
    stdout._destroy = stdoutDestroy;
    stdout.destroy();
    process.removeListener('SIGWINCH', refreshStdoutOnSigWinch);
    stdout = undefined;
  });
  // No need to add deserialize callback because stdout = undefined above
  // causes the stream to be lazily initialized again later.
  return stdout;
}

function getStderr() {
  if (stderr) return stderr;
  stderr = createWritableStdioStream(2);
  stderr.destroySoon = stderr.destroy;
  stderrDestroy = stderr._destroy;
  // Override _destroy so that the fd is never actually closed.
  stderr._destroy = dummyDestroy;
  if (stderr.isTTY) {
    process.on('SIGWINCH', refreshStderrOnSigWinch);
  }
  addCleanup(function cleanupStderr() {
    stderr._destroy = stderrDestroy;
    stderr.destroy();
    process.removeListener('SIGWINCH', refreshStderrOnSigWinch);
    stderr = undefined;
  });
  // No need to add deserialize callback because stderr = undefined above
  // causes the stream to be lazily initialized again later.
  return stderr;
}

function getStdin() {
  if (stdin) return stdin;
  const fd = 0;

  switch (guessHandleType(fd)) {
    case 'TTY': {
      const tty = require('tty');
      stdin = new tty.ReadStream(fd);
      break;
    }

    case 'FILE': {
      const fs = require('fs');
      stdin = new fs.ReadStream(null, { fd: fd, autoClose: false });
      break;
    }

    case 'PIPE':
    case 'TCP': {
      const net = require('net');

      // It could be that process has been started with an IPC channel
      // sitting on fd=0, in such case the pipe for this fd is already
      // present and creating a new one will lead to the assertion failure
      // in libuv.
      if (process.channel && process.channel.fd === fd) {
        stdin = new net.Socket({
          handle: process.channel,
          readable: true,
          writable: false,
          manualStart: true,
        });
      } else {
        stdin = new net.Socket({
          fd: fd,
          readable: true,
          writable: false,
          manualStart: true,
        });
      }
      // Make sure the stdin can't be `.end()`-ed
      stdin._writableState.ended = true;
      break;
    }

    default: {
      // Provide a dummy contentless input for e.g. non-console
      // Windows applications.
      const { Readable } = require('stream');
      stdin = new Readable({ read() {} });
      stdin.push(null);
    }
  }

  // For supporting legacy API we put the FD here.
  stdin.fd = fd;

  // `stdin` starts out life in a paused state, but node doesn't
  // know yet. Explicitly to readStop() it to put it in the
  // not-reading state.
  if (stdin._handle?.readStop) {
    stdin._handle.reading = false;
    stdin._readableState.reading = false;
    stdin._handle.readStop();
  }

  // If the user calls stdin.pause(), then we need to stop reading
  // once the stream implementation does so (one nextTick later),
  // so that the process can close down.
  stdin.on('pause', () => {
    process.nextTick(onpause);
  });

  function onpause() {
    if (!stdin._handle)
      return;
    if (stdin._handle.reading && !stdin.readableFlowing) {
      stdin._readableState.reading = false;
      stdin._handle.reading = false;
      stdin._handle.readStop();
    }
  }

  addCleanup(function cleanupStdin() {
    stdin.destroy();
    stdin = undefined;
  });
  // No need to add deserialize callback because stdin = undefined above
  // causes the stream to be lazily initialized again later.
  return stdin;
}

// Used by internal tests.
rawMethods.resetStdioForTesting = function() {
  stdin = undefined;
  stdout = undefined;
  stderr = undefined;
};

// Needed by the module loader and generally needed everywhere.
require('fs');
require('util');
require('url'); // eslint-disable-line no-restricted-modules
internalBinding('module_wrap');
require('internal/modules/cjs/loader');
require('internal/modules/esm/utils');

// Needed to refresh the time origin.
require('internal/perf/utils');
// Needed to register the async hooks.
if (internalBinding('config').hasInspector) {
  require('internal/inspector_async_hook');
}
// Needed to set the wasm web API callbacks.
internalBinding('wasm_web_api');
// Needed to detect whether it's on main thread.
internalBinding('worker');
// Needed by most execution modes.
require('internal/modules/run_main');
// Needed to refresh DNS configurations.
require('internal/dns/utils');
// Needed by almost all execution modes. It's fine to
// load them into the snapshot as long as we don't run
// any of the initialization.
require('internal/process/pre_execution');
