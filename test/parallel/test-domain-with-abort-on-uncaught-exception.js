'use strict';

const common = require('../common');
const assert = require('assert');
const fs = require('fs');

// The goal of this test is to make sure that:
//
// - Even if --abort_on_uncaught_exception is passed on the command line,
// setting up a top-level domain error handler and throwing an error
// within this domain does *not* make the process abort. The process exits
// gracefully.
//
// - When passing --abort_on_uncaught_exception on the command line and
// setting up a top-level domain error handler, an error thrown
// within this domain's error handler *does* make the process abort.
//
// - When *not* passing --abort_on_uncaught_exception on the command line and
// setting up a top-level domain error handler, an error thrown within this
// domain's error handler does *not* make the process abort, but makes it exit
// with the proper failure exit code.
//
// - When throwing an error within the top-level domain's error handler
// within a try/catch block, the process should exit gracefully, whether or
// not --abort_on_uncaught_exception is passed on the command line.

const domainErrHandlerExMessage = 'exception from domain error handler';

if (process.argv[2] === 'child') {
  const domain = require('domain');
  const d = domain.create();

  process.on('uncaughtException', function onUncaughtException() {
    // The process' uncaughtException event must not be emitted when
    // an error handler is setup on the top-level domain.
    // Exiting with exit code of 42 here so that it would assert when
    // the parent checks the child exit code.
    process.exit(42);
  });

  d.on('error', function(err) {
    // Swallowing the error on purpose if 'throwInDomainErrHandler' is not
    // set
    if (process.argv.includes('throwInDomainErrHandler')) {
      // If useTryCatch is set, wrap the throw in a try/catch block.
      // This is to make sure that a caught exception does not trigger
      // an abort.
      if (process.argv.includes('useTryCatch')) {
        try {
          throw new Error(domainErrHandlerExMessage);
        } catch {
          // Continue regardless of error.
        }
      } else {
        throw new Error(domainErrHandlerExMessage);
      }
    }
  });

  d.run(function doStuff() {
    // Throwing from within different types of callbacks as each of them
    // handles domains differently
    process.nextTick(function() {
      throw new Error('Error from nextTick callback');
    });

    fs.exists('/non/existing/file', function onExists(exists) {
      throw new Error('Error from fs.exists callback');
    });

    setImmediate(function onSetImmediate() {
      throw new Error('Error from setImmediate callback');
    });

    setTimeout(function onTimeout() {
      throw new Error('Error from setTimeout callback');
    }, 0);

    throw new Error('Error from domain.run callback');
  });
} else {
  const exec = require('child_process').exec;

  function testDomainExceptionHandling(cmdLineOption, options) {
    if (typeof cmdLineOption === 'object') {
      options = cmdLineOption;
      cmdLineOption = undefined;
    }

    let throwInDomainErrHandlerOpt;
    if (options.throwInDomainErrHandler)
      throwInDomainErrHandlerOpt = 'throwInDomainErrHandler';

    let useTryCatchOpt;
    if (options.useTryCatch)
      useTryCatchOpt = 'useTryCatch';

    const escapedArgs = common.escapePOSIXShell`"${process.execPath}" ${cmdLineOption || ''} "${__filename}" child ${throwInDomainErrHandlerOpt || ''} ${useTryCatchOpt || ''}`;
    if (!common.isWindows) {
      // Do not create core files, as it can take a lot of disk space on
      // continuous testing and developers' machines
      escapedArgs[0] = 'ulimit -c 0 && ' + escapedArgs[0];
    }
    const child = exec(...escapedArgs);

    if (child) {
      child.on('exit', function onChildExited(exitCode, signal) {
        // When throwing errors from the top-level domain error handler
        // outside of a try/catch block, the process should not exit gracefully
        if (!options.useTryCatch && options.throwInDomainErrHandler) {
          if (cmdLineOption === '--abort_on_uncaught_exception') {
            assert(common.nodeProcessAborted(exitCode, signal),
                   'process should have aborted, but did not');
          } else {
            // By default, uncaught exceptions make node exit with an exit
            // code of 7.
            assert.strictEqual(exitCode, 7);
            assert.strictEqual(signal, null);
          }
        } else {
          // If the top-level domain's error handler does not throw,
          // the process must exit gracefully, whether or not
          // --abort_on_uncaught_exception was passed on the command line
          assert.strictEqual(exitCode, 0);
          assert.strictEqual(signal, null);
        }
      });
    }
  }

  testDomainExceptionHandling('--abort_on_uncaught_exception', {
    throwInDomainErrHandler: false,
    useTryCatch: false
  });

  testDomainExceptionHandling('--abort_on_uncaught_exception', {
    throwInDomainErrHandler: false,
    useTryCatch: true
  });

  testDomainExceptionHandling('--abort_on_uncaught_exception', {
    throwInDomainErrHandler: true,
    useTryCatch: false
  });

  testDomainExceptionHandling('--abort_on_uncaught_exception', {
    throwInDomainErrHandler: true,
    useTryCatch: true
  });

  testDomainExceptionHandling({
    throwInDomainErrHandler: false
  });

  testDomainExceptionHandling({
    throwInDomainErrHandler: false,
    useTryCatch: false
  });

  testDomainExceptionHandling({
    throwInDomainErrHandler: true,
    useTryCatch: true
  });
}
