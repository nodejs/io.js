'use strict';
const {
  ArrayPrototypeForEach,
  ArrayPrototypeJoin,
  ArrayPrototypePush,
  DatePrototypeToISOString,
  ObjectEntries,
  RegExpPrototypeSymbolReplace,
  RegExpPrototypeSymbolSplit,
  SafeMap,
  SafeSet,
  StringPrototypeRepeat,
  StringPrototypeReplaceAll,
} = primordials;
const { inspectWithNoCustomRetry } = require('internal/errors');
const { isError, kEmptyObject } = require('internal/util');
const { getCoverageReport } = require('internal/test_runner/utils');
const kDefaultIndent = '    '; // 4 spaces
const kFrameStartRegExp = /^ {4}at /;
const kLineBreakRegExp = /\n|\r\n/;
const kDefaultTAPVersion = 13;
const inspectOptions = { __proto__: null, colors: false, breakLength: Infinity };
let testModule; // Lazy loaded due to circular dependency.

function lazyLoadTest() {
  testModule ??= require('internal/test_runner/test');
  return testModule;
}


async function * tapReporter(source) {
  yield `TAP version ${kDefaultTAPVersion}\n`;
  for await (const { type, data } of source) {
    switch (type) {
      case 'test:fail': {
        yield reportTest(data.nesting, data.testNumber, 'not ok', data.name, data.skip, data.todo);
        const location = data.file ? `${data.file}:${data.line}:${data.column}` : null;
        yield reportDetails(data.nesting, data.details, location);
        break;
      } case 'test:pass':
        yield reportTest(data.nesting, data.testNumber, 'ok', data.name, data.skip, data.todo);
        yield reportDetails(data.nesting, data.details, null);
        break;
      case 'test:plan':
        yield `${indent(data.nesting)}1..${data.count}\n`;
        break;
      case 'test:start':
        yield `${indent(data.nesting)}# Subtest: ${tapEscape(data.name)}\n`;
        break;
      case 'test:stderr':
      case 'test:stdout': {
        const lines = RegExpPrototypeSymbolSplit(kLineBreakRegExp, data.message);
        for (let i = 0; i < lines.length; i++) {
          if (lines[i].length === 0) continue;
          yield `# ${tapEscape(lines[i])}\n`;
        }
        break;
      } case 'test:diagnostic':
        yield `${indent(data.nesting)}# ${tapEscape(data.message)}\n`;
        break;
      case 'test:coverage':
        yield getCoverageReport(indent(data.nesting), data.summary, '# ', '', true);
        break;
    }
  }
}

function reportTest(nesting, testNumber, status, name, skip, todo) {
  let line = `${indent(nesting)}${status} ${testNumber}`;

  if (name) {
    line += ` ${tapEscape(`- ${name}`)}`;
  }

  if (skip !== undefined) {
    line += ` # SKIP${typeof skip === 'string' && skip.length ? ` ${tapEscape(skip)}` : ''}`;
  } else if (todo !== undefined) {
    line += ` # TODO${typeof todo === 'string' && todo.length ? ` ${tapEscape(todo)}` : ''}`;
  }

  line += '\n';

  return line;
}

function reportDetails(nesting, data = kEmptyObject, location) {
  const { error, duration_ms } = data;
  const _indent = indent(nesting);
  let details = `${_indent}  ---\n`;

  details += jsToYaml(_indent, 'duration_ms', duration_ms);
  details += jsToYaml(_indent, 'type', data.type);

  if (location) {
    details += jsToYaml(_indent, 'location', location);
  }

  details += jsToYaml(_indent, null, error, new SafeSet());
  details += `${_indent}  ...\n`;
  return details;
}

const memo = new SafeMap();
function indent(nesting) {
  let value = memo.get(nesting);
  if (value === undefined) {
    value = StringPrototypeRepeat(kDefaultIndent, nesting);
    memo.set(nesting, value);
  }

  return value;
}


// In certain places, # and \ need to be escaped as \# and \\.
function tapEscape(input) {
  let result = StringPrototypeReplaceAll(input, '\b', '\\b');
  result = StringPrototypeReplaceAll(result, '\f', '\\f');
  result = StringPrototypeReplaceAll(result, '\t', '\\t');
  result = StringPrototypeReplaceAll(result, '\n', '\\n');
  result = StringPrototypeReplaceAll(result, '\r', '\\r');
  result = StringPrototypeReplaceAll(result, '\v', '\\v');
  result = StringPrototypeReplaceAll(result, '\\', '\\\\');
  result = StringPrototypeReplaceAll(result, '#', '\\#');
  return result;
}

function jsToYaml(indent, name, value, seen) {
  if (value === undefined) {
    return '';
  }

  const prefix = `${indent}  ${name}:`;

  if (value === null) {
    return `${prefix} ~\n`;
  }

  if (typeof value !== 'object') {
    if (typeof value !== 'string') {
      return `${prefix} ${inspectWithNoCustomRetry(value, inspectOptions)}\n`;
    }

    const lines = RegExpPrototypeSymbolSplit(kLineBreakRegExp, value);

    if (lines.length === 1) {
      return `${prefix} ${inspectWithNoCustomRetry(value, inspectOptions)}\n`;
    }

    let str = `${prefix} |-\n`;

    for (let i = 0; i < lines.length; i++) {
      str += `${indent}    ${lines[i]}\n`;
    }

    return str;
  }

  seen.add(value);
  const entries = ObjectEntries(value);
  const isErrorObj = isError(value);
  let propsIndent = indent;
  let result = '';

  if (name != null) {
    result += prefix;
    if (internalBinding('types').isDate(value)) {
      // YAML uses the ISO-8601 standard to express dates.
      result += ' ' + DatePrototypeToISOString(value);
    }
    result += '\n';
    propsIndent += '  ';
  }

  for (let i = 0; i < entries.length; i++) {
    const { 0: key, 1: value } = entries[i];

    if (isErrorObj && (key === 'cause' || key === 'code')) {
      continue;
    }
    if (seen.has(value)) {
      result += `${propsIndent}  ${key}: <Circular>\n`;
      continue;
    }

    result += jsToYaml(propsIndent, key, value, seen);
  }

  if (isErrorObj) {
    const { kUnwrapErrors } = lazyLoadTest();
    const {
      cause,
      code,
      failureType,
      message,
      expected,
      actual,
      operator,
      stack,
      name,
    } = value;
    let errMsg = message ?? '<unknown error>';
    let errName = name;
    let errStack = stack;
    let errCode = code;
    let errExpected = expected;
    let errActual = actual;
    let errOperator = operator;
    let errIsAssertion = isAssertionLike(value);

    // If the ERR_TEST_FAILURE came from an error provided by user code,
    // then try to unwrap the original error message and stack.
    if (code === 'ERR_TEST_FAILURE' && kUnwrapErrors.has(failureType)) {
      errStack = cause?.stack ?? errStack;
      errCode = cause?.code ?? errCode;
      errName = cause?.name ?? errName;
      errMsg = cause?.message ?? errMsg;

      if (isAssertionLike(cause)) {
        errExpected = cause.expected;
        errActual = cause.actual;
        errOperator = cause.operator ?? errOperator;
        errIsAssertion = true;
      }
    }

    result += jsToYaml(indent, 'error', errMsg, seen);

    if (errCode) {
      result += jsToYaml(indent, 'code', errCode, seen);
    }
    if (errName && errName !== 'Error') {
      result += jsToYaml(indent, 'name', errName, seen);
    }

    if (errIsAssertion) {
      // Note that we're deliberately creating shallow copies of the `seen`
      // set here in order to isolate the discovery of circular references
      // within the expected and actual properties respectively.
      result += jsToYaml(indent, 'expected', errExpected, new SafeSet(seen));
      result += jsToYaml(indent, 'actual', errActual, new SafeSet(seen));
      if (errOperator) {
        result += jsToYaml(indent, 'operator', errOperator, seen);
      }
    }

    if (typeof errStack === 'string') {
      const frames = [];

      ArrayPrototypeForEach(
        RegExpPrototypeSymbolSplit(kLineBreakRegExp, errStack),
        (frame) => {
          const processed = RegExpPrototypeSymbolReplace(
            kFrameStartRegExp,
            frame,
            '',
          );

          if (processed.length > 0 && processed.length !== frame.length) {
            ArrayPrototypePush(frames, processed);
          }
        },
      );

      if (frames.length > 0) {
        const frameDelimiter = `\n${indent}    `;

        result += `${indent}  stack: |-${frameDelimiter}`;
        result += `${ArrayPrototypeJoin(frames, frameDelimiter)}\n`;
      }
    }
  }

  return result;
}

function isAssertionLike(value) {
  return value && typeof value === 'object' && 'expected' in value && 'actual' in value;
}

module.exports = tapReporter;
