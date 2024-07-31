'use strict';

var utils = require('./utils.js');

var linebreakStyle = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Enforce consistent linebreak style",
      url: "https://eslint.style/rules/js/linebreak-style"
    },
    fixable: "whitespace",
    schema: [
      {
        type: "string",
        enum: ["unix", "windows"]
      }
    ],
    messages: {
      expectedLF: "Expected linebreaks to be 'LF' but found 'CRLF'.",
      expectedCRLF: "Expected linebreaks to be 'CRLF' but found 'LF'."
    }
  },
  create(context) {
    const sourceCode = context.sourceCode;
    function createFix(range, text) {
      return function(fixer) {
        return fixer.replaceTextRange(range, text);
      };
    }
    return {
      Program: function checkForLinebreakStyle(node) {
        const linebreakStyle = context.options[0] || "unix";
        const expectedLF = linebreakStyle === "unix";
        const expectedLFChars = expectedLF ? "\n" : "\r\n";
        const source = sourceCode.getText();
        const pattern = utils.createGlobalLinebreakMatcher();
        let match;
        let i = 0;
        while ((match = pattern.exec(source)) !== null) {
          i++;
          if (match[0] === expectedLFChars)
            continue;
          const index = match.index;
          const range = [index, index + match[0].length];
          context.report({
            node,
            loc: {
              start: {
                line: i,
                column: sourceCode.lines[i - 1].length
              },
              end: {
                line: i + 1,
                column: 0
              }
            },
            messageId: expectedLF ? "expectedLF" : "expectedCRLF",
            fix: createFix(range, expectedLFChars)
          });
        }
      }
    };
  }
});

exports.linebreakStyle = linebreakStyle;
