'use strict';

// Flags: --expose-internals --permission --allow-fs-read=*

const common = require('../common');
const stream = require('stream');
const REPL = require('internal/repl');
const assert = require('assert');
const { inspect } = require('util');

if (process.env.TERM === 'dumb') {
  common.skip('skipping - dumb terminal');
}

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
const TABULATION = { name: 'tab' };

const prompt = '> ';

const tests = [
  {
    test: (function*() {
      yield 'f';
      yield TABULATION;
      yield ENTER;
    })(),
    expected: [],
    env: {}
  },
];

const numtests = tests.length;

const runTestWrap = common.mustCall(runTest, numtests);

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
          // TODO(BridgeAR): Auto close on last chunk!
          i++;
        }

        next();
      }
    }),
    allowBlockingCompletions: true,
    completer: opts.completer,
    prompt,
    useColors: false,
    preview: opts.preview,
    terminal: true
  }, function(err, repl) {
    if (err) {
      console.error(`Failed test # ${numtests - tests.length}`);
      throw err;
    }

    repl.once('close', () => {

      if (opts.checkTotal) {
        assert.deepStrictEqual(lastChunks, expected);
      } else if (expected.length !== i) {
        console.error(tests[numtests - tests.length - 1]);
        throw new Error(`Failed test # ${numtests - tests.length}`);
      }

      setImmediate(runTestWrap, true);
    });

    repl.input.run(opts.test);
  });
}

// run the tests
runTest();
