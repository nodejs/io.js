'use strict';

const credentials = internalBinding('credentials');
const rawMethods = internalBinding('process_methods');
// TODO: this should be detached from ERR_WORKER_UNSUPPORTED_OPERATION
const { unavailable } = require('internal/process/worker_thread_only');

process.abort = unavailable('process.abort()');
process.chdir = unavailable('process.chdir()');
process.umask = wrappedUmask;
process.cwd = rawMethods.cwd;

if (credentials.implementsPosixCredentials) {
  process.initgroups = unavailable('process.initgroups()');
  process.setgroups = unavailable('process.setgroups()');
  process.setegid = unavailable('process.setegid()');
  process.seteuid = unavailable('process.seteuid()');
  process.setgid = unavailable('process.setgid()');
  process.setuid = unavailable('process.setuid()');
}

// ---- keep the attachment of the wrappers above so that it's easier to ----
// ----              compare the setups side-by-side                    -----

const {
  codes: {
    ERR_WORKER_UNSUPPORTED_OPERATION,
  },
} = require('internal/errors');

function wrappedUmask(mask) {
  // process.umask() is a read-only operation in workers.
  if (mask !== undefined) {
    throw new ERR_WORKER_UNSUPPORTED_OPERATION('Setting process.umask()');
  }

  return rawMethods.umask(mask);
}
