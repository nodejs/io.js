'use strict';

const common = require('../common');
const fixtures = require('../common/fixtures');
const stream = require('stream');
const REPL = require('repl');
const assert = require('assert');
const fs = require('fs');
const os = require('os');
const util = require('util');

if (process.env.TERM === 'dumb') {
  common.skip('skipping - dumb terminal');
}

const tmpdir = require('../common/tmpdir');
tmpdir.refresh();

// Mock os.homedir()
os.homedir = function() {
  return tmpdir.path;
};

// Create an input stream specialized for testing an array of actions
class ActionStream extends stream.Stream {
  run(data) {
    const _iter = data[Symbol.iterator]();
    const doAction = () => {
      const next = _iter.next();
      if (next.done) {
        // Close the repl. Note that it must have a clean prompt to do so.
        setImmediate(() => {
          this.emit('keypress', '', { ctrl: true, name: 'd' });
        });
        return;
      }
      const action = next.value;

      if (typeof action === 'object') {
        this.emit('keypress', '', action);
      } else {
        this.emit('data', `${action}\n`);
      }
      setImmediate(doAction);
    };
    setImmediate(doAction);
  }
  resume() {}
  pause() {}
}
ActionStream.prototype.readable = true;


// Mock keys
const UP = { name: 'up' };
const DOWN = { name: 'down' };
const ENTER = { name: 'enter' };
const CLEAR = { ctrl: true, name: 'u' };

// File paths
const historyFixturePath = fixtures.path('.node_repl_history');
const historyPath = tmpdir.resolve('.fixture_copy_repl_history');
const historyPathFail = fixtures.path('nonexistent_folder', 'filename');
const defaultHistoryPath = tmpdir.resolve('.node_repl_history');
const emptyHiddenHistoryPath = fixtures.path('.empty-hidden-repl-history-file');
const devNullHistoryPath = tmpdir.resolve('.dev-null-repl-history-file');
// Common message bits
const prompt = '> ';
const replDisabled = '\nPersistent history support disabled. Set the ' +
                     'NODE_REPL_HISTORY environment\nvariable to a valid, ' +
                     'user-writable path to enable.\n';
const homedirErr = '\nError: Could not get the home directory.\n' +
                   'REPL session history will not be persisted.\n';
const replFailedRead = '\nError: Could not open history file.\n' +
                       'REPL session history will not be persisted.\n';

const tests = [
  {
    env: { NODE_REPL_HISTORY: '' },
    test: [UP],
    expected: [prompt, replDisabled, prompt]
  },
  {
    env: { NODE_REPL_HISTORY: ' ' },
    test: [UP],
    expected: [prompt, replDisabled, prompt]
  },
  {
    env: { NODE_REPL_HISTORY: historyPath },
    test: [UP, CLEAR],
    expected: [prompt, `${prompt}'you look fabulous today'`, prompt]
  },
  {
    env: {},
    test: [UP, '21', ENTER, "'42'", ENTER],
    expected: [
      prompt,
      // TODO(BridgeAR): The line is refreshed too many times. The double prompt
      // is redundant and can be optimized away.
      '2', '1', '21\n', prompt, prompt,
      "'", '4', '2', "'", "'42'\n", prompt, prompt,
    ],
    clean: false
  },
  { // Requires the above test case
    env: {},
    test: [UP, UP, UP, DOWN, ENTER],
    expected: [
      prompt,
      `${prompt}'42'`,
      `${prompt}21`,
      prompt,
      `${prompt}21`,
      '21\n',
      prompt,
    ]
  },
  {
    env: { NODE_REPL_HISTORY: historyPath,
           NODE_REPL_HISTORY_SIZE: 1 },
    test: [UP, UP, DOWN, CLEAR],
    expected: [
      prompt,
      `${prompt}'you look fabulous today'`,
      prompt,
      `${prompt}'you look fabulous today'`,
      prompt,
    ]
  },
  {
    env: { NODE_REPL_HISTORY: historyPathFail,
           NODE_REPL_HISTORY_SIZE: 1 },
    test: [UP],
    expected: [prompt, replFailedRead, prompt, replDisabled, prompt]
  },
  {
    before: function before() {
      if (common.isWindows) {
        const execSync = require('child_process').execSync;
        execSync(`ATTRIB +H "${emptyHiddenHistoryPath}"`, (err) => {
          assert.ifError(err);
        });
      }
    },
    env: { NODE_REPL_HISTORY: emptyHiddenHistoryPath },
    test: [UP],
    expected: [prompt]
  },
  {
    before: function before() {
      if (!common.isWindows)
        fs.symlinkSync('/dev/null', devNullHistoryPath);
    },
    env: { NODE_REPL_HISTORY: devNullHistoryPath },
    test: [UP],
    expected: [prompt]
  },
  { // Make sure this is always the last test, since we change os.homedir()
    before: function before() {
      // Mock os.homedir() failure
      os.homedir = function() {
        throw new Error('os.homedir() failure');
      };
    },
    env: {},
    test: [UP],
    expected: [prompt, homedirErr, prompt, replDisabled, prompt]
  },
];
const numtests = tests.length;


function cleanupTmpFile() {
  try {
    // Write over the file, clearing any history
    fs.writeFileSync(defaultHistoryPath, '');
  } catch (err) {
    if (err.code === 'ENOENT') return true;
    throw err;
  }
  return true;
}

// Copy our fixture to the tmp directory
fs.createReadStream(historyFixturePath)
  .pipe(fs.createWriteStream(historyPath)).on('unpipe', () => runTest());

const runTestWrap = common.mustCall(runTest, numtests);

function runTest(assertCleaned) {
  const opts = tests.shift();
  if (!opts) return; // All done

  console.log('NEW');

  if (assertCleaned) {
    try {
      assert.strictEqual(fs.readFileSync(defaultHistoryPath, 'utf8'), '');
    } catch (e) {
      if (e.code !== 'ENOENT') {
        console.error(`Failed test # ${numtests - tests.length}`);
        throw e;
      }
    }
  }

  const test = opts.test;
  const expected = opts.expected;
  const clean = opts.clean;
  const before = opts.before;
  const historySize = opts.env.NODE_REPL_HISTORY_SIZE;
  const historyFile = opts.env.NODE_REPL_HISTORY;

  if (before) before();

  const repl = REPL.start({
    input: new ActionStream(),
    output: new stream.Writable({
      write(chunk, _, next) {
        const output = chunk.toString();
        console.log('INPUT', util.inspect(output));

        // Ignore escapes and blank lines
        if (output.charCodeAt(0) === 27 || /^[\r\n]+$/.test(output))
          return next();

        try {
          assert.strictEqual(output, expected.shift());
        } catch (err) {
          console.error(`Failed test # ${numtests - tests.length}`);
          throw err;
        }
        next();
      }
    }),
    prompt: prompt,
    useColors: false,
    terminal: true,
    historySize: historySize
  });

  repl.setupHistory(historyFile, function(err, repl) {
    if (err) {
      console.error(`Failed test # ${numtests - tests.length}`);
      throw err;
    }

    repl.once('close', () => {
      if (repl._flushing) {
        repl.once('flushHistory', onClose);
        return;
      }

      onClose();
    });

    function onClose() {
      const cleaned = clean === false ? false : cleanupTmpFile();

      try {
        // Ensure everything that we expected was output
        assert.strictEqual(expected.length, 0);
        setImmediate(runTestWrap, cleaned);
      } catch (err) {
        console.error(`Failed test # ${numtests - tests.length}`);
        throw err;
      }
    }

    repl.inputStream.run(test);
  });
}
