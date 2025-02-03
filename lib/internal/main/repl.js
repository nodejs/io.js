'use strict';

// Create the REPL if `-i` or `--interactive` is passed, or if
// the main module is not specified and stdin is a TTY.

const {
  prepareMainThreadExecution,
  markBootstrapComplete,
} = require('internal/process/pre_execution');

const {
  evalScript,
} = require('internal/process/execution');

const console = require('internal/console/global');

const { getOptionValue } = require('internal/options');

const { exitCodes: { kInvalidCommandLineArgument } } = internalBinding('errors');

prepareMainThreadExecution();

markBootstrapComplete();

if (process.env.NODE_REPL_EXTERNAL_MODULE) {
  require('internal/modules/cjs/loader')
    .wrapModuleLoad(process.env.NODE_REPL_EXTERNAL_MODULE, undefined, true);
} else {
  // --input-type flag not supported in REPL
  if (getOptionValue('--input-type')) {
    // If we can't write to stderr, we'd like to make this a noop,
    // so use console.error.
    console.error('Cannot specify --input-type for REPL');
    process.exit(kInvalidCommandLineArgument);
  }

  require('internal/modules/run_main').runEntryPointWithESMLoader(() => {
    console.log(`Welcome to Node.js ${process.version}.\n` +
      'Type ".help" for more information.');

    const cliRepl = require('internal/repl');
    cliRepl.createInternalRepl(process.env, (err, repl) => {
      if (err) {
        throw err;
      }
      repl.on('exit', () => {
        if (repl._flushing) {
          repl.pause();
          return repl.once('flushHistory', () => {
            process.exit();
          });
        }
        process.exit();
      });
    });

    // If user passed '-e' or '--eval' along with `-i` or `--interactive`,
    // evaluate the code in the current context.
    if (getOptionValue('[has_eval_string]')) {
      evalScript('[eval]',
                 getOptionValue('--eval'),
                 getOptionValue('--inspect-brk'),
                 getOptionValue('--print'));
    }
    // The TLAs in the REPL are still run as scripts, just transformed as async
    // IIFEs for the REPL code itself to await on.
  });
}
