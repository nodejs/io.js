/**
 * @fileOverview Any non-ASCII characters in lib/ will increase the size
 *               of the compiled node binary. This linter rule ensures that
 *               any such character is reported.
 * @author Sarat Addepalli <sarat.addepalli@gmail.com>
 */

'use strict';

//------------------------------------------------------------------------------
// Rule Definition
//------------------------------------------------------------------------------

const nonAsciiRegexPattern = /[^\r\n\x20-\x7e]/;
const suggestions = {
  '’': '\'',
  '‛': '\'',
  '‘': '\'',
  '“': '"',
  '‟': '"',
  '”': '"',
  '«': '"',
  '»': '"',
  '—': '-',
};

module.exports = {
  create(context) {

    const reportIfError = (node, sourceCode) => {

      const matches = sourceCode.text.match(nonAsciiRegexPattern);

      if (!matches) return;

      const offendingCharacter = matches[0];
      const offendingCharacterPosition = matches.index;
      const suggestion = suggestions[offendingCharacter];

      let message = `Non-ASCII character '${offendingCharacter}' detected.`;

      message = suggestion ?
        `${message} Consider replacing with: ${suggestion}` :
        message;

      context.report({
        node,
        message,
        loc: sourceCode.getLocFromIndex(offendingCharacterPosition),
      });
    };

    return {
      Program: (node) => reportIfError(node, context.sourceCode),
    };
  },
};
