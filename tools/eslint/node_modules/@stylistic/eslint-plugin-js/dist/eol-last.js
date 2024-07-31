'use strict';

var utils = require('./utils.js');

var eolLast = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Require or disallow newline at the end of files",
      url: "https://eslint.style/rules/js/eol-last"
    },
    fixable: "whitespace",
    schema: [
      {
        type: "string",
        enum: ["always", "never", "unix", "windows"]
      }
    ],
    messages: {
      missing: "Newline required at end of file but not found.",
      unexpected: "Newline not allowed at end of file."
    }
  },
  create(context) {
    return {
      Program: function checkBadEOF(node) {
        const sourceCode = context.sourceCode;
        const src = sourceCode.getText();
        const lastLine = sourceCode.lines[sourceCode.lines.length - 1];
        const location = {
          column: lastLine.length,
          line: sourceCode.lines.length
        };
        const LF = "\n";
        const CRLF = `\r${LF}`;
        const endsWithNewline = src.endsWith(LF);
        if (!src.length)
          return;
        let mode = context.options[0] || "always";
        let appendCRLF = false;
        if (mode === "unix") {
          mode = "always";
        }
        if (mode === "windows") {
          mode = "always";
          appendCRLF = true;
        }
        if (mode === "always" && !endsWithNewline) {
          context.report({
            node,
            loc: location,
            messageId: "missing",
            fix(fixer) {
              return fixer.insertTextAfterRange([0, src.length], appendCRLF ? CRLF : LF);
            }
          });
        } else if (mode === "never" && endsWithNewline) {
          const secondLastLine = sourceCode.lines[sourceCode.lines.length - 2];
          context.report({
            node,
            loc: {
              start: { line: sourceCode.lines.length - 1, column: secondLastLine.length },
              end: { line: sourceCode.lines.length, column: 0 }
            },
            messageId: "unexpected",
            fix(fixer) {
              const finalEOLs = /(?:\r?\n)+$/u;
              const match = finalEOLs.exec(sourceCode.text);
              const start = match.index;
              const end = sourceCode.text.length;
              return fixer.replaceTextRange([start, end], "");
            }
          });
        }
      }
    };
  }
});

exports.eolLast = eolLast;
