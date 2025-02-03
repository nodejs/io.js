/**
 * @fileoverview Look for unescaped "literal" dots in regular expressions
 * @author Brian White
 */
'use strict';

//------------------------------------------------------------------------------
// Rule Definition
//------------------------------------------------------------------------------

module.exports = {
  create(context) {
    const sourceCode = context.sourceCode;
    const regexpStack = [];
    let regexpBuffer = [];
    let inRegExp = false;

    function report(node, startOffset) {
      const indexOfDot = sourceCode.getIndexFromLoc(node.loc.start) + startOffset;
      context.report({
        node,
        loc: sourceCode.getLocFromIndex(indexOfDot),
        message: 'Unescaped dot character in regular expression',
      });
    }
    const allowedModifiers = ['+', '*', '?', '{'];

    function checkRegExp(nodes) {
      let escaping = false;
      let inCharClass = false;
      for (let n = 0; n < nodes.length; ++n) {
        const pair = nodes[n];
        const node = pair[0];
        const str = pair[1];
        for (let i = 0; i < str.length; ++i) {
          switch (str[i]) {
            case '[':
              if (!escaping)
                inCharClass = true;
              else
                escaping = false;
              break;
            case ']':
              if (!escaping) {
                inCharClass &&= false;
              } else {
                escaping = false;
              }
              break;
            case '\\':
              escaping = !escaping;
              break;
            case '.':
              if (!escaping) {
                if (!inCharClass &&
                  ((i + 1) === str.length ||
                   allowedModifiers.indexOf(str[i + 1]) === -1)) {
                  report(node, i);
                }
              } else {
                escaping = false;
              }
              break;
            default:
              escaping &&= false;
          }
        }
      }
    }

    function checkRegExpStart(node) {
      if (node.callee && node.callee.name === 'RegExp') {
        if (inRegExp) {
          regexpStack.push(regexpBuffer);
          regexpBuffer = [];
        }
        inRegExp = true;
      }
    }

    function checkRegExpEnd(node) {
      if (node.callee && node.callee.name === 'RegExp') {
        checkRegExp(regexpBuffer);
        if (regexpStack.length) {
          regexpBuffer = regexpStack.pop();
        } else {
          inRegExp = false;
          regexpBuffer = [];
        }
      }
    }

    function checkLiteral(node) {
      const isTemplate = (node.type === 'TemplateLiteral' && node.quasis?.length);
      if (inRegExp &&
        (isTemplate || (typeof node.value === 'string' && node.value.length))) {
        let p = node.parent;
        while (p && p.type === 'BinaryExpression') {
          p = p.parent;
        }
        if (p && (p.type === 'NewExpression' || p.type === 'CallExpression') &&
          p.callee && p.callee.type === 'Identifier' &&
          p.callee.name === 'RegExp') {
          if (isTemplate) {
            const quasis = node.quasis;
            for (let i = 0; i < quasis.length; ++i) {
              const el = quasis[i];
              if (el.type === 'TemplateElement' && el.value?.cooked)
                regexpBuffer.push([el, el.value.cooked]);
            }
          } else {
            regexpBuffer.push([node, node.value]);
          }
        }
      } else if (node.regex) {
        checkRegExp([[node, node.regex.pattern]]);
      }
    }


    return {
      'TemplateLiteral': checkLiteral,
      'Literal': checkLiteral,
      'CallExpression': checkRegExpStart,
      'NewExpression': checkRegExpStart,
      'CallExpression:exit': checkRegExpEnd,
      'NewExpression:exit': checkRegExpEnd,
    };
  },
};
