'use strict';

// Flags: --expose-internals

const common = require('../common');
const stream = require('stream');
const REPL = require('internal/repl');
const assert = require('assert');
const fs = require('fs');
const { inspect } = require('util');

if (process.env.TERM === 'dumb') {
  common.skip('skipping - dumb terminal');
}

common.allowGlobals('aaaa');

const tmpdir = require('../common/tmpdir');
tmpdir.refresh();

const defaultHistoryPath = tmpdir.resolve('.node_repl_history');

// Create an input stream specialized for testing an array of actions
class ActionStream extends stream.Stream {
  run(data) {
    const _iter = data[Symbol.iterator]();
    const doAction = () => {
      const next = _iter.next();
      if (next.done) {
        // Close the repl. Note that it must have a clean prompt to do so.
        this.emit('keypress', '', { ctrl: true, name: 'd' });
        return;
      }
      const action = next.value;

      if (typeof action === 'object') {
        this.emit('keypress', '', action);
      } else {
        this.emit('data', `${action}`);
      }
      setImmediate(doAction);
    };
    doAction();
  }
  resume() {}
  pause() {}
}
ActionStream.prototype.readable = true;

// Mock keys
const ENTER = { name: 'enter' };
const UP = { name: 'up' };
const DOWN = { name: 'down' };
const BACKSPACE = { name: 'backspace' };
const SEARCH_BACKWARDS = { name: 'r', ctrl: true };
const SEARCH_FORWARDS = { name: 's', ctrl: true };
const ESCAPE = { name: 'escape' };
const CTRL_C = { name: 'c', ctrl: true };
const DELETE_WORD_LEFT = { name: 'w', ctrl: true };

const prompt = '> ';

// TODO(BridgeAR): Add tests for lines that exceed the maximum columns.
const tests = [
  { // Creates few history to navigate for
    env: { NODE_REPL_HISTORY: defaultHistoryPath },
    test: [
      'console.log("foo")', ENTER,
      'ab = "aaaa"', ENTER,
      'repl.repl.historyIndex', ENTER,
      'console.log("foo")', ENTER,
      'let ba = 9', ENTER,
      'ab = "aaaa"', ENTER,
      '555 - 909', ENTER,
      '{key : {key2 :[] }}', ENTER,
      'Array(100).fill(1)', ENTER,
    ],
    expected: [],
    clean: false
  },
  {
    env: { NODE_REPL_HISTORY: defaultHistoryPath },
    showEscapeCodes: true,
    checkTotal: true,
    useColors: true,
    test: [
      '7',              // 1
      SEARCH_FORWARDS,
      SEARCH_FORWARDS,  // 3
      'a',
      SEARCH_BACKWARDS, // 5
      SEARCH_FORWARDS,
      SEARCH_BACKWARDS, // 7
      'a',
      BACKSPACE,        // 9
      DELETE_WORD_LEFT,
      'aa',             // 11
      SEARCH_BACKWARDS,
      SEARCH_BACKWARDS, // 13
      SEARCH_BACKWARDS,
      SEARCH_BACKWARDS, // 15
      SEARCH_FORWARDS,
      ESCAPE,           // 17
      ENTER,
    ],
    // A = Cursor n up
    // B = Cursor n down
    // C = Cursor n forward
    // D = Cursor n back
    // G = Cursor to column n
    // J = Erase in screen; 0 = right; 1 = left; 2 = total
    // K = Erase in line; 0 = right; 1 = left; 2 = total
    expected: [
      // 0. Start
      '\x1B[1G', '\x1B[0J',
      prompt, '\x1B[3G',
      // 1. '7'
      '7',
      // 2. SEARCH FORWARDS
      '\nfwd-i-search: _', '\x1B[1A', '\x1B[4G',
      // 3. SEARCH FORWARDS
      '\x1B[3G', '\x1B[0J',
      '7\nfwd-i-search: _', '\x1B[1A', '\x1B[4G',
      // 4. 'a'
      '\x1B[3G', '\x1B[0J',
      '7\nfailed-fwd-i-search: a_', '\x1B[1A', '\x1B[4G',
      // 5. SEARCH BACKWARDS
      '\x1B[3G', '\x1B[0J',
      'Arr\x1B[4ma\x1B[24my(100).fill(1)\nbck-i-search: a_',
      '\x1B[1A', '\x1B[6G',
      // 6. SEARCH FORWARDS
      '\x1B[3G', '\x1B[0J',
      '7\nfailed-fwd-i-search: a_', '\x1B[1A', '\x1B[4G',
      // 7. SEARCH BACKWARDS
      '\x1B[3G', '\x1B[0J',
      'Arr\x1B[4ma\x1B[24my(100).fill(1)\nbck-i-search: a_',
      '\x1B[1A', '\x1B[6G',
      // 8. 'a'
      '\x1B[3G', '\x1B[0J',
      'ab = "aa\x1B[4maa\x1B[24m"\nbck-i-search: aa_',
      '\x1B[1A', '\x1B[11G',
      // 9. BACKSPACE
      '\x1B[3G', '\x1B[0J',
      'Arr\x1B[4ma\x1B[24my(100).fill(1)\nbck-i-search: a_',
      '\x1B[1A', '\x1B[6G',
      // 10. DELETE WORD LEFT (works as backspace)
      '\x1B[3G', '\x1B[0J',
      '7\nbck-i-search: _', '\x1B[1A', '\x1B[4G',
      // 11. 'a'
      '\x1B[3G', '\x1B[0J',
      'Arr\x1B[4ma\x1B[24my(100).fill(1)\nbck-i-search: a_',
      '\x1B[1A', '\x1B[6G',
      // 11. 'aa' - continued
      '\x1B[3G', '\x1B[0J',
      'ab = "aa\x1B[4maa\x1B[24m"\nbck-i-search: aa_',
      '\x1B[1A', '\x1B[11G',
      // 12. SEARCH BACKWARDS
      '\x1B[3G', '\x1B[0J',
      'ab = "a\x1B[4maa\x1B[24ma"\nbck-i-search: aa_',
      '\x1B[1A', '\x1B[10G',
      // 13. SEARCH BACKWARDS
      '\x1B[3G', '\x1B[0J',
      'ab = "\x1B[4maa\x1B[24maa"\nbck-i-search: aa_',
      '\x1B[1A', '\x1B[9G',
      // 14. SEARCH BACKWARDS
      '\x1B[3G', '\x1B[0J',
      '7\nfailed-bck-i-search: aa_', '\x1B[1A', '\x1B[4G',
      // 15. SEARCH BACKWARDS
      '\x1B[3G', '\x1B[0J',
      '7\nfailed-bck-i-search: aa_', '\x1B[1A', '\x1B[4G',
      // 16. SEARCH FORWARDS
      '\x1B[3G', '\x1B[0J',
      'ab = "\x1B[4maa\x1B[24maa"\nfwd-i-search: aa_',
      '\x1B[1A', '\x1B[9G',
      // 17. ESCAPE
      '\x1B[3G', '\x1B[0J',
      '7',
      // 18. ENTER
      '\r\n',
      '\x1B[33m7\x1B[39m\n',
      '\x1B[1G', '\x1B[0J',
      prompt,
      '\x1B[3G',
      '\r\n',
    ],
    clean: false
  },
  {
    env: { NODE_REPL_HISTORY: defaultHistoryPath },
    showEscapeCodes: true,
    skip: !process.features.inspector,
    checkTotal: true,
    useColors: false,
    test: [
      'fu',              // 1
      SEARCH_BACKWARDS,
      '}',               // 3
      SEARCH_BACKWARDS,
      CTRL_C,            // 5
      CTRL_C,
      '1+1',             // 7
      ENTER,
      SEARCH_BACKWARDS,  // 9
      '+',
      '\r',              // 11
      '2',
      SEARCH_BACKWARDS,  // 13
      're',
      UP,                // 15
      DOWN,
      SEARCH_FORWARDS,   // 17
      '\n',
    ],
    expected: [
      '\x1B[1G', '\x1B[0J',
      prompt, '\x1B[3G',
      'f', 'u', '\nbck-i-search: _', '\x1B[1A', '\x1B[5G',
      '\x1B[3G', '\x1B[0J',
      '{key : {key2 :[] }}\nbck-i-search: }_', '\x1B[1A', '\x1B[21G',
      '\x1B[3G', '\x1B[0J',
      '{key : {key2 :[] }}\nbck-i-search: }_', '\x1B[1A', '\x1B[20G',
      '\x1B[3G', '\x1B[0J',
      'fu',
      '\r\n',
      '\x1B[1G', '\x1B[0J',
      prompt, '\x1B[3G',
      '1', '+', '1', '\n// 2', '\x1B[6G', '\x1B[1A',
      '\x1B[1B', '\x1B[2K', '\x1B[1A',
      '\r\n',
      '2\n',
      '\x1B[1G', '\x1B[0J',
      prompt, '\x1B[3G',
      '\nbck-i-search: _', '\x1B[1A',
      '\x1B[3G', '\x1B[0J',
      '1+1\nbck-i-search: +_', '\x1B[1A', '\x1B[4G',
      '\x1B[3G', '\x1B[0J',
      '1+1', '\x1B[4G',
      '\x1B[2C',
      '\r\n',
      '2\n',
      '\x1B[1G', '\x1B[0J',
      prompt, '\x1B[3G',
      '2',
      '\nbck-i-search: _', '\x1B[1A', '\x1B[4G',
      '\x1B[3G', '\x1B[0J',
      'Array(100).fill(1)\nbck-i-search: r_', '\x1B[1A', '\x1B[5G',
      '\x1B[3G', '\x1B[0J',
      'repl.repl.historyIndex\nbck-i-search: re_', '\x1B[1A', '\x1B[8G',
      '\x1B[3G', '\x1B[0J',
      'repl.repl.historyIndex', '\x1B[8G',
      '\x1B[1G', '\x1B[0J',
      `${prompt}ab = "aaaa"`, '\x1B[14G',
      '\x1B[1G', '\x1B[0J',
      `${prompt}repl.repl.historyIndex`, '\x1B[25G', '\n// 8',
      '\x1B[25G', '\x1B[1A',
      '\x1B[1B', '\x1B[2K', '\x1B[1A',
      '\nfwd-i-search: _', '\x1B[1A', '\x1B[25G',
      '\x1B[3G', '\x1B[0J',
      'repl.repl.historyIndex',
      '\r\n',
      '-1\n',
      '\x1B[1G', '\x1B[0J',
      prompt, '\x1B[3G',
      '\r\n',
    ],
    clean: false
  },
];
const numtests = tests.length;

const runTestWrap = common.mustCall(runTest, numtests);

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

function runTest() {
  const opts = tests.shift();
  if (!opts) return; // All done

  const { expected, skip } = opts;

  // Test unsupported on platform.
  if (skip) {
    setImmediate(runTestWrap, true);
    return;
  }

  const lastChunks = [];
  let i = 0;

  REPL.createInternalRepl(opts.env, {
    input: new ActionStream(),
    output: new stream.Writable({
      write(chunk, _, next) {
        const output = chunk.toString();

        if (!opts.showEscapeCodes &&
            (output[0] === '\x1B' || /^[\r\n]+$/.test(output))) {
          return next();
        }

        lastChunks.push(output);

        if (expected.length && !opts.checkTotal) {
          try {
            assert.strictEqual(output, expected[i]);
          } catch (e) {
            console.error(`Failed test # ${numtests - tests.length}`);
            console.error('Last outputs: ' + inspect(lastChunks, {
              breakLength: 5, colors: true
            }));
            throw e;
          }
          i++;
        }

        next();
      }
    }),
    completer: opts.completer,
    prompt,
    useColors: opts.useColors || false,
    terminal: true
  }, function(err, repl) {
    if (err) {
      console.error(`Failed test # ${numtests - tests.length}`);
      throw err;
    }

    repl.once('close', () => {
      if (opts.clean)
        cleanupTmpFile();

      if (opts.checkTotal) {
        assert.deepStrictEqual(lastChunks, expected);
      } else if (expected.length !== i) {
        console.error(tests[numtests - tests.length - 1]);
        throw new Error(`Failed test # ${numtests - tests.length}`);
      }

      setImmediate(runTestWrap, true);
    });

    if (opts.columns) {
      Object.defineProperty(repl, 'columns', {
        value: opts.columns,
        enumerable: true
      });
    }
    repl.inputStream.run(opts.test);
  });
}

// run the tests
runTest();
