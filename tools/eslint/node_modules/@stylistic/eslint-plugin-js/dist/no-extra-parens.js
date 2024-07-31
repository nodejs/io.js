'use strict';

var utils = require('./utils.js');

var noExtraParens = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Disallow unnecessary parentheses",
      url: "https://eslint.style/rules/js/no-extra-parens"
    },
    fixable: "code",
    schema: {
      anyOf: [
        {
          type: "array",
          items: [
            {
              type: "string",
              enum: ["functions"]
            }
          ],
          minItems: 0,
          maxItems: 1
        },
        {
          type: "array",
          items: [
            {
              type: "string",
              enum: ["all"]
            },
            {
              type: "object",
              properties: {
                conditionalAssign: { type: "boolean" },
                ternaryOperandBinaryExpressions: { type: "boolean" },
                nestedBinaryExpressions: { type: "boolean" },
                returnAssign: { type: "boolean" },
                ignoreJSX: { type: "string", enum: ["none", "all", "single-line", "multi-line"] },
                enforceForArrowConditionals: { type: "boolean" },
                enforceForSequenceExpressions: { type: "boolean" },
                enforceForNewInMemberExpressions: { type: "boolean" },
                enforceForFunctionPrototypeMethods: { type: "boolean" },
                allowParensAfterCommentPattern: { type: "string" }
              },
              additionalProperties: false
            }
          ],
          minItems: 0,
          maxItems: 2
        }
      ]
    },
    messages: {
      unexpected: "Unnecessary parentheses around expression."
    }
  },
  create(context) {
    const sourceCode = context.sourceCode;
    const tokensToIgnore = /* @__PURE__ */ new WeakSet();
    const precedence = utils.getPrecedence;
    const ALL_NODES = context.options[0] !== "functions";
    const EXCEPT_COND_ASSIGN = ALL_NODES && context.options[1] && context.options[1].conditionalAssign === false;
    const EXCEPT_COND_TERNARY = ALL_NODES && context.options[1] && context.options[1].ternaryOperandBinaryExpressions === false;
    const NESTED_BINARY = ALL_NODES && context.options[1] && context.options[1].nestedBinaryExpressions === false;
    const EXCEPT_RETURN_ASSIGN = ALL_NODES && context.options[1] && context.options[1].returnAssign === false;
    const IGNORE_JSX = ALL_NODES && context.options[1] && context.options[1].ignoreJSX;
    const IGNORE_ARROW_CONDITIONALS = ALL_NODES && context.options[1] && context.options[1].enforceForArrowConditionals === false;
    const IGNORE_SEQUENCE_EXPRESSIONS = ALL_NODES && context.options[1] && context.options[1].enforceForSequenceExpressions === false;
    const IGNORE_NEW_IN_MEMBER_EXPR = ALL_NODES && context.options[1] && context.options[1].enforceForNewInMemberExpressions === false;
    const IGNORE_FUNCTION_PROTOTYPE_METHODS = ALL_NODES && context.options[1] && context.options[1].enforceForFunctionPrototypeMethods === false;
    const ALLOW_PARENS_AFTER_COMMENT_PATTERN = ALL_NODES && context.options[1] && context.options[1].allowParensAfterCommentPattern;
    const PRECEDENCE_OF_ASSIGNMENT_EXPR = precedence({ type: "AssignmentExpression" });
    const PRECEDENCE_OF_UPDATE_EXPR = precedence({ type: "UpdateExpression" });
    let reportsBuffer;
    function isImmediateFunctionPrototypeMethodCall(node) {
      const callNode = utils.skipChainExpression(node);
      if (callNode.type !== "CallExpression")
        return false;
      const callee = utils.skipChainExpression(callNode.callee);
      return callee.type === "MemberExpression" && callee.object.type === "FunctionExpression" && ["call", "apply"].includes(utils.getStaticPropertyName(callee));
    }
    function ruleApplies(node) {
      if (node.type === "JSXElement" || node.type === "JSXFragment") {
        const isSingleLine = node.loc.start.line === node.loc.end.line;
        switch (IGNORE_JSX) {
          case "all":
            return false;
          case "multi-line":
            return isSingleLine;
          case "single-line":
            return !isSingleLine;
        }
      }
      if (node.type === "SequenceExpression" && IGNORE_SEQUENCE_EXPRESSIONS)
        return false;
      if (isImmediateFunctionPrototypeMethodCall(node) && IGNORE_FUNCTION_PROTOTYPE_METHODS)
        return false;
      return ALL_NODES || node.type === "FunctionExpression" || node.type === "ArrowFunctionExpression";
    }
    function isParenthesised(node) {
      return utils.isParenthesized(1, node, sourceCode);
    }
    function isParenthesisedTwice(node) {
      return utils.isParenthesized(2, node, sourceCode);
    }
    function hasExcessParens(node) {
      return ruleApplies(node) && isParenthesised(node);
    }
    function hasDoubleExcessParens(node) {
      return ruleApplies(node) && isParenthesisedTwice(node);
    }
    function hasExcessParensWithPrecedence(node, precedenceLowerLimit) {
      if (ruleApplies(node) && isParenthesised(node)) {
        if (precedence(node) >= precedenceLowerLimit || isParenthesisedTwice(node)) {
          return true;
        }
      }
      return false;
    }
    function isCondAssignException(node) {
      return EXCEPT_COND_ASSIGN && node.test && node.test.type === "AssignmentExpression";
    }
    function isInReturnStatement(node) {
      for (let currentNode = node; currentNode; currentNode = currentNode.parent) {
        if (currentNode.type === "ReturnStatement" || currentNode.type === "ArrowFunctionExpression" && currentNode.body.type !== "BlockStatement") {
          return true;
        }
      }
      return false;
    }
    function isNewExpressionWithParens(newExpression) {
      const lastToken = sourceCode.getLastToken(newExpression);
      const penultimateToken = sourceCode.getTokenBefore(lastToken);
      return newExpression.arguments.length > 0 || // The expression should end with its own parens, e.g., new new foo() is not a new expression with parens
      utils.isOpeningParenToken(penultimateToken) && utils.isClosingParenToken(lastToken) && newExpression.callee.range[1] < newExpression.range[1];
    }
    function containsAssignment(node) {
      if (node.type === "AssignmentExpression")
        return true;
      if (node.type === "ConditionalExpression" && (node.consequent.type === "AssignmentExpression" || node.alternate.type === "AssignmentExpression")) {
        return true;
      }
      if ("left" in node && (node.left && node.left.type === "AssignmentExpression" || node.right && node.right.type === "AssignmentExpression")) {
        return true;
      }
      return false;
    }
    function isReturnAssignException(node) {
      if (!EXCEPT_RETURN_ASSIGN || !isInReturnStatement(node))
        return false;
      if (node.type === "ReturnStatement")
        return node.argument && containsAssignment(node.argument);
      if (node.type === "ArrowFunctionExpression" && node.body.type !== "BlockStatement")
        return containsAssignment(node.body);
      return containsAssignment(node);
    }
    function hasExcessParensNoLineTerminator(token, node) {
      if (token.loc.end.line === node.loc.start.line)
        return hasExcessParens(node);
      return hasDoubleExcessParens(node);
    }
    function requiresLeadingSpace(node) {
      const leftParenToken = sourceCode.getTokenBefore(node);
      const tokenBeforeLeftParen = sourceCode.getTokenBefore(leftParenToken, { includeComments: true });
      const tokenAfterLeftParen = sourceCode.getTokenAfter(leftParenToken, { includeComments: true });
      return tokenBeforeLeftParen && tokenBeforeLeftParen.range[1] === leftParenToken.range[0] && leftParenToken.range[1] === tokenAfterLeftParen.range[0] && !utils.canTokensBeAdjacent(tokenBeforeLeftParen, tokenAfterLeftParen);
    }
    function requiresTrailingSpace(node) {
      const nextTwoTokens = sourceCode.getTokensAfter(node, { count: 2 });
      const rightParenToken = nextTwoTokens[0];
      const tokenAfterRightParen = nextTwoTokens[1];
      const tokenBeforeRightParen = sourceCode.getLastToken(node);
      return rightParenToken && tokenAfterRightParen && !sourceCode.isSpaceBetweenTokens(rightParenToken, tokenAfterRightParen) && !utils.canTokensBeAdjacent(tokenBeforeRightParen, tokenAfterRightParen);
    }
    function isIIFE(node) {
      const maybeCallNode = utils.skipChainExpression(node);
      return maybeCallNode.type === "CallExpression" && maybeCallNode.callee.type === "FunctionExpression";
    }
    function canBeAssignmentTarget(node) {
      return node && (node.type === "Identifier" || node.type === "MemberExpression");
    }
    function isFixable(node) {
      if (node.type !== "Literal" || typeof node.value !== "string")
        return true;
      if (isParenthesisedTwice(node))
        return true;
      return !utils.isTopLevelExpressionStatement(node.parent);
    }
    function report(node) {
      const leftParenToken = sourceCode.getTokenBefore(node);
      const rightParenToken = sourceCode.getTokenAfter(node);
      if (!isParenthesisedTwice(node)) {
        if (tokensToIgnore.has(sourceCode.getFirstToken(node)))
          return;
        if (isIIFE(node) && !("callee" in node && isParenthesised(node.callee)))
          return;
        if (ALLOW_PARENS_AFTER_COMMENT_PATTERN) {
          const commentsBeforeLeftParenToken = sourceCode.getCommentsBefore(leftParenToken);
          const totalCommentsBeforeLeftParenTokenCount = commentsBeforeLeftParenToken.length;
          const ignorePattern = new RegExp(ALLOW_PARENS_AFTER_COMMENT_PATTERN, "u");
          if (totalCommentsBeforeLeftParenTokenCount > 0 && ignorePattern.test(commentsBeforeLeftParenToken[totalCommentsBeforeLeftParenTokenCount - 1].value)) {
            return;
          }
        }
      }
      function finishReport() {
        context.report({
          node,
          loc: leftParenToken.loc,
          messageId: "unexpected",
          fix: isFixable(node) ? (fixer) => {
            const parenthesizedSource = sourceCode.text.slice(leftParenToken.range[1], rightParenToken.range[0]);
            return fixer.replaceTextRange([
              leftParenToken.range[0],
              rightParenToken.range[1]
            ], (requiresLeadingSpace(node) ? " " : "") + parenthesizedSource + (requiresTrailingSpace(node) ? " " : ""));
          } : null
        });
      }
      if (reportsBuffer) {
        reportsBuffer.reports.push({ node, finishReport });
        return;
      }
      finishReport();
    }
    function checkArgumentWithPrecedence(node) {
      if ("argument" in node && node.argument && hasExcessParensWithPrecedence(node.argument, precedence(node)))
        report(node.argument);
    }
    function doesMemberExpressionContainCallExpression(node) {
      let currentNode = node.object;
      let currentNodeType = node.object.type;
      while (currentNodeType === "MemberExpression") {
        if (!("object" in currentNode))
          break;
        currentNode = currentNode.object;
        currentNodeType = currentNode.type;
      }
      return currentNodeType === "CallExpression";
    }
    function checkCallNew(node) {
      const callee = node.callee;
      if (hasExcessParensWithPrecedence(callee, precedence(node))) {
        if (hasDoubleExcessParens(callee) || !(isIIFE(node) || callee.type === "NewExpression" && !isNewExpressionWithParens(callee) && !(node.type === "NewExpression" && !isNewExpressionWithParens(node)) || node.type === "NewExpression" && callee.type === "MemberExpression" && doesMemberExpressionContainCallExpression(callee) || (!("optional" in node) || !node.optional) && callee.type === "ChainExpression")) {
          report(node.callee);
        }
      }
      node.arguments.filter((arg) => hasExcessParensWithPrecedence(arg, PRECEDENCE_OF_ASSIGNMENT_EXPR)).forEach(report);
    }
    function checkBinaryLogical(node) {
      const prec = precedence(node);
      const leftPrecedence = precedence(node.left);
      const rightPrecedence = precedence(node.right);
      const isExponentiation = node.operator === "**";
      const shouldSkipLeft = NESTED_BINARY && (node.left.type === "BinaryExpression" || node.left.type === "LogicalExpression");
      const shouldSkipRight = NESTED_BINARY && (node.right.type === "BinaryExpression" || node.right.type === "LogicalExpression");
      if (!shouldSkipLeft && hasExcessParens(node.left)) {
        if (!(["AwaitExpression", "UnaryExpression"].includes(node.left.type) && isExponentiation) && !utils.isMixedLogicalAndCoalesceExpressions(node.left, node) && (leftPrecedence > prec || leftPrecedence === prec && !isExponentiation) || isParenthesisedTwice(node.left)) {
          report(node.left);
        }
      }
      if (!shouldSkipRight && hasExcessParens(node.right)) {
        if (!utils.isMixedLogicalAndCoalesceExpressions(node.right, node) && (rightPrecedence > prec || rightPrecedence === prec && isExponentiation) || isParenthesisedTwice(node.right)) {
          report(node.right);
        }
      }
    }
    function checkClass(node) {
      if (!node.superClass)
        return;
      const hasExtraParens = precedence(node.superClass) > PRECEDENCE_OF_UPDATE_EXPR ? hasExcessParens(node.superClass) : hasDoubleExcessParens(node.superClass);
      if (hasExtraParens)
        report(node.superClass);
    }
    function checkSpreadOperator(node) {
      if (hasExcessParensWithPrecedence(node.argument, PRECEDENCE_OF_ASSIGNMENT_EXPR))
        report(node.argument);
    }
    function checkExpressionOrExportStatement(node) {
      const firstToken = isParenthesised(node) ? sourceCode.getTokenBefore(node) : sourceCode.getFirstToken(node);
      const secondToken = sourceCode.getTokenAfter(firstToken, utils.isNotOpeningParenToken);
      const thirdToken = secondToken ? sourceCode.getTokenAfter(secondToken) : null;
      const tokenAfterClosingParens = secondToken ? sourceCode.getTokenAfter(secondToken, utils.isNotClosingParenToken) : null;
      if (utils.isOpeningParenToken(firstToken) && (utils.isOpeningBraceToken(secondToken) || secondToken.type === "Keyword" && (secondToken.value === "function" || secondToken.value === "class" || secondToken.value === "let" && tokenAfterClosingParens && (utils.isOpeningBracketToken(tokenAfterClosingParens) || tokenAfterClosingParens.type === "Identifier")) || secondToken && secondToken.type === "Identifier" && secondToken.value === "async" && thirdToken && thirdToken.type === "Keyword" && thirdToken.value === "function")) {
        tokensToIgnore.add(secondToken);
      }
      const hasExtraParens = node.parent.type === "ExportDefaultDeclaration" ? hasExcessParensWithPrecedence(node, PRECEDENCE_OF_ASSIGNMENT_EXPR) : hasExcessParens(node);
      if (hasExtraParens)
        report(node);
    }
    function pathToAncestor(node, ancestor) {
      const path = [node];
      let currentNode = node;
      while (currentNode !== ancestor) {
        currentNode = currentNode.parent;
        if (currentNode === null || currentNode === void 0)
          throw new Error("Nodes are not in the ancestor-descendant relationship.");
        path.push(currentNode);
      }
      return path;
    }
    function pathToDescendant(node, descendant) {
      return pathToAncestor(descendant, node).reverse();
    }
    function isSafelyEnclosingInExpression(node, child) {
      switch (node.type) {
        case "ArrayExpression":
        case "ArrayPattern":
        case "BlockStatement":
        case "ObjectExpression":
        case "ObjectPattern":
        case "TemplateLiteral":
          return true;
        case "ArrowFunctionExpression":
        case "FunctionExpression":
          return node.params.includes(child);
        case "CallExpression":
        case "NewExpression":
          return node.arguments.includes(child);
        case "MemberExpression":
          return node.computed && node.property === child;
        case "ConditionalExpression":
          return node.consequent === child;
        default:
          return false;
      }
    }
    function startNewReportsBuffering() {
      reportsBuffer = {
        upper: reportsBuffer,
        inExpressionNodes: [],
        reports: []
      };
    }
    function endCurrentReportsBuffering() {
      const { upper, inExpressionNodes, reports } = reportsBuffer ?? {};
      if (upper) {
        upper.inExpressionNodes.push(...inExpressionNodes ?? []);
        upper.reports.push(...reports ?? []);
      } else {
        reports?.forEach(({ finishReport }) => finishReport());
      }
      reportsBuffer = upper;
    }
    function isInCurrentReportsBuffer(node) {
      return reportsBuffer?.reports.some((r) => r.node === node);
    }
    function removeFromCurrentReportsBuffer(node) {
      if (reportsBuffer)
        reportsBuffer.reports = reportsBuffer.reports.filter((r) => r.node !== node);
    }
    function isMemberExpInNewCallee(node) {
      if (node.type === "MemberExpression") {
        return node.parent.type === "NewExpression" && node.parent.callee === node ? true : "object" in node.parent && node.parent.object === node && isMemberExpInNewCallee(node.parent);
      }
      return false;
    }
    function isAnonymousFunctionAssignmentException({ left, operator, right }) {
      if (left.type === "Identifier" && ["=", "&&=", "||=", "??="].includes(operator)) {
        const rhsType = right.type;
        if (rhsType === "ArrowFunctionExpression")
          return true;
        if ((rhsType === "FunctionExpression" || rhsType === "ClassExpression") && !right.id)
          return true;
      }
      return false;
    }
    return {
      ArrayExpression(node) {
        node.elements.filter((e) => !!e && hasExcessParensWithPrecedence(e, PRECEDENCE_OF_ASSIGNMENT_EXPR)).forEach(report);
      },
      ArrayPattern(node) {
        node.elements.filter((e) => e && canBeAssignmentTarget(e) && hasExcessParens(e)).forEach(report);
      },
      ArrowFunctionExpression(node) {
        if (isReturnAssignException(node))
          return;
        if (node.body.type === "ConditionalExpression" && IGNORE_ARROW_CONDITIONALS) {
          return;
        }
        if (node.body.type !== "BlockStatement") {
          const firstBodyToken = sourceCode.getFirstToken(node.body, utils.isNotOpeningParenToken);
          const tokenBeforeFirst = sourceCode.getTokenBefore(firstBodyToken);
          if (utils.isOpeningParenToken(tokenBeforeFirst) && utils.isOpeningBraceToken(firstBodyToken))
            tokensToIgnore.add(firstBodyToken);
          if (hasExcessParensWithPrecedence(node.body, PRECEDENCE_OF_ASSIGNMENT_EXPR))
            report(node.body);
        }
      },
      AssignmentExpression(node) {
        if (canBeAssignmentTarget(node.left) && hasExcessParens(node.left) && (!isAnonymousFunctionAssignmentException(node) || isParenthesisedTwice(node.left))) {
          report(node.left);
        }
        if (!isReturnAssignException(node) && hasExcessParensWithPrecedence(node.right, precedence(node)))
          report(node.right);
      },
      BinaryExpression(node) {
        if (reportsBuffer && node.operator === "in")
          reportsBuffer.inExpressionNodes.push(node);
        checkBinaryLogical(node);
      },
      "CallExpression": checkCallNew,
      ConditionalExpression(node) {
        if (isReturnAssignException(node))
          return;
        const availableTypes = /* @__PURE__ */ new Set(["BinaryExpression", "LogicalExpression"]);
        if (!(EXCEPT_COND_TERNARY && availableTypes.has(node.test.type)) && !isCondAssignException(node) && hasExcessParensWithPrecedence(node.test, precedence({ type: "LogicalExpression", operator: "||" }))) {
          report(node.test);
        }
        if (!(EXCEPT_COND_TERNARY && availableTypes.has(node.consequent.type)) && hasExcessParensWithPrecedence(node.consequent, PRECEDENCE_OF_ASSIGNMENT_EXPR)) {
          report(node.consequent);
        }
        if (!(EXCEPT_COND_TERNARY && availableTypes.has(node.alternate.type)) && hasExcessParensWithPrecedence(node.alternate, PRECEDENCE_OF_ASSIGNMENT_EXPR)) {
          report(node.alternate);
        }
      },
      DoWhileStatement(node) {
        if (hasExcessParens(node.test) && !isCondAssignException(node))
          report(node.test);
      },
      "ExportDefaultDeclaration": (node) => checkExpressionOrExportStatement(node.declaration),
      "ExpressionStatement": (node) => checkExpressionOrExportStatement(node.expression),
      ForInStatement(node) {
        if (node.left.type !== "VariableDeclaration") {
          const firstLeftToken = sourceCode.getFirstToken(node.left, utils.isNotOpeningParenToken);
          if (firstLeftToken.value === "let" && utils.isOpeningBracketToken(
            sourceCode.getTokenAfter(firstLeftToken, utils.isNotClosingParenToken)
          )) {
            tokensToIgnore.add(firstLeftToken);
          }
        }
        if (hasExcessParens(node.left))
          report(node.left);
        if (hasExcessParens(node.right))
          report(node.right);
      },
      ForOfStatement(node) {
        if (node.left.type !== "VariableDeclaration") {
          const firstLeftToken = sourceCode.getFirstToken(node.left, utils.isNotOpeningParenToken);
          if (firstLeftToken.value === "let") {
            tokensToIgnore.add(firstLeftToken);
          }
        }
        if (hasExcessParens(node.left))
          report(node.left);
        if (hasExcessParensWithPrecedence(node.right, PRECEDENCE_OF_ASSIGNMENT_EXPR))
          report(node.right);
      },
      ForStatement(node) {
        if (node.test && hasExcessParens(node.test) && !isCondAssignException(node))
          report(node.test);
        if (node.update && hasExcessParens(node.update))
          report(node.update);
        if (node.init) {
          if (node.init.type !== "VariableDeclaration") {
            const firstToken = sourceCode.getFirstToken(node.init, utils.isNotOpeningParenToken);
            if (firstToken.value === "let" && utils.isOpeningBracketToken(
              sourceCode.getTokenAfter(firstToken, utils.isNotClosingParenToken)
            )) {
              tokensToIgnore.add(firstToken);
            }
          }
          startNewReportsBuffering();
          if (hasExcessParens(node.init))
            report(node.init);
        }
      },
      "ForStatement > *.init:exit": function(node) {
        if (reportsBuffer?.reports.length) {
          reportsBuffer.inExpressionNodes.forEach((inExpressionNode) => {
            const path = pathToDescendant(node, inExpressionNode);
            let nodeToExclude = null;
            for (let i = 0; i < path.length; i++) {
              const pathNode = path[i];
              if (i < path.length - 1) {
                const nextPathNode = path[i + 1];
                if (isSafelyEnclosingInExpression(pathNode, nextPathNode)) {
                  return;
                }
              }
              if (isParenthesised(pathNode)) {
                if (isInCurrentReportsBuffer(pathNode)) {
                  if (isParenthesisedTwice(pathNode)) {
                    return;
                  }
                  if (!nodeToExclude)
                    nodeToExclude = pathNode;
                } else {
                  return;
                }
              }
            }
            if (nodeToExclude)
              removeFromCurrentReportsBuffer(nodeToExclude);
          });
        }
        endCurrentReportsBuffering();
      },
      IfStatement(node) {
        if (hasExcessParens(node.test) && !isCondAssignException(node))
          report(node.test);
      },
      ImportExpression(node) {
        const { source } = node;
        if (source.type === "SequenceExpression") {
          if (hasDoubleExcessParens(source))
            report(source);
        } else if (hasExcessParens(source)) {
          report(source);
        }
      },
      "LogicalExpression": checkBinaryLogical,
      MemberExpression(node) {
        const shouldAllowWrapOnce = isMemberExpInNewCallee(node) && doesMemberExpressionContainCallExpression(node);
        const nodeObjHasExcessParens = shouldAllowWrapOnce ? hasDoubleExcessParens(node.object) : hasExcessParens(node.object) && !(isImmediateFunctionPrototypeMethodCall(node.parent) && "callee" in node.parent && node.parent.callee === node && IGNORE_FUNCTION_PROTOTYPE_METHODS);
        if (nodeObjHasExcessParens && precedence(node.object) >= precedence(node) && (node.computed || !(utils.isDecimalInteger(node.object) || node.object.type === "Literal" && "regex" in node.object && node.object.regex))) {
          report(node.object);
        }
        if (nodeObjHasExcessParens && node.object.type === "CallExpression") {
          report(node.object);
        }
        if (nodeObjHasExcessParens && !IGNORE_NEW_IN_MEMBER_EXPR && node.object.type === "NewExpression" && isNewExpressionWithParens(node.object)) {
          report(node.object);
        }
        if (nodeObjHasExcessParens && node.optional && node.object.type === "ChainExpression") {
          report(node.object);
        }
        if (node.computed && hasExcessParens(node.property))
          report(node.property);
      },
      "MethodDefinition[computed=true]": function(node) {
        if (hasExcessParensWithPrecedence(node.key, PRECEDENCE_OF_ASSIGNMENT_EXPR))
          report(node.key);
      },
      "NewExpression": checkCallNew,
      ObjectExpression(node) {
        node.properties.filter((property) => property.type === "Property" && property.value && hasExcessParensWithPrecedence(property.value, PRECEDENCE_OF_ASSIGNMENT_EXPR)).forEach((property) => report(property.value));
      },
      ObjectPattern(node) {
        node.properties.filter((property) => {
          const value = property.value;
          return value && canBeAssignmentTarget(value) && hasExcessParens(value);
        }).forEach((property) => report(property.value));
      },
      Property(node) {
        if (node.computed) {
          const { key } = node;
          if (key && hasExcessParensWithPrecedence(key, PRECEDENCE_OF_ASSIGNMENT_EXPR))
            report(key);
        }
      },
      PropertyDefinition(node) {
        if (node.computed && hasExcessParensWithPrecedence(node.key, PRECEDENCE_OF_ASSIGNMENT_EXPR))
          report(node.key);
        if (node.value && hasExcessParensWithPrecedence(node.value, PRECEDENCE_OF_ASSIGNMENT_EXPR))
          report(node.value);
      },
      RestElement(node) {
        const argument = node.argument;
        if (canBeAssignmentTarget(argument) && hasExcessParens(argument))
          report(argument);
      },
      ReturnStatement(node) {
        const returnToken = sourceCode.getFirstToken(node);
        if (isReturnAssignException(node))
          return;
        if (node.argument && returnToken && hasExcessParensNoLineTerminator(returnToken, node.argument) && !(node.argument.type === "Literal" && "regex" in node.argument && node.argument.regex)) {
          report(node.argument);
        }
      },
      SequenceExpression(node) {
        const precedenceOfNode = precedence(node);
        node.expressions.filter((e) => hasExcessParensWithPrecedence(e, precedenceOfNode)).forEach(report);
      },
      SwitchCase(node) {
        if (node.test && hasExcessParens(node.test))
          report(node.test);
      },
      SwitchStatement(node) {
        if (hasExcessParens(node.discriminant))
          report(node.discriminant);
      },
      ThrowStatement(node) {
        const throwToken = sourceCode.getFirstToken(node);
        if (throwToken && node.argument && hasExcessParensNoLineTerminator(throwToken, node.argument))
          report(node.argument);
      },
      "UnaryExpression": checkArgumentWithPrecedence,
      UpdateExpression(node) {
        if (node.prefix) {
          checkArgumentWithPrecedence(node);
        } else {
          const { argument } = node;
          const operatorToken = sourceCode.getLastToken(node);
          if (argument.loc.end.line === operatorToken.loc.start.line) {
            checkArgumentWithPrecedence(node);
          } else {
            if (hasDoubleExcessParens(argument))
              report(argument);
          }
        }
      },
      "AwaitExpression": checkArgumentWithPrecedence,
      VariableDeclarator(node) {
        if (node.init && hasExcessParensWithPrecedence(node.init, PRECEDENCE_OF_ASSIGNMENT_EXPR) && !(node.init.type === "Literal" && "regex" in node.init && node.init.regex)) {
          report(node.init);
        }
      },
      WhileStatement(node) {
        if (hasExcessParens(node.test) && !isCondAssignException(node))
          report(node.test);
      },
      WithStatement(node) {
        if (hasExcessParens(node.object))
          report(node.object);
      },
      YieldExpression(node) {
        if (node.argument) {
          const yieldToken = sourceCode.getFirstToken(node);
          if (precedence(node.argument) >= precedence(node) && yieldToken && hasExcessParensNoLineTerminator(yieldToken, node.argument) || hasDoubleExcessParens(node.argument)) {
            report(node.argument);
          }
        }
      },
      "ClassDeclaration": checkClass,
      "ClassExpression": checkClass,
      "SpreadElement": checkSpreadOperator,
      "SpreadProperty": checkSpreadOperator,
      TemplateLiteral(node) {
        node.expressions.filter((e) => e && hasExcessParens(e)).forEach(report);
      },
      AssignmentPattern(node) {
        const { left, right } = node;
        if (canBeAssignmentTarget(left) && hasExcessParens(left))
          report(left);
        if (right && hasExcessParensWithPrecedence(right, PRECEDENCE_OF_ASSIGNMENT_EXPR))
          report(right);
      },
      // This listener is exposed for TypeScript rule to consume
      TSStringKeyword(node) {
        if (hasExcessParens(node))
          report(node);
      }
    };
  }
});

exports.noExtraParens = noExtraParens;
