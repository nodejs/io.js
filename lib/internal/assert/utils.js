'use strict';

const {
  ArrayPrototypeShift,
  Error,
  ErrorCaptureStackTrace,
  FunctionPrototypeBind,
  RegExpPrototypeSymbolReplace,
  SafeMap,
  StringPrototypeCharCodeAt,
  StringPrototypeIncludes,
  StringPrototypeIndexOf,
  StringPrototypeReplace,
  StringPrototypeSlice,
  StringPrototypeSplit,
  StringPrototypeStartsWith,
} = primordials;

const { Buffer } = require('buffer');
const {
  isErrorStackTraceLimitWritable,
  overrideStackTrace,
} = require('internal/errors');
const AssertionError = require('internal/assert/assertion_error');
const { openSync, closeSync, readSync } = require('fs');
const { EOL } = require('internal/constants');
const { BuiltinModule } = require('internal/bootstrap/realm');
const { isError } = require('internal/util');

const errorCache = new SafeMap();
const { fileURLToPath } = require('internal/url');

let parseExpressionAt;
let findNodeAround;
let tokenizer;
let decoder;

// Escape control characters but not \n and \t to keep the line breaks and
// indentation intact.
// eslint-disable-next-line no-control-regex
const escapeSequencesRegExp = /[\x00-\x08\x0b\x0c\x0e-\x1f]/g;
const meta = [
  '\\u0000', '\\u0001', '\\u0002', '\\u0003', '\\u0004',
  '\\u0005', '\\u0006', '\\u0007', '\\b', '',
  '', '\\u000b', '\\f', '', '\\u000e',
  '\\u000f', '\\u0010', '\\u0011', '\\u0012', '\\u0013',
  '\\u0014', '\\u0015', '\\u0016', '\\u0017', '\\u0018',
  '\\u0019', '\\u001a', '\\u001b', '\\u001c', '\\u001d',
  '\\u001e', '\\u001f',
];

const escapeFn = (str) => meta[StringPrototypeCharCodeAt(str, 0)];

function findColumn(fd, column, code) {
  if (code.length > column + 100) {
    try {
      return parseCode(code, column);
    } catch {
      // End recursion in case no code could be parsed. The expression should
      // have been found after 2500 characters, so stop trying.
      if (code.length - column > 2500) {
        // eslint-disable-next-line no-throw-literal
        throw null;
      }
    }
  }
  // Read up to 2500 bytes more than necessary in columns. That way we address
  // multi byte characters and read enough data to parse the code.
  const bytesToRead = column - code.length + 2500;
  const buffer = Buffer.allocUnsafe(bytesToRead);
  const bytesRead = readSync(fd, buffer, 0, bytesToRead);
  code += decoder.write(buffer.slice(0, bytesRead));
  // EOF: fast path.
  if (bytesRead < bytesToRead) {
    return parseCode(code, column);
  }
  // Read potentially missing code.
  return findColumn(fd, column, code);
}

function getCode(fd, line, column) {
  let bytesRead = 0;
  if (line === 0) {
    // Special handle line number one. This is more efficient and simplifies the
    // rest of the algorithm. Read more than the regular column number in bytes
    // to prevent multiple reads in case multi byte characters are used.
    return findColumn(fd, column, '');
  }
  let lines = 0;
  // Prevent blocking the event loop by limiting the maximum amount of
  // data that may be read.
  let maxReads = 32; // bytesPerRead * maxReads = 512 KiB
  const bytesPerRead = 16384;
  // Use a single buffer up front that is reused until the call site is found.
  let buffer = Buffer.allocUnsafe(bytesPerRead);
  while (maxReads-- !== 0) {
    // Only allocate a new buffer in case the needed line is found. All data
    // before that can be discarded.
    buffer = lines < line ? buffer : Buffer.allocUnsafe(bytesPerRead);
    bytesRead = readSync(fd, buffer, 0, bytesPerRead);
    // Read the buffer until the required code line is found.
    for (let i = 0; i < bytesRead; i++) {
      if (buffer[i] === 10 && ++lines === line) {
        // If the end of file is reached, directly parse the code and return.
        if (bytesRead < bytesPerRead) {
          return parseCode(buffer.toString('utf8', i + 1, bytesRead), column);
        }
        // Check if the read code is sufficient or read more until the whole
        // expression is read. Make sure multi byte characters are preserved
        // properly by using the decoder.
        const code = decoder.write(buffer.slice(i + 1, bytesRead));
        return findColumn(fd, column, code);
      }
    }
  }
}

function parseCode(code, offset) {
  // Lazy load acorn.
  if (parseExpressionAt === undefined) {
    const Parser = require('internal/deps/acorn/acorn/dist/acorn').Parser;
    ({ findNodeAround } = require('internal/deps/acorn/acorn-walk/dist/walk'));

    parseExpressionAt = FunctionPrototypeBind(Parser.parseExpressionAt, Parser);
    tokenizer = FunctionPrototypeBind(Parser.tokenizer, Parser);
  }
  let node;
  let start;
  // Parse the read code until the correct expression is found.
  for (const token of tokenizer(code, { ecmaVersion: 'latest' })) {
    start = token.start;
    if (start > offset) {
      // No matching expression found. This could happen if the assert
      // expression is bigger than the provided buffer.
      break;
    }
    try {
      node = parseExpressionAt(code, start, { ecmaVersion: 'latest' });
      // Find the CallExpression in the tree.
      node = findNodeAround(node, offset, 'CallExpression');
      if (node?.node.end >= offset) {
        return [
          node.node.start,
          StringPrototypeReplace(StringPrototypeSlice(code,
                                                      node.node.start, node.node.end),
                                 escapeSequencesRegExp, escapeFn),
        ];
      }
    // eslint-disable-next-line no-unused-vars
    } catch (err) {
      continue;
    }
  }
  // eslint-disable-next-line no-throw-literal
  throw null;
}

function getErrMessage(message, fn) {
  const tmpLimit = Error.stackTraceLimit;
  const errorStackTraceLimitIsWritable = isErrorStackTraceLimitWritable();
  // Make sure the limit is set to 1. Otherwise it could fail (<= 0) or it
  // does to much work.
  if (errorStackTraceLimitIsWritable) Error.stackTraceLimit = 1;
  // We only need the stack trace. To minimize the overhead use an object
  // instead of an error.
  const err = {};
  ErrorCaptureStackTrace(err, fn);
  if (errorStackTraceLimitIsWritable) Error.stackTraceLimit = tmpLimit;

  overrideStackTrace.set(err, (_, stack) => stack);
  const call = err.stack[0];

  let filename = call.getFileName();
  const line = call.getLineNumber() - 1;
  let column = call.getColumnNumber() - 1;
  let identifier;
  let code;

  if (filename) {
    identifier = `${filename}${line}${column}`;

    // Skip Node.js modules!
    if (StringPrototypeStartsWith(filename, 'node:') &&
        BuiltinModule.exists(StringPrototypeSlice(filename, 5))) {
      errorCache.set(identifier, undefined);
      return;
    }
  } else {
    return message;
  }

  if (errorCache.has(identifier)) {
    return errorCache.get(identifier);
  }

  let fd;
  try {
    // Set the stack trace limit to zero. This makes sure unexpected token
    // errors are handled faster.
    if (errorStackTraceLimitIsWritable) Error.stackTraceLimit = 0;

    if (filename) {
      if (decoder === undefined) {
        const { StringDecoder } = require('string_decoder');
        decoder = new StringDecoder('utf8');
      }

      // ESM file prop is a file proto. Convert that to path.
      // This ensure opensync will not throw ENOENT for ESM files.
      const fileProtoPrefix = 'file://';
      if (StringPrototypeStartsWith(filename, fileProtoPrefix)) {
        filename = fileURLToPath(filename);
      }

      fd = openSync(filename, 'r', 0o666);
      // Reset column and message.
      ({ 0: column, 1: message } = getCode(fd, line, column));
      // Flush unfinished multi byte characters.
      decoder.end();
    } else {
      for (let i = 0; i < line; i++) {
        code = StringPrototypeSlice(code,
                                    StringPrototypeIndexOf(code, '\n') + 1);
      }
      ({ 0: column, 1: message } = parseCode(code, column));
    }
    // Always normalize indentation, otherwise the message could look weird.
    if (StringPrototypeIncludes(message, '\n')) {
      if (EOL === '\r\n') {
        message = RegExpPrototypeSymbolReplace(/\r\n/g, message, '\n');
      }
      const frames = StringPrototypeSplit(message, '\n');
      message = ArrayPrototypeShift(frames);
      for (const frame of frames) {
        let pos = 0;
        while (pos < column && (frame[pos] === ' ' || frame[pos] === '\t')) {
          pos++;
        }
        message += `\n  ${StringPrototypeSlice(frame, pos)}`;
      }
    }
    message = `The expression evaluated to a falsy value:\n\n  ${message}\n`;
    // Make sure to always set the cache! No matter if the message is
    // undefined or not
    errorCache.set(identifier, message);

    return message;
  } catch {
    // Invalidate cache to prevent trying to read this part again.
    errorCache.set(identifier, undefined);
  } finally {
    // Reset limit.
    if (errorStackTraceLimitIsWritable) Error.stackTraceLimit = tmpLimit;
    if (fd !== undefined)
      closeSync(fd);
  }
}

function innerOk(fn, argLen, value, message) {
  if (!value) {
    let generatedMessage = false;

    if (argLen === 0) {
      generatedMessage = true;
      message = 'No value argument passed to `assert.ok()`';
    } else if (message == null) {
      generatedMessage = true;
      message = getErrMessage(message, fn);
    } else if (isError(message)) {
      throw message;
    }

    const err = new AssertionError({
      actual: value,
      expected: true,
      message,
      operator: '==',
      stackStartFn: fn,
    });
    err.generatedMessage = generatedMessage;
    throw err;
  }
}

module.exports = {
  innerOk,
};
