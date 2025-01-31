'use strict';

const common = require('../common');
const assert = require('assert');
const repl = require('repl');
const ArrayStream = require('../common/arraystream');

if (process.env.TERM === 'dumb') {
  common.skip('skipping - dumb terminal');
}

// \u001b[nG - Moves the cursor to n st column
// \u001b[0J - Clear screen
// \u001b[0K - Clear to line end
const terminalCode = '\u001b[1G\u001b[0J> \u001b[3G';
const terminalCodeRegex = new RegExp(terminalCode.replace(/\[/g, '\\['), 'g');

function run({ input, output, event, checkTerminalCodes = true }) {
  const stream = new ArrayStream();
  let found = '';

  stream.write = (msg) => found += msg.replace('\r', '');

  let expected =
    `${terminalCode}.editor\n` +
    '// Entering editor mode (Ctrl+D to finish, Ctrl+C to cancel)\n' +
    `${input}${output}\n${terminalCode}`;

  const replServer = repl.start({
    prompt: '> ',
    terminal: true,
    input: stream,
    output: stream,
    useColors: false
  });

  stream.emit('data', '.editor\n');
  stream.emit('data', input);
  replServer.write('', event);
  replServer.close();

  if (!checkTerminalCodes) {
    found = found.replace(terminalCodeRegex, '').replace(/\n/g, '');
    expected = expected.replace(terminalCodeRegex, '').replace(/\n/g, '');
  }

  assert.strictEqual(found, expected);
}

const tests = [
  {
    input: '',
    output: '\n(To exit, press Ctrl+C again or Ctrl+D or type .exit)',
    event: { ctrl: true, name: 'c' }
  },
  {
    input: 'let i = 1;',
    output: '',
    event: { ctrl: true, name: 'c' }
  },
  {
    input: 'let i = 1;\ni + 3',
    output: '\n4',
    event: { ctrl: true, name: 'd' }
  },
  {
    input: '  let i = 1;\ni + 3',
    output: '\n4',
    event: { ctrl: true, name: 'd' }
  },
  {
    input: '',
    output: '',
    checkTerminalCodes: false,
    event: null,
  },
];

tests.forEach(run);

// Auto code alignment for .editor mode
function testCodeAlignment({ input, cursor = 0, line = '' }) {
  const stream = new ArrayStream();
  const outputStream = new ArrayStream();

  stream.write = () => { throw new Error('Writing not allowed!'); };

  const replServer = repl.start({
    prompt: '> ',
    terminal: true,
    input: stream,
    output: outputStream,
    useColors: false
  });

  stream.emit('data', '.editor\n');
  input.split('').forEach((ch) => stream.emit('data', ch));
  // Test the content of current line and the cursor position
  assert.strictEqual(line, replServer.line);
  assert.strictEqual(cursor, replServer.cursor);

  replServer.write('', { ctrl: true, name: 'd' });
  replServer.close();
  // Ensure that empty lines are not saved in history
  assert.notStrictEqual(replServer.history[0].trim(), '');
}

const codeAlignmentTests = [
  {
    input: 'let i = 1;\n'
  },
  {
    input: '  let i = 1;\n',
    cursor: 2,
    line: '  '
  },
  {
    input: '     let i = 1;\n',
    cursor: 5,
    line: '     '
  },
  {
    input: ' let i = 1;\n let j = 2\n',
    cursor: 2,
    line: '  '
  },
];

codeAlignmentTests.forEach(testCodeAlignment);
