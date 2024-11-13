'use strict';

const {
  Array,
  ArrayPrototypeFill,
  ArrayPrototypePush,
  ArrayPrototypeSlice,
  StringPrototypeEndsWith,
} = primordials;

const colors = require('internal/util/colors');

const kNopLinesToCollapse = 5;

function areLinesEqual(actual, expected, checkCommaDisparity) {
  if (actual === expected) {
    return true;
  }
  if (checkCommaDisparity) {
    return `${actual},` === expected || actual === `${expected},`;
  }
  return false;
}

function myersDiff(actual, expected, checkCommaDisparity = false) {
  const actualLength = actual.length;
  const expectedLength = expected.length;
  const max = actualLength + expectedLength;
  const v = ArrayPrototypeFill(Array(2 * max + 1), 0);

  const trace = [];

  for (let diffLevel = 0; diffLevel <= max; diffLevel++) {
    const newTrace = ArrayPrototypeSlice(v);
    ArrayPrototypePush(trace, newTrace);

    for (let diagonalIndex = -diffLevel; diagonalIndex <= diffLevel; diagonalIndex += 2) {
      let x;
      if (diagonalIndex === -diffLevel ||
        (diagonalIndex !== diffLevel && v[diagonalIndex - 1 + max] < v[diagonalIndex + 1 + max])) {
        x = v[diagonalIndex + 1 + max];
      } else {
        x = v[diagonalIndex - 1 + max] + 1;
      }

      let y = x - diagonalIndex;

      while (x < actualLength && y < expectedLength && areLinesEqual(actual[x], expected[y], checkCommaDisparity)) {
        x++;
        y++;
      }

      v[diagonalIndex + max] = x;

      if (x >= actualLength && y >= expectedLength) {
        return backtrack(trace, actual, expected, checkCommaDisparity);
      }
    }
  }
}

function backtrack(trace, actual, expected, checkCommaDisparity) {
  const actualLength = actual.length;
  const expectedLength = expected.length;
  const max = actualLength + expectedLength;

  let x = actualLength;
  let y = expectedLength;
  const result = [];

  for (let diffLevel = trace.length - 1; diffLevel >= 0; diffLevel--) {
    const v = trace[diffLevel];
    const diagonalIndex = x - y;
    let prevDiagonalIndex;

    if (diagonalIndex === -diffLevel ||
      (diagonalIndex !== diffLevel && v[diagonalIndex - 1 + max] < v[diagonalIndex + 1 + max])) {
      prevDiagonalIndex = diagonalIndex + 1;
    } else {
      prevDiagonalIndex = diagonalIndex - 1;
    }

    const prevX = v[prevDiagonalIndex + max];
    const prevY = prevX - prevDiagonalIndex;

    while (x > prevX && y > prevY) {
      const value = !checkCommaDisparity ||
        StringPrototypeEndsWith(actual[x - 1], ',') ? actual[x - 1] : expected[y - 1];
      ArrayPrototypePush(result, { __proto__: null, type: 'nop', value });
      x--;
      y--;
    }

    if (diffLevel > 0) {
      if (x > prevX) {
        ArrayPrototypePush(result, { __proto__: null, type: 'insert', value: actual[x - 1] });
        x--;
      } else {
        ArrayPrototypePush(result, { __proto__: null, type: 'delete', value: expected[y - 1] });
        y--;
      }
    }
  }

  return result;
}

function printSimpleMyersDiff(diff) {
  let message = '';

  for (let diffIdx = diff.length - 1; diffIdx >= 0; diffIdx--) {
    const { type, value } = diff[diffIdx];
    if (type === 'insert') {
      message += `${colors.green}${value}${colors.white}`;
    } else if (type === 'delete') {
      message += `${colors.red}${value}${colors.white}`;
    } else {
      message += `${colors.white}${value}${colors.white}`;
    }
  }

  return `\n${message}`;
}

function printMyersDiff(diff, simple = false) {
  let message = '';
  let skipped = false;
  let nopCount = 0;

  for (let diffIdx = diff.length - 1; diffIdx >= 0; diffIdx--) {
    const { type, value } = diff[diffIdx];
    const previousType = (diffIdx < (diff.length - 1)) ? diff[diffIdx + 1].type : null;
    const typeChanged = previousType && (type !== previousType);

    if (typeChanged && previousType === 'nop') {
      // Avoid grouping if only one line would have been grouped otherwise
      if (nopCount === kNopLinesToCollapse + 1) {
        message += `${colors.white}  ${diff[diffIdx + 1].value}\n`;
      } else if (nopCount === kNopLinesToCollapse + 2) {
        message += `${colors.white}  ${diff[diffIdx + 2].value}\n`;
        message += `${colors.white}  ${diff[diffIdx + 1].value}\n`;
      } if (nopCount >= (kNopLinesToCollapse + 3)) {
        message += `${colors.blue}...${colors.white}\n`;
        message += `${colors.white}  ${diff[diffIdx + 1].value}\n`;
        skipped = true;
      }
      nopCount = 0;
    }

    if (type === 'insert') {
      message += `${colors.green}+${colors.white} ${value}\n`;
    } else if (type === 'delete') {
      message += `${colors.red}-${colors.white} ${value}\n`;
    } else if (type === 'nop') {
      if (nopCount < kNopLinesToCollapse) {
        message += `${colors.white}  ${value}\n`;
      }
      nopCount++;
    }
  }

  message = message.trimEnd();

  return { message: `\n${message}`, skipped };
}

module.exports = { myersDiff, printMyersDiff, printSimpleMyersDiff };
