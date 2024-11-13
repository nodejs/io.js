'use strict';

// This test makes sure that when throwing an error from a domain, and then
// handling that error in an uncaughtException handler by throwing an error
// again, the exit code, signal and error messages are the ones we expect with
// and without using --abort-on-uncaught-exception.

const common = require('../common');
const assert = require('assert');
const child_process = require('child_process');
const domain = require('domain');

const uncaughtExceptionHandlerErrMsg = 'boom from uncaughtException handler';
const domainErrMsg = 'boom from domain';

const RAN_UNCAUGHT_EXCEPTION_HANDLER_EXIT_CODE = 42;

if (process.argv[2] === 'child') {
  process.on('uncaughtException', common.mustCall(function onUncaught() {
    if (process.execArgv.includes('--abort-on-uncaught-exception')) {
      // When passing --abort-on-uncaught-exception to the child process,
      // we want to make sure that this handler (the process' uncaughtException
      // event handler) wasn't called. Unfortunately we can't parse the child
      // process' output to do that, since on Windows the standard error output
      // is not properly flushed in V8's Isolate::Throw right before the
      // process aborts due to an uncaught exception, and thus the error
      // message representing the error that was thrown cannot be read by the
      // parent process. So instead of parsing the child process' standard
      // error, the parent process will check that in the case
      // --abort-on-uncaught-exception was passed, the process did not exit
      // with exit code RAN_UNCAUGHT_EXCEPTION_HANDLER_EXIT_CODE.
      process.exit(RAN_UNCAUGHT_EXCEPTION_HANDLER_EXIT_CODE);
    } else {
      // On the other hand, when not passing --abort-on-uncaught-exception to
      // the node process, we want to throw in this event handler to make sure
      // that the proper error message, exit code and signal are the ones we
      // expect.
      throw new Error(uncaughtExceptionHandlerErrMsg);
    }
  }));

  const d = domain.create();
  d.run(common.mustCall(function() {
    throw new Error(domainErrMsg);
  }));
} else {
  runTestWithoutAbortOnUncaughtException();
  runTestWithAbortOnUncaughtException();
}

function runTestWithoutAbortOnUncaughtException() {
  child_process.exec(
    ...createTestCmdLine(),
    function onTestDone(err, stdout, stderr) {
      // When _not_ passing --abort-on-uncaught-exception, the process'
      // uncaughtException handler _must_ be called, and thus the error
      // message must include only the message of the error thrown from the
      // process' uncaughtException handler.
      assert(stderr.includes(uncaughtExceptionHandlerErrMsg),
             'stderr output must include proper uncaughtException ' +
             'handler\'s error\'s message');
      assert(!stderr.includes(domainErrMsg),
             'stderr output must not include domain\'s error\'s message');

      assert.notStrictEqual(err.code, 0,
                            'child process should have exited with a ' +
                            'non-zero exit code, but did not');
    }
  );
}

function runTestWithAbortOnUncaughtException() {
  child_process.exec(...createTestCmdLine({
    withAbortOnUncaughtException: true
  }), function onTestDone(err, stdout, stderr) {
    assert.notStrictEqual(err.code, RAN_UNCAUGHT_EXCEPTION_HANDLER_EXIT_CODE,
                          'child process should not have run its ' +
                          'uncaughtException event handler');
    assert(common.nodeProcessAborted(err.code, err.signal),
           'process should have aborted, but did not');
  });
}

function createTestCmdLine(options) {
  const escapedArgs = common.escapePOSIXShell`"${process.execPath}" ${
    options?.withAbortOnUncaughtException ? '--abort-on-uncaught-exception' : ''
  } "${__filename}" child`;

  if (!common.isWindows) {
    // Do not create core files, as it can take a lot of disk space on
    // continuous testing and developers' machines
    escapedArgs[0] = 'ulimit -c 0 && ' + escapedArgs[0];
  }
  return escapedArgs;
}
