// Flags: --expose-internals
'use strict';
const common = require('../common');
common.skipIfDumbTerminal();

const assert = require('assert');
const readline = require('readline/promises');
const {
  getStringWidth,
  stripVTControlCharacters
} = require('internal/util/inspect');
const EventEmitter = require('events').EventEmitter;
const { Writable, Readable } = require('stream');

class FakeInput extends EventEmitter {
  resume() {}
  pause() {}
  write() {}
  end() {}
}

function isWarned(emitter) {
  for (const name in emitter) {
    const listeners = emitter[name];
    if (listeners && listeners.warned) return true;
  }
  return false;
}

function getInterface(options) {
  const fi = new FakeInput();
  const rli = new readline.Interface({
    input: fi,
    output: fi,
    ...options,
  });
  return [rli, fi];
}

function assertCursorRowsAndCols(rli, rows, cols) {
  const cursorPos = rli.getCursorPos();
  assert.strictEqual(cursorPos.rows, rows);
  assert.strictEqual(cursorPos.cols, cols);
}

[
  undefined,
  50,
  0,
  100.5,
  5000,
].forEach((crlfDelay) => {
  const [rli] = getInterface({ crlfDelay });
  assert.strictEqual(rli.crlfDelay, Math.max(crlfDelay || 100, 100));
  rli.close();
});

{
  const input = new FakeInput();

  // Constructor throws if completer is not a function or undefined
  assert.throws(() => {
    readline.createInterface({
      input,
      completer: 'string is not valid'
    });
  }, {
    name: 'TypeError',
    code: 'ERR_INVALID_ARG_VALUE'
  });

  assert.throws(() => {
    readline.createInterface({
      input,
      completer: ''
    });
  }, {
    name: 'TypeError',
    code: 'ERR_INVALID_ARG_VALUE'
  });

  assert.throws(() => {
    readline.createInterface({
      input,
      completer: false
    });
  }, {
    name: 'TypeError',
    code: 'ERR_INVALID_ARG_VALUE'
  });

  // Constructor throws if history is not an array
  ['not an array', 123, 123n, {}, true, Symbol(), null].forEach((history) => {
    assert.throws(() => {
      readline.createInterface({
        input,
        history,
      });
    }, {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_TYPE'
    });
  });

  // Constructor throws if historySize is not a positive number
  [-1, NaN].forEach((historySize) => {
    assert.throws(() => {
      readline.createInterface({
        input,
        historySize,
      });
    }, {
      name: 'RangeError',
      code: 'ERR_OUT_OF_RANGE',
    });
  });

  // Constructor throws if type of historySize is not a number
  ['not a number', {}, true, Symbol(), null].forEach((historySize) => {
    assert.throws(() => {
      readline.createInterface({
        input,
        historySize,
      });
    }, {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_TYPE',
    });
  });

  // Check for invalid tab sizes.
  assert.throws(
    () => new readline.Interface({
      input,
      tabSize: 0
    }),
    { code: 'ERR_OUT_OF_RANGE' }
  );

  assert.throws(
    () => new readline.Interface({
      input,
      tabSize: '4'
    }),
    { code: 'ERR_INVALID_ARG_TYPE' }
  );

  assert.throws(
    () => new readline.Interface({
      input,
      tabSize: 4.5
    }),
    {
      code: 'ERR_OUT_OF_RANGE',
      message: 'The value of "tabSize" is out of range. ' +
                'It must be an integer. Received 4.5'
    }
  );
}

// Sending a single character with no newline
{
  const fi = new FakeInput();
  const rli = new readline.Interface(fi, {});
  rli.on('line', common.mustNotCall());
  fi.emit('data', 'a');
  rli.close();
}

// Sending multiple newlines at once that does not end with a new line and a
// `end` event(last line is). \r should behave like \n when alone.
{
  const [rli, fi] = getInterface({ terminal: true });
  const expectedLines = ['foo', 'bar', 'baz', 'bat'];
  rli.on('line', common.mustCall((line) => {
    assert.strictEqual(line, expectedLines.shift());
  }, expectedLines.length - 1));
  fi.emit('data', expectedLines.join('\r'));
  rli.close();
}

// \r at start of input should output blank line
{
  const [rli, fi] = getInterface({ terminal: true });
  const expectedLines = ['', 'foo' ];
  rli.on('line', common.mustCall((line) => {
    assert.strictEqual(line, expectedLines.shift());
  }, expectedLines.length));
  fi.emit('data', '\rfoo\r');
  rli.close();
}

// \t does not become part of the input when there is a completer function
{
  const completer = (line) => [[], line];
  const [rli, fi] = getInterface({ terminal: true, completer });
  rli.on('line', common.mustCall((line) => {
    assert.strictEqual(line, 'foo');
  }));
  for (const character of '\tfo\to\t') {
    fi.emit('data', character);
  }
  fi.emit('data', '\n');
  rli.close();
}

// \t when there is no completer function should behave like an ordinary
// character
{
  const [rli, fi] = getInterface({ terminal: true });
  rli.on('line', common.mustCall((line) => {
    assert.strictEqual(line, '\t');
  }));
  fi.emit('data', '\t');
  fi.emit('data', '\n');
  rli.close();
}

// Adding history lines should emit the history event with
// the history array
{
  const [rli, fi] = getInterface({ terminal: true });
  const expectedLines = ['foo', 'bar', 'baz', 'bat'];
  rli.on('history', common.mustCall((history) => {
    const expectedHistory = expectedLines.slice(0, history.length).reverse();
    assert.deepStrictEqual(history, expectedHistory);
  }, expectedLines.length));
  for (const line of expectedLines) {
    fi.emit('data', `${line}\n`);
  }
  rli.close();
}

// Altering the history array in the listener should not alter
// the line being processed
{
  const [rli, fi] = getInterface({ terminal: true });
  const expectedLine = 'foo';
  rli.on('history', common.mustCall((history) => {
    assert.strictEqual(history[0], expectedLine);
    history.shift();
  }));
  rli.on('line', common.mustCall((line) => {
    assert.strictEqual(line, expectedLine);
    assert.strictEqual(rli.history.length, 0);
  }));
  fi.emit('data', `${expectedLine}\n`);
  rli.close();
}

// Duplicate lines are removed from history when
// `options.removeHistoryDuplicates` is `true`
{
  const [rli, fi] = getInterface({
    terminal: true,
    removeHistoryDuplicates: true
  });
  const expectedLines = ['foo', 'bar', 'baz', 'bar', 'bat', 'bat'];
  // ['foo', 'baz', 'bar', bat'];
  let callCount = 0;
  rli.on('line', function(line) {
    assert.strictEqual(line, expectedLines[callCount]);
    callCount++;
  });
  fi.emit('data', `${expectedLines.join('\n')}\n`);
  assert.strictEqual(callCount, expectedLines.length);
  fi.emit('keypress', '.', { name: 'up' }); // 'bat'
  assert.strictEqual(rli.line, expectedLines[--callCount]);
  fi.emit('keypress', '.', { name: 'up' }); // 'bar'
  assert.notStrictEqual(rli.line, expectedLines[--callCount]);
  assert.strictEqual(rli.line, expectedLines[--callCount]);
  fi.emit('keypress', '.', { name: 'up' }); // 'baz'
  assert.strictEqual(rli.line, expectedLines[--callCount]);
  fi.emit('keypress', '.', { name: 'up' }); // 'foo'
  assert.notStrictEqual(rli.line, expectedLines[--callCount]);
  assert.strictEqual(rli.line, expectedLines[--callCount]);
  assert.strictEqual(callCount, 0);
  fi.emit('keypress', '.', { name: 'down' }); // 'baz'
  assert.strictEqual(rli.line, 'baz');
  assert.strictEqual(rli.historyIndex, 2);
  fi.emit('keypress', '.', { name: 'n', ctrl: true }); // 'bar'
  assert.strictEqual(rli.line, 'bar');
  assert.strictEqual(rli.historyIndex, 1);
  fi.emit('keypress', '.', { name: 'n', ctrl: true });
  assert.strictEqual(rli.line, 'bat');
  assert.strictEqual(rli.historyIndex, 0);
  // Activate the substring history search.
  fi.emit('keypress', '.', { name: 'down' }); // 'bat'
  assert.strictEqual(rli.line, 'bat');
  assert.strictEqual(rli.historyIndex, -1);
  // Deactivate substring history search.
  fi.emit('keypress', '.', { name: 'backspace' }); // 'ba'
  assert.strictEqual(rli.historyIndex, -1);
  assert.strictEqual(rli.line, 'ba');
  // Activate the substring history search.
  fi.emit('keypress', '.', { name: 'down' }); // 'ba'
  assert.strictEqual(rli.historyIndex, -1);
  assert.strictEqual(rli.line, 'ba');
  fi.emit('keypress', '.', { name: 'down' }); // 'ba'
  assert.strictEqual(rli.historyIndex, -1);
  assert.strictEqual(rli.line, 'ba');
  fi.emit('keypress', '.', { name: 'up' }); // 'bat'
  assert.strictEqual(rli.historyIndex, 0);
  assert.strictEqual(rli.line, 'bat');
  fi.emit('keypress', '.', { name: 'up' }); // 'bar'
  assert.strictEqual(rli.historyIndex, 1);
  assert.strictEqual(rli.line, 'bar');
  fi.emit('keypress', '.', { name: 'up' }); // 'baz'
  assert.strictEqual(rli.historyIndex, 2);
  assert.strictEqual(rli.line, 'baz');
  fi.emit('keypress', '.', { name: 'up' }); // 'ba'
  assert.strictEqual(rli.historyIndex, 4);
  assert.strictEqual(rli.line, 'ba');
  fi.emit('keypress', '.', { name: 'up' }); // 'ba'
  assert.strictEqual(rli.historyIndex, 4);
  assert.strictEqual(rli.line, 'ba');
  // Deactivate substring history search and reset history index.
  fi.emit('keypress', '.', { name: 'right' }); // 'ba'
  assert.strictEqual(rli.historyIndex, -1);
  assert.strictEqual(rli.line, 'ba');
  // Substring history search activated.
  fi.emit('keypress', '.', { name: 'up' }); // 'ba'
  assert.strictEqual(rli.historyIndex, 0);
  assert.strictEqual(rli.line, 'bat');
  rli.close();
}

// Duplicate lines are not removed from history when
// `options.removeHistoryDuplicates` is `false`
{
  const [rli, fi] = getInterface({
    terminal: true,
    removeHistoryDuplicates: false
  });
  const expectedLines = ['foo', 'bar', 'baz', 'bar', 'bat', 'bat'];
  let callCount = 0;
  rli.on('line', function(line) {
    assert.strictEqual(line, expectedLines[callCount]);
    callCount++;
  });
  fi.emit('data', `${expectedLines.join('\n')}\n`);
  assert.strictEqual(callCount, expectedLines.length);
  fi.emit('keypress', '.', { name: 'up' }); // 'bat'
  assert.strictEqual(rli.line, expectedLines[--callCount]);
  fi.emit('keypress', '.', { name: 'up' }); // 'bar'
  assert.notStrictEqual(rli.line, expectedLines[--callCount]);
  assert.strictEqual(rli.line, expectedLines[--callCount]);
  fi.emit('keypress', '.', { name: 'up' }); // 'baz'
  assert.strictEqual(rli.line, expectedLines[--callCount]);
  fi.emit('keypress', '.', { name: 'up' }); // 'bar'
  assert.strictEqual(rli.line, expectedLines[--callCount]);
  fi.emit('keypress', '.', { name: 'up' }); // 'foo'
  assert.strictEqual(rli.line, expectedLines[--callCount]);
  assert.strictEqual(callCount, 0);
  rli.close();
}

// Regression test for repl freeze, #1968:
// check that nothing fails if 'keypress' event throws.
{
  const [rli, fi] = getInterface({ terminal: true });
  const keys = [];
  const err = new Error('bad thing happened');
  fi.on('keypress', function(key) {
    keys.push(key);
    if (key === 'X') {
      throw err;
    }
  });
  assert.throws(
    () => fi.emit('data', 'fooX'),
    (e) => {
      assert.strictEqual(e, err);
      return true;
    }
  );
  fi.emit('data', 'bar');
  assert.strictEqual(keys.join(''), 'fooXbar');
  rli.close();
}

// History is bound
{
  const [rli, fi] = getInterface({ terminal: true, historySize: 2 });
  const lines = ['line 1', 'line 2', 'line 3'];
  fi.emit('data', lines.join('\n') + '\n');
  assert.strictEqual(rli.history.length, 2);
  assert.strictEqual(rli.history[0], 'line 3');
  assert.strictEqual(rli.history[1], 'line 2');
}

// Question
{
  const [rli] = getInterface({ terminal: true });
  const expectedLines = ['foo'];
  rli.question(expectedLines[0]).then(() => rli.close());
  assertCursorRowsAndCols(rli, 0, expectedLines[0].length);
  rli.close();
}

// Sending a multi-line question
{
  const [rli] = getInterface({ terminal: true });
  const expectedLines = ['foo', 'bar'];
  rli.question(expectedLines.join('\n')).then(() => rli.close());
  assertCursorRowsAndCols(
    rli, expectedLines.length - 1, expectedLines.slice(-1)[0].length);
  rli.close();
}

{
  // Beginning and end of line
  const [rli, fi] = getInterface({ terminal: true, prompt: '' });
  fi.emit('data', 'the quick brown fox');
  fi.emit('keypress', '.', { ctrl: true, name: 'a' });
  assertCursorRowsAndCols(rli, 0, 0);
  fi.emit('keypress', '.', { ctrl: true, name: 'e' });
  assertCursorRowsAndCols(rli, 0, 19);
  rli.close();
}

{
  // Back and Forward one character
  const [rli, fi] = getInterface({ terminal: true, prompt: '' });
  fi.emit('data', 'the quick brown fox');
  assertCursorRowsAndCols(rli, 0, 19);

  // Back one character
  fi.emit('keypress', '.', { ctrl: true, name: 'b' });
  assertCursorRowsAndCols(rli, 0, 18);
  // Back one character
  fi.emit('keypress', '.', { ctrl: true, name: 'b' });
  assertCursorRowsAndCols(rli, 0, 17);
  // Forward one character
  fi.emit('keypress', '.', { ctrl: true, name: 'f' });
  assertCursorRowsAndCols(rli, 0, 18);
  // Forward one character
  fi.emit('keypress', '.', { ctrl: true, name: 'f' });
  assertCursorRowsAndCols(rli, 0, 19);
  rli.close();
}

// Back and Forward one astral character
{
  const [rli, fi] = getInterface({ terminal: true, prompt: '' });
  fi.emit('data', '💻');

  // Move left one character/code point
  fi.emit('keypress', '.', { name: 'left' });
  assertCursorRowsAndCols(rli, 0, 0);

  // Move right one character/code point
  fi.emit('keypress', '.', { name: 'right' });
  assertCursorRowsAndCols(rli, 0, 2);

  rli.on('line', common.mustCall((line) => {
    assert.strictEqual(line, '💻');
  }));
  fi.emit('data', '\n');
  rli.close();
}

// Two astral characters left
{
  const [rli, fi] = getInterface({ terminal: true, prompt: '' });
  fi.emit('data', '💻');

  // Move left one character/code point
  fi.emit('keypress', '.', { name: 'left' });
  assertCursorRowsAndCols(rli, 0, 0);

  fi.emit('data', '🐕');
  assertCursorRowsAndCols(rli, 0, 2);

  rli.on('line', common.mustCall((line) => {
    assert.strictEqual(line, '🐕💻');
  }));
  fi.emit('data', '\n');
  rli.close();
}

// Two astral characters right
{
  const [rli, fi] = getInterface({ terminal: true, prompt: '' });
  fi.emit('data', '💻');

  // Move left one character/code point
  fi.emit('keypress', '.', { name: 'right' });
  assertCursorRowsAndCols(rli, 0, 2);

  fi.emit('data', '🐕');
  assertCursorRowsAndCols(rli, 0, 4);

  rli.on('line', common.mustCall((line) => {
    assert.strictEqual(line, '💻🐕');
  }));
  fi.emit('data', '\n');
  rli.close();
}

{
  // `wordLeft` and `wordRight`
  const [rli, fi] = getInterface({ terminal: true, prompt: '' });
  fi.emit('data', 'the quick brown fox');
  fi.emit('keypress', '.', { ctrl: true, name: 'left' });
  assertCursorRowsAndCols(rli, 0, 16);
  fi.emit('keypress', '.', { meta: true, name: 'b' });
  assertCursorRowsAndCols(rli, 0, 10);
  fi.emit('keypress', '.', { ctrl: true, name: 'right' });
  assertCursorRowsAndCols(rli, 0, 16);
  fi.emit('keypress', '.', { meta: true, name: 'f' });
  assertCursorRowsAndCols(rli, 0, 19);
  rli.close();
}

// `deleteWordLeft`
[
  { ctrl: true, name: 'w' },
  { ctrl: true, name: 'backspace' },
  { meta: true, name: 'backspace' },
].forEach((deleteWordLeftKey) => {
  let [rli, fi] = getInterface({ terminal: true, prompt: '' });
  fi.emit('data', 'the quick brown fox');
  fi.emit('keypress', '.', { ctrl: true, name: 'left' });
  rli.on('line', common.mustCall((line) => {
    assert.strictEqual(line, 'the quick fox');
  }));
  fi.emit('keypress', '.', deleteWordLeftKey);
  fi.emit('data', '\n');
  rli.close();

  // No effect if pressed at beginning of line
  [rli, fi] = getInterface({ terminal: true, prompt: '' });
  fi.emit('data', 'the quick brown fox');
  fi.emit('keypress', '.', { ctrl: true, name: 'a' });
  rli.on('line', common.mustCall((line) => {
    assert.strictEqual(line, 'the quick brown fox');
  }));
  fi.emit('keypress', '.', deleteWordLeftKey);
  fi.emit('data', '\n');
  rli.close();
});

// `deleteWordRight`
[
  { ctrl: true, name: 'delete' },
  { meta: true, name: 'delete' },
  { meta: true, name: 'd' },
].forEach((deleteWordRightKey) => {
  let [rli, fi] = getInterface({ terminal: true, prompt: '' });
  fi.emit('data', 'the quick brown fox');
  fi.emit('keypress', '.', { ctrl: true, name: 'left' });
  fi.emit('keypress', '.', { ctrl: true, name: 'left' });
  rli.on('line', common.mustCall((line) => {
    assert.strictEqual(line, 'the quick fox');
  }));
  fi.emit('keypress', '.', deleteWordRightKey);
  fi.emit('data', '\n');
  rli.close();

  // No effect if pressed at end of line
  [rli, fi] = getInterface({ terminal: true, prompt: '' });
  fi.emit('data', 'the quick brown fox');
  rli.on('line', common.mustCall((line) => {
    assert.strictEqual(line, 'the quick brown fox');
  }));
  fi.emit('keypress', '.', deleteWordRightKey);
  fi.emit('data', '\n');
  rli.close();
});

// deleteLeft
{
  const [rli, fi] = getInterface({ terminal: true, prompt: '' });
  fi.emit('data', 'the quick brown fox');
  assertCursorRowsAndCols(rli, 0, 19);

  // Delete left character
  fi.emit('keypress', '.', { ctrl: true, name: 'h' });
  assertCursorRowsAndCols(rli, 0, 18);
  rli.on('line', common.mustCall((line) => {
    assert.strictEqual(line, 'the quick brown fo');
  }));
  fi.emit('data', '\n');
  rli.close();
}

// deleteLeft astral character
{
  const [rli, fi] = getInterface({ terminal: true, prompt: '' });
  fi.emit('data', '💻');
  assertCursorRowsAndCols(rli, 0, 2);
  // Delete left character
  fi.emit('keypress', '.', { ctrl: true, name: 'h' });
  assertCursorRowsAndCols(rli, 0, 0);
  rli.on('line', common.mustCall((line) => {
    assert.strictEqual(line, '');
  }));
  fi.emit('data', '\n');
  rli.close();
}

// deleteRight
{
  const [rli, fi] = getInterface({ terminal: true, prompt: '' });
  fi.emit('data', 'the quick brown fox');

  // Go to the start of the line
  fi.emit('keypress', '.', { ctrl: true, name: 'a' });
  assertCursorRowsAndCols(rli, 0, 0);

  // Delete right character
  fi.emit('keypress', '.', { ctrl: true, name: 'd' });
  assertCursorRowsAndCols(rli, 0, 0);
  rli.on('line', common.mustCall((line) => {
    assert.strictEqual(line, 'he quick brown fox');
  }));
  fi.emit('data', '\n');
  rli.close();
}

// deleteRight astral character
{
  const [rli, fi] = getInterface({ terminal: true, prompt: '' });
  fi.emit('data', '💻');

  // Go to the start of the line
  fi.emit('keypress', '.', { ctrl: true, name: 'a' });
  assertCursorRowsAndCols(rli, 0, 0);

  // Delete right character
  fi.emit('keypress', '.', { ctrl: true, name: 'd' });
  assertCursorRowsAndCols(rli, 0, 0);
  rli.on('line', common.mustCall((line) => {
    assert.strictEqual(line, '');
  }));
  fi.emit('data', '\n');
  rli.close();
}

// deleteLineLeft
{
  const [rli, fi] = getInterface({ terminal: true, prompt: '' });
  fi.emit('data', 'the quick brown fox');
  assertCursorRowsAndCols(rli, 0, 19);

  // Delete from current to start of line
  fi.emit('keypress', '.', { ctrl: true, shift: true, name: 'backspace' });
  assertCursorRowsAndCols(rli, 0, 0);
  rli.on('line', common.mustCall((line) => {
    assert.strictEqual(line, '');
  }));
  fi.emit('data', '\n');
  rli.close();
}

// deleteLineRight
{
  const [rli, fi] = getInterface({ terminal: true, prompt: '' });
  fi.emit('data', 'the quick brown fox');

  // Go to the start of the line
  fi.emit('keypress', '.', { ctrl: true, name: 'a' });
  assertCursorRowsAndCols(rli, 0, 0);

  // Delete from current to end of line
  fi.emit('keypress', '.', { ctrl: true, shift: true, name: 'delete' });
  assertCursorRowsAndCols(rli, 0, 0);
  rli.on('line', common.mustCall((line) => {
    assert.strictEqual(line, '');
  }));
  fi.emit('data', '\n');
  rli.close();
}

// Close readline interface
{
  const [rli, fi] = getInterface({ terminal: true, prompt: '' });
  fi.emit('keypress', '.', { ctrl: true, name: 'c' });
  assert(rli.closed);
}

// Multi-line input cursor position
{
  const [rli, fi] = getInterface({ terminal: true, prompt: '' });
  fi.columns = 10;
  fi.emit('data', 'multi-line text');
  assertCursorRowsAndCols(rli, 1, 5);
  rli.close();
}

// Multi-line input cursor position and long tabs
{
  const [rli, fi] = getInterface({ tabSize: 16, terminal: true, prompt: '' });
  fi.columns = 10;
  fi.emit('data', 'multi-line\ttext \t');
  assert.strictEqual(rli.cursor, 17);
  assertCursorRowsAndCols(rli, 3, 2);
  rli.close();
}

// Check for the default tab size.
{
  const [rli, fi] = getInterface({ terminal: true, prompt: '' });
  fi.emit('data', 'the quick\tbrown\tfox');
  assert.strictEqual(rli.cursor, 19);
  // The first tab is 7 spaces long, the second one 3 spaces.
  assertCursorRowsAndCols(rli, 0, 27);
}

// Multi-line prompt cursor position
{
  const [rli, fi] = getInterface({
    terminal: true,
    prompt: '\nfilledline\nwraping text\n> '
  });
  fi.columns = 10;
  fi.emit('data', 't');
  assertCursorRowsAndCols(rli, 4, 3);
  rli.close();
}

// Clear the whole screen
{
  const [rli, fi] = getInterface({ terminal: true, prompt: '' });
  const lines = ['line 1', 'line 2', 'line 3'];
  fi.emit('data', lines.join('\n'));
  fi.emit('keypress', '.', { ctrl: true, name: 'l' });
  assertCursorRowsAndCols(rli, 0, 6);
  rli.on('line', common.mustCall((line) => {
    assert.strictEqual(line, 'line 3');
  }));
  fi.emit('data', '\n');
  rli.close();
}

// Wide characters should be treated as two columns.
assert.strictEqual(getStringWidth('a'), 1);
assert.strictEqual(getStringWidth('あ'), 2);
assert.strictEqual(getStringWidth('谢'), 2);
assert.strictEqual(getStringWidth('고'), 2);
assert.strictEqual(getStringWidth(String.fromCodePoint(0x1f251)), 2);
assert.strictEqual(getStringWidth('abcde'), 5);
assert.strictEqual(getStringWidth('古池や'), 6);
assert.strictEqual(getStringWidth('ノード.js'), 9);
assert.strictEqual(getStringWidth('你好'), 4);
assert.strictEqual(getStringWidth('안녕하세요'), 10);
assert.strictEqual(getStringWidth('A\ud83c\ude00BC'), 5);
assert.strictEqual(getStringWidth('👨‍👩‍👦‍👦'), 8);
assert.strictEqual(getStringWidth('🐕𐐷あ💻😀'), 9);
// TODO(BridgeAR): This should have a width of 4.
assert.strictEqual(getStringWidth('⓬⓪'), 2);
assert.strictEqual(getStringWidth('\u0301\u200D\u200E'), 0);

// Check if vt control chars are stripped
assert.strictEqual(stripVTControlCharacters('\u001b[31m> \u001b[39m'), '> ');
assert.strictEqual(
  stripVTControlCharacters('\u001b[31m> \u001b[39m> '),
  '> > '
);
assert.strictEqual(stripVTControlCharacters('\u001b[31m\u001b[39m'), '');
assert.strictEqual(stripVTControlCharacters('> '), '> ');
assert.strictEqual(getStringWidth('\u001b[31m> \u001b[39m'), 2);
assert.strictEqual(getStringWidth('\u001b[31m> \u001b[39m> '), 4);
assert.strictEqual(getStringWidth('\u001b[31m\u001b[39m'), 0);
assert.strictEqual(getStringWidth('> '), 2);

// Check EventEmitter memory leak
for (let i = 0; i < 12; i++) {
  const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout
  });
  rl.close();
  assert.strictEqual(isWarned(process.stdin._events), false);
  assert.strictEqual(isWarned(process.stdout._events), false);
}

[true, false].forEach(function(terminal) {
  // Disable history
  {
    const [rli, fi] = getInterface({ terminal, historySize: 0 });
    assert.strictEqual(rli.historySize, 0);

    fi.emit('data', 'asdf\n');
    assert.deepStrictEqual(rli.history, []);
    rli.close();
  }

  // Default history size 30
  {
    const [rli, fi] = getInterface({ terminal });
    assert.strictEqual(rli.historySize, 30);

    fi.emit('data', 'asdf\n');
    assert.deepStrictEqual(rli.history, terminal ? ['asdf'] : []);
    rli.close();
  }

  // Sending a full line
  {
    const [rli, fi] = getInterface({ terminal });
    rli.on('line', common.mustCall((line) => {
      assert.strictEqual(line, 'asdf');
    }));
    fi.emit('data', 'asdf\n');
  }

  // Ensure that options.signal.removeEventListener was called
  {
    const ac = new AbortController();
    const signal = ac.signal;
    const [rli] = getInterface({ terminal });
    signal.removeEventListener = common.mustCall(
      (event, onAbortFn) => {
        assert.strictEqual(event, 'abort');
        assert.strictEqual(onAbortFn.name, 'onAbort');
      });

    rli.question('hello?', { signal }).then(common.mustCall());

    rli.write('bar\n');
    ac.abort();
    rli.close();
  }

  // Sending a blank line
  {
    const [rli, fi] = getInterface({ terminal });
    rli.on('line', common.mustCall((line) => {
      assert.strictEqual(line, '');
    }));
    fi.emit('data', '\n');
  }

  // Sending a single character with no newline and then a newline
  {
    const [rli, fi] = getInterface({ terminal });
    let called = false;
    rli.on('line', (line) => {
      called = true;
      assert.strictEqual(line, 'a');
    });
    fi.emit('data', 'a');
    assert.ok(!called);
    fi.emit('data', '\n');
    assert.ok(called);
    rli.close();
  }

  // Sending multiple newlines at once
  {
    const [rli, fi] = getInterface({ terminal });
    const expectedLines = ['foo', 'bar', 'baz'];
    rli.on('line', common.mustCall((line) => {
      assert.strictEqual(line, expectedLines.shift());
    }, expectedLines.length));
    fi.emit('data', `${expectedLines.join('\n')}\n`);
    rli.close();
  }

  // Sending multiple newlines at once that does not end with a new line
  {
    const [rli, fi] = getInterface({ terminal });
    const expectedLines = ['foo', 'bar', 'baz', 'bat'];
    rli.on('line', common.mustCall((line) => {
      assert.strictEqual(line, expectedLines.shift());
    }, expectedLines.length - 1));
    fi.emit('data', expectedLines.join('\n'));
    rli.close();
  }

  // Sending multiple newlines at once that does not end with a new(empty)
  // line and a `end` event
  {
    const [rli, fi] = getInterface({ terminal });
    const expectedLines = ['foo', 'bar', 'baz', ''];
    rli.on('line', common.mustCall((line) => {
      assert.strictEqual(line, expectedLines.shift());
    }, expectedLines.length - 1));
    rli.on('close', common.mustCall());
    fi.emit('data', expectedLines.join('\n'));
    fi.emit('end');
    rli.close();
  }

  // Sending a multi-byte utf8 char over multiple writes
  {
    const buf = Buffer.from('☮', 'utf8');
    const [rli, fi] = getInterface({ terminal });
    let callCount = 0;
    rli.on('line', function(line) {
      callCount++;
      assert.strictEqual(line, buf.toString('utf8'));
    });
    for (const i of buf) {
      fi.emit('data', Buffer.from([i]));
    }
    assert.strictEqual(callCount, 0);
    fi.emit('data', '\n');
    assert.strictEqual(callCount, 1);
    rli.close();
  }

  // Calling readline without `new`
  {
    const [rli, fi] = getInterface({ terminal });
    rli.on('line', common.mustCall((line) => {
      assert.strictEqual(line, 'asdf');
    }));
    fi.emit('data', 'asdf\n');
    rli.close();
  }

  // Calling the question callback
  {
    const [rli] = getInterface({ terminal });
    rli.question('foo?').then(common.mustCall((answer) => {
      assert.strictEqual(answer, 'bar');
    }));
    rli.write('bar\n');
    rli.close();
  }

  // Calling the question callback with abort signal
  {
    const [rli] = getInterface({ terminal });
    const { signal } = new AbortController();
    rli.question('foo?', { signal }).then(common.mustCall((answer) => {
      assert.strictEqual(answer, 'bar');
    }));
    rli.write('bar\n');
    rli.close();
  }

  // Aborting a question
  {
    const ac = new AbortController();
    const signal = ac.signal;
    const [rli] = getInterface({ terminal });
    rli.on('line', common.mustCall((line) => {
      assert.strictEqual(line, 'bar');
    }));
    assert.rejects(rli.question('hello?', { signal }), { name: 'AbortError' })
        .then(common.mustCall());
    ac.abort();
    rli.write('bar\n');
    rli.close();
  }

  (async () => {
    const [rli] = getInterface({ terminal });
    const signal = AbortSignal.abort('boom');
    await assert.rejects(rli.question('hello', { signal }), {
      cause: 'boom',
    });
    rli.close();
  })().then(common.mustCall());

  // Throw an error when question is executed with an aborted signal
  {
    const ac = new AbortController();
    const signal = ac.signal;
    ac.abort();
    const [rli] = getInterface({ terminal });
    assert.rejects(
      rli.question('hello?', { signal }),
      {
        name: 'AbortError'
      }
    ).then(common.mustCall());
    rli.close();
  }

  // Call question after close
  {
    const [rli, fi] = getInterface({ terminal });
    rli.question('What\'s your name?').then(common.mustCall((name) => {
      assert.strictEqual(name, 'Node.js');
      rli.close();
      rli.question('How are you?')
        .then(common.mustNotCall(), common.expectsError({
          code: 'ERR_USE_AFTER_CLOSE',
          name: 'Error'
        }));
      assert.notStrictEqual(rli.getPrompt(), 'How are you?');
    }));
    fi.emit('data', 'Node.js\n');
  }


  // Can create a new readline Interface with a null output argument
  {
    const [rli, fi] = getInterface({ output: null, terminal });
    rli.on('line', common.mustCall((line) => {
      assert.strictEqual(line, 'asdf');
    }));
    fi.emit('data', 'asdf\n');

    rli.setPrompt('ddd> ');
    rli.prompt();
    rli.write("really shouldn't be seeing this");
    rli.question('What do you think of node.js? ', function(answer) {
      console.log('Thank you for your valuable feedback:', answer);
      rli.close();
    });
  }

  // Calling the getPrompt method
  {
    const expectedPrompts = ['$ ', '> '];
    const [rli] = getInterface({ terminal });
    for (const prompt of expectedPrompts) {
      rli.setPrompt(prompt);
      assert.strictEqual(rli.getPrompt(), prompt);
    }
  }

  {
    const expected = terminal ?
      ['\u001b[1G', '\u001b[0J', '$ ', '\u001b[3G'] :
      ['$ '];

    const output = new Writable({
      write: common.mustCall((chunk, enc, cb) => {
        assert.strictEqual(chunk.toString(), expected.shift());
        cb();
        rl.close();
      }, expected.length)
    });

    const rl = readline.createInterface({
      input: new Readable({ read: common.mustCall() }),
      output,
      prompt: '$ ',
      terminal
    });

    rl.prompt();

    assert.strictEqual(rl.getPrompt(), '$ ');
  }

  {
    const fi = new FakeInput();
    assert.deepStrictEqual(fi.listeners(terminal ? 'keypress' : 'data'), []);
  }

  // Emit two line events when the delay
  // between \r and \n exceeds crlfDelay
  {
    const crlfDelay = 200;
    const [rli, fi] = getInterface({ terminal, crlfDelay });
    let callCount = 0;
    rli.on('line', function(line) {
      callCount++;
    });
    fi.emit('data', '\r');
    setTimeout(common.mustCall(() => {
      fi.emit('data', '\n');
      assert.strictEqual(callCount, 2);
      rli.close();
    }), crlfDelay + 10);
  }

  // For the purposes of the following tests, we do not care about the exact
  // value of crlfDelay, only that the behaviour conforms to what's expected.
  // Setting it to Infinity allows the test to succeed even under extreme
  // CPU stress.
  const crlfDelay = Infinity;

  // Set crlfDelay to `Infinity` is allowed
  {
    const delay = 200;
    const [rli, fi] = getInterface({ terminal, crlfDelay });
    let callCount = 0;
    rli.on('line', function(line) {
      callCount++;
    });
    fi.emit('data', '\r');
    setTimeout(common.mustCall(() => {
      fi.emit('data', '\n');
      assert.strictEqual(callCount, 1);
      rli.close();
    }), delay);
  }

  // Sending multiple newlines at once that does not end with a new line
  // and a `end` event(last line is)

  // \r\n should emit one line event, not two
  {
    const [rli, fi] = getInterface({ terminal, crlfDelay });
    const expectedLines = ['foo', 'bar', 'baz', 'bat'];
    rli.on('line', common.mustCall((line) => {
      assert.strictEqual(line, expectedLines.shift());
    }, expectedLines.length - 1));
    fi.emit('data', expectedLines.join('\r\n'));
    rli.close();
  }

  // \r\n should emit one line event when split across multiple writes.
  {
    const [rli, fi] = getInterface({ terminal, crlfDelay });
    const expectedLines = ['foo', 'bar', 'baz', 'bat'];
    let callCount = 0;
    rli.on('line', common.mustCall((line) => {
      assert.strictEqual(line, expectedLines[callCount]);
      callCount++;
    }, expectedLines.length));
    expectedLines.forEach((line) => {
      fi.emit('data', `${line}\r`);
      fi.emit('data', '\n');
    });
    rli.close();
  }

  // Emit one line event when the delay between \r and \n is
  // over the default crlfDelay but within the setting value.
  {
    const delay = 125;
    const [rli, fi] = getInterface({ terminal, crlfDelay });
    let callCount = 0;
    rli.on('line', () => callCount++);
    fi.emit('data', '\r');
    setTimeout(common.mustCall(() => {
      fi.emit('data', '\n');
      assert.strictEqual(callCount, 1);
      rli.close();
    }), delay);
  }
});

// Ensure that the _wordLeft method works even for large input
{
  const input = new Readable({
    read() {
      this.push('\x1B[1;5D'); // CTRL + Left
      this.push(null);
    },
  });
  const output = new Writable({
    write: common.mustCall((data, encoding, cb) => {
      assert.strictEqual(rl.cursor, rl.line.length - 1);
      cb();
    }),
  });
  const rl = new readline.createInterface({
    input,
    output,
    terminal: true,
  });
  rl.line = `a${' '.repeat(1e6)}a`;
  rl.cursor = rl.line.length;
}
