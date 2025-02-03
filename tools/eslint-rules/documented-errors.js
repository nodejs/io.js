'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const { isDefiningError } = require('./rules-utils.js');

// Load the errors documentation file once
const docPath = path.resolve(__dirname, '../../doc/api/errors.md');
const doc = fs.readFileSync(docPath, 'utf8');

// Helper function to parse errors documentation and return a Map
function getErrorsInDoc() {
  const lines = doc.split('\n');
  let currentHeader;
  const errors = new Set();
  const legacyErrors = new Set();
  const codePattern = /^### `([^`]+)`$/;
  const anchorPattern = /^<a id="([^"]+)"><\/a>$/;

  let previousAnchor;

  function parse(line, legacy, lineNumber) {
    const anchorMatch = anchorPattern.exec(line);
    if (anchorMatch) {
      const code = anchorMatch[1];
      if (previousAnchor != null && previousAnchor > code) {
        throw new Error(`Unordered error anchor in ${docPath}:${lineNumber}`, { cause: `${previousAnchor} ≤ ${code}` });
      }
      previousAnchor = code;
      return;
    }

    const codeMatch = codePattern.exec(line);
    if (codeMatch == null) return;

    const code = codeMatch[1];
    if (previousAnchor == null) {
      throw new Error(`Missing error anchor in ${docPath}:${lineNumber}`, { cause: code });
    }
    assert.strictEqual(code, previousAnchor, `Error anchor do not match with error code in ${docPath}:${lineNumber}`);

    if (legacy && errors.has(code)) {
      throw new Error(`Error is documented both as legacy and non-legacy in ${docPath}:${lineNumber}`, { cause: code });
    }
    (legacy ? legacyErrors : errors).add(code);
  }

  let lineNumber = 0;
  for (const line of lines) {
    lineNumber++;
    if (line.startsWith('## ')) currentHeader = line.substring(3);
    if (currentHeader === 'Node.js error codes') parse(line, false, lineNumber);
    if (line === '## Legacy Node.js error codes') previousAnchor = null;
    if (currentHeader === 'Legacy Node.js error codes') parse(line, true, lineNumber);
  }

  return { errors, legacyErrors };
}

// Main rule export
module.exports = {
  create(context) {
    const { errors, legacyErrors } = getErrorsInDoc();
    return {
      ExpressionStatement(node) {
        if (!isDefiningError(node)) return;

        const code = node.expression.arguments?.[0]?.value;
        if (!code) return;

        if (legacyErrors.has(code)) {
          context.report({
            node,
            message: `"${code}" is marked as legacy, yet it is used in lib/.`,
          });
        } else if (!errors.has(code)) {
          context.report({
            node,
            message: `"${code}" is not documented in doc/api/errors.md`,
          });
        }
      },
    };
  },
};
