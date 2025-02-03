/**
 * @fileoverview Iterating over arrays should be avoided because it relies on
 *               user-mutable global methods (`Array.prototype[Symbol.iterator]`
 *               and `%ArrayIteratorPrototype%.next`), we should instead use
 *               other alternatives. This file defines a rule that disallow
 *               array destructuring syntax in favor of object destructuring
 *               syntax. Note that you can ignore this rule if you are using
 *               the array destructuring syntax over a safe iterable, or
 *               actually want to iterate over a user-provided object.
 * @author aduh95 <duhamelantoine1995@gmail.com>
 */
'use strict';

//------------------------------------------------------------------------------
// Rule Definition
//------------------------------------------------------------------------------

const USE_OBJ_DESTRUCTURING =
  'Use object destructuring instead of array destructuring.';
const USE_ARRAY_METHODS =
  'Use primordials.ArrayPrototypeSlice to avoid unsafe array iteration.';

const findComma = (sourceCode, elements, i, start) => {
  if (i === 0)
    return sourceCode.getTokenAfter(sourceCode.getTokenByRangeStart(start));

  let element;
  const originalIndex = i;
  while (i && !element) {
    element = elements[--i];
  }
  let token = sourceCode.getTokenAfter(
    element ?? sourceCode.getTokenByRangeStart(start),
  );
  for (; i < originalIndex; i++) {
    token = sourceCode.getTokenAfter(token);
  }
  return token;
};
const createFix = (fixer, sourceCode, { range: [start, end], elements }) => [
  fixer.replaceTextRange([start, start + 1], '{'),
  fixer.replaceTextRange([end - 1, end], '}'),
  ...elements.map((node, i) =>
    (node === null ?
      fixer.remove(findComma(sourceCode, elements, i, start)) :
      fixer.insertTextBefore(node, i + ':')),
  ),
];
const arrayPatternContainsRestOperator = ({ elements }) =>
  elements?.find((node) => node?.type === 'RestElement');

module.exports = {
  meta: {
    type: 'suggestion',
    fixable: 'code',
    schema: [],
  },
  create(context) {
    const sourceCode = context.sourceCode;

    return {
      ArrayPattern(node) {
        const hasRest = arrayPatternContainsRestOperator(node);
        context.report({
          message: hasRest ? USE_ARRAY_METHODS : USE_OBJ_DESTRUCTURING,
          node: hasRest || node,
          fix: hasRest ?
            undefined :
            (fixer) => [
              ...(node.parent.type === 'AssignmentExpression' ?
                [
                  fixer.insertTextBefore(node.parent, '('),
                  fixer.insertTextAfter(node.parent, ')'),
                ] :
                []),
              ...createFix(fixer, sourceCode, node),
            ],
        });

      },
    };
  },
};
