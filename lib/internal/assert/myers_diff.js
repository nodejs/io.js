'use strict';

const {
  ArrayPrototypePush,
  ArrayPrototypeSlice,
  Int32Array,
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
  const v = new Int32Array(2 * max + 1);

  const trace = [];

  for (let diffLevel = 0; diffLevel <= max; diffLevel++) {
    const newTrace = ArrayPrototypeSlice(v);
    ArrayPrototypePush(trace, newTrace);

    for (let diagonalIndex = -diffLevel; diagonalIndex <= diffLevel; diagonalIndex += 2) {
      const offset = diagonalIndex + max;
      const previousOffset = v[offset - 1];
      const nextOffset = v[offset + 1];
      let x = diagonalIndex === -diffLevel || (diagonalIndex !== diffLevel && previousOffset < nextOffset) ?
        nextOffset :
        previousOffset + 1;
      let y = x - diagonalIndex;

      while (
        x < actualLength &&
        y < expectedLength &&
        areLinesEqual(actual[x], expected[y], checkCommaDisparity)
      ) {
        x++;
        y++;
      }

      v[offset] = x;

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
    const offset = diagonalIndex + max;

    let prevDiagonalIndex;
    if (
      diagonalIndex === -diffLevel ||
      (diagonalIndex !== diffLevel && v[offset - 1] < v[offset + 1])
    ) {
      prevDiagonalIndex = diagonalIndex + 1;
    } else {
      prevDiagonalIndex = diagonalIndex - 1;
    }

    const prevX = v[prevDiagonalIndex + max];
    const prevY = prevX - prevDiagonalIndex;

    while (x > prevX && y > prevY) {
      const actualItem = actual[x - 1];
      const value =
        !checkCommaDisparity || StringPrototypeEndsWith(actualItem, ',') ?
          actualItem :
          expected[y - 1];
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
    let color = colors.white;

    if (type === 'insert') {
      color = colors.green;
    } else if (type === 'delete') {
      color = colors.red;
    }

    message += `${color}${value}${colors.white}`;
  }

  return `\n${message}`;
}

function printMyersDiff(diff, operator) {
  let message = '';
  let skipped = false;
  let nopCount = 0;

  for (let diffIdx = diff.length - 1; diffIdx >= 0; diffIdx--) {
    const { type, value } = diff[diffIdx];
    const previousType = diffIdx < diff.length - 1 ? diff[diffIdx + 1].type : null;

    // Avoid grouping if only one line would have been grouped otherwise
    if (previousType === 'nop' && type !== previousType) {
      if (nopCount === kNopLinesToCollapse + 1) {
        message += `${colors.white}  ${diff[diffIdx + 1].value}\n`;
      } else if (nopCount === kNopLinesToCollapse + 2) {
        message += `${colors.white}  ${diff[diffIdx + 2].value}\n`;
        message += `${colors.white}  ${diff[diffIdx + 1].value}\n`;
      } else if (nopCount >= kNopLinesToCollapse + 3) {
        message += `${colors.blue}...${colors.white}\n`;
        message += `${colors.white}  ${diff[diffIdx + 1].value}\n`;
        skipped = true;
      }
      nopCount = 0;
    }

    if (type === 'insert') {
      if (operator === 'partialDeepStrictEqual') {
        message += `${colors.gray}${colors.hasColors ? ' ' : '+'} ${value}${colors.white}\n`;
      } else {
        message += `${colors.green}+${colors.white} ${value}\n`;
      }
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
