"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.ArrowFunctionExpression = ArrowFunctionExpression;
exports.AssignmentExpression = AssignmentExpression;
exports.Binary = Binary;
exports.BinaryExpression = BinaryExpression;
exports.ClassExpression = ClassExpression;
exports.ConditionalExpression = ConditionalExpression;
exports.DoExpression = DoExpression;
exports.FunctionExpression = FunctionExpression;
exports.FunctionTypeAnnotation = FunctionTypeAnnotation;
exports.Identifier = Identifier;
exports.LogicalExpression = LogicalExpression;
exports.NullableTypeAnnotation = NullableTypeAnnotation;
exports.ObjectExpression = ObjectExpression;
exports.OptionalIndexedAccessType = OptionalIndexedAccessType;
exports.OptionalCallExpression = exports.OptionalMemberExpression = OptionalMemberExpression;
exports.SequenceExpression = SequenceExpression;
exports.TSSatisfiesExpression = exports.TSAsExpression = TSAsExpression;
exports.TSInferType = TSInferType;
exports.TSInstantiationExpression = TSInstantiationExpression;
exports.UnaryLike = exports.TSTypeAssertion = UnaryLike;
exports.TSIntersectionType = exports.TSUnionType = TSUnionType;
exports.IntersectionTypeAnnotation = exports.UnionTypeAnnotation = UnionTypeAnnotation;
exports.UpdateExpression = UpdateExpression;
exports.AwaitExpression = exports.YieldExpression = YieldExpression;
var _t = require("@babel/types");
var _index = require("./index.js");
const {
  isArrayTypeAnnotation,
  isBinaryExpression,
  isCallExpression,
  isExportDeclaration,
  isForOfStatement,
  isIndexedAccessType,
  isMemberExpression,
  isObjectPattern,
  isOptionalMemberExpression,
  isYieldExpression
} = _t;
const PRECEDENCE = new Map([["||", 0], ["??", 0], ["|>", 0], ["&&", 1], ["|", 2], ["^", 3], ["&", 4], ["==", 5], ["===", 5], ["!=", 5], ["!==", 5], ["<", 6], [">", 6], ["<=", 6], [">=", 6], ["in", 6], ["instanceof", 6], [">>", 7], ["<<", 7], [">>>", 7], ["+", 8], ["-", 8], ["*", 9], ["/", 9], ["%", 9], ["**", 10]]);
function getBinaryPrecedence(node, nodeType) {
  if (nodeType === "BinaryExpression" || nodeType === "LogicalExpression") {
    return PRECEDENCE.get(node.operator);
  }
  if (nodeType === "TSAsExpression" || nodeType === "TSSatisfiesExpression") {
    return PRECEDENCE.get("in");
  }
}
function isTSTypeExpression(nodeType) {
  return nodeType === "TSAsExpression" || nodeType === "TSSatisfiesExpression" || nodeType === "TSTypeAssertion";
}
const isClassExtendsClause = (node, parent) => {
  const parentType = parent.type;
  return (parentType === "ClassDeclaration" || parentType === "ClassExpression") && parent.superClass === node;
};
const hasPostfixPart = (node, parent) => {
  const parentType = parent.type;
  return (parentType === "MemberExpression" || parentType === "OptionalMemberExpression") && parent.object === node || (parentType === "CallExpression" || parentType === "OptionalCallExpression" || parentType === "NewExpression") && parent.callee === node || parentType === "TaggedTemplateExpression" && parent.tag === node || parentType === "TSNonNullExpression";
};
function NullableTypeAnnotation(node, parent) {
  return isArrayTypeAnnotation(parent);
}
function FunctionTypeAnnotation(node, parent, tokenContext) {
  const parentType = parent.type;
  return parentType === "UnionTypeAnnotation" || parentType === "IntersectionTypeAnnotation" || parentType === "ArrayTypeAnnotation" || Boolean(tokenContext & _index.TokenContext.arrowFlowReturnType);
}
function UpdateExpression(node, parent) {
  return hasPostfixPart(node, parent) || isClassExtendsClause(node, parent);
}
function ObjectExpression(node, parent, tokenContext) {
  return Boolean(tokenContext & (_index.TokenContext.expressionStatement | _index.TokenContext.arrowBody));
}
function DoExpression(node, parent, tokenContext) {
  return !node.async && Boolean(tokenContext & _index.TokenContext.expressionStatement);
}
function Binary(node, parent) {
  const parentType = parent.type;
  if (node.type === "BinaryExpression" && node.operator === "**" && parentType === "BinaryExpression" && parent.operator === "**") {
    return parent.left === node;
  }
  if (isClassExtendsClause(node, parent)) {
    return true;
  }
  if (hasPostfixPart(node, parent) || parentType === "UnaryExpression" || parentType === "SpreadElement" || parentType === "AwaitExpression") {
    return true;
  }
  const parentPos = getBinaryPrecedence(parent, parentType);
  if (parentPos != null) {
    const nodePos = getBinaryPrecedence(node, node.type);
    if (parentPos === nodePos && parentType === "BinaryExpression" && parent.right === node || parentPos > nodePos) {
      return true;
    }
  }
  return undefined;
}
function UnionTypeAnnotation(node, parent) {
  const parentType = parent.type;
  return parentType === "ArrayTypeAnnotation" || parentType === "NullableTypeAnnotation" || parentType === "IntersectionTypeAnnotation" || parentType === "UnionTypeAnnotation";
}
function OptionalIndexedAccessType(node, parent) {
  return isIndexedAccessType(parent) && parent.objectType === node;
}
function TSAsExpression(node, parent) {
  if ((parent.type === "AssignmentExpression" || parent.type === "AssignmentPattern") && parent.left === node) {
    return true;
  }
  if (parent.type === "BinaryExpression" && (parent.operator === "|" || parent.operator === "&") && node === parent.left) {
    return true;
  }
  return Binary(node, parent);
}
function TSUnionType(node, parent) {
  const parentType = parent.type;
  return parentType === "TSArrayType" || parentType === "TSOptionalType" || parentType === "TSIntersectionType" || parentType === "TSUnionType" || parentType === "TSRestType";
}
function TSInferType(node, parent) {
  const parentType = parent.type;
  return parentType === "TSArrayType" || parentType === "TSOptionalType";
}
function TSInstantiationExpression(node, parent) {
  const parentType = parent.type;
  return (parentType === "CallExpression" || parentType === "OptionalCallExpression" || parentType === "NewExpression" || parentType === "TSInstantiationExpression") && !!parent.typeParameters;
}
function BinaryExpression(node, parent, tokenContext, inForStatementInit) {
  return node.operator === "in" && inForStatementInit;
}
function SequenceExpression(node, parent) {
  const parentType = parent.type;
  if (parentType === "ForStatement" || parentType === "ThrowStatement" || parentType === "ReturnStatement" || parentType === "IfStatement" && parent.test === node || parentType === "WhileStatement" && parent.test === node || parentType === "ForInStatement" && parent.right === node || parentType === "SwitchStatement" && parent.discriminant === node || parentType === "ExpressionStatement" && parent.expression === node) {
    return false;
  }
  return true;
}
function YieldExpression(node, parent) {
  const parentType = parent.type;
  return parentType === "BinaryExpression" || parentType === "LogicalExpression" || parentType === "UnaryExpression" || parentType === "SpreadElement" || hasPostfixPart(node, parent) || parentType === "AwaitExpression" && isYieldExpression(node) || parentType === "ConditionalExpression" && node === parent.test || isClassExtendsClause(node, parent) || isTSTypeExpression(parentType);
}
function ClassExpression(node, parent, tokenContext) {
  return Boolean(tokenContext & (_index.TokenContext.expressionStatement | _index.TokenContext.exportDefault));
}
function UnaryLike(node, parent) {
  return hasPostfixPart(node, parent) || isBinaryExpression(parent) && parent.operator === "**" && parent.left === node || isClassExtendsClause(node, parent);
}
function FunctionExpression(node, parent, tokenContext) {
  return Boolean(tokenContext & (_index.TokenContext.expressionStatement | _index.TokenContext.exportDefault));
}
function ArrowFunctionExpression(node, parent) {
  return isExportDeclaration(parent) || ConditionalExpression(node, parent);
}
function ConditionalExpression(node, parent) {
  const parentType = parent.type;
  if (parentType === "UnaryExpression" || parentType === "SpreadElement" || parentType === "BinaryExpression" || parentType === "LogicalExpression" || parentType === "ConditionalExpression" && parent.test === node || parentType === "AwaitExpression" || isTSTypeExpression(parentType)) {
    return true;
  }
  return UnaryLike(node, parent);
}
function OptionalMemberExpression(node, parent) {
  return isCallExpression(parent) && parent.callee === node || isMemberExpression(parent) && parent.object === node;
}
function AssignmentExpression(node, parent) {
  if (isObjectPattern(node.left)) {
    return true;
  } else {
    return ConditionalExpression(node, parent);
  }
}
function LogicalExpression(node, parent) {
  const parentType = parent.type;
  if (isTSTypeExpression(parentType)) return true;
  if (parentType !== "LogicalExpression") return false;
  switch (node.operator) {
    case "||":
      return parent.operator === "??" || parent.operator === "&&";
    case "&&":
      return parent.operator === "??";
    case "??":
      return parent.operator !== "??";
  }
}
function Identifier(node, parent, tokenContext) {
  var _node$extra;
  const parentType = parent.type;
  if ((_node$extra = node.extra) != null && _node$extra.parenthesized && parentType === "AssignmentExpression" && parent.left === node) {
    const rightType = parent.right.type;
    if ((rightType === "FunctionExpression" || rightType === "ClassExpression") && parent.right.id == null) {
      return true;
    }
  }
  if (node.name === "let") {
    const isFollowedByBracket = isMemberExpression(parent, {
      object: node,
      computed: true
    }) || isOptionalMemberExpression(parent, {
      object: node,
      computed: true,
      optional: false
    });
    if (isFollowedByBracket && tokenContext & (_index.TokenContext.expressionStatement | _index.TokenContext.forHead | _index.TokenContext.forInHead)) {
      return true;
    }
    return Boolean(tokenContext & _index.TokenContext.forOfHead);
  }
  return node.name === "async" && isForOfStatement(parent, {
    left: node,
    await: false
  });
}

//# sourceMappingURL=parentheses.js.map
