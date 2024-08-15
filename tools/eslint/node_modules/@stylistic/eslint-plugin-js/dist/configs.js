'use strict';

var arrayBracketNewline = require('./array-bracket-newline.js');
var arrayBracketSpacing = require('./array-bracket-spacing.js');
var arrayElementNewline = require('./array-element-newline.js');
var arrowParens = require('./arrow-parens.js');
var arrowSpacing = require('./arrow-spacing.js');
var blockSpacing = require('./block-spacing.js');
var braceStyle = require('./brace-style.js');
var commaDangle = require('./comma-dangle.js');
var commaSpacing = require('./comma-spacing.js');
var commaStyle = require('./comma-style.js');
var computedPropertySpacing = require('./computed-property-spacing.js');
var dotLocation = require('./dot-location.js');
var eolLast = require('./eol-last.js');
var functionCallArgumentNewline = require('./function-call-argument-newline.js');
var functionCallSpacing = require('./function-call-spacing.js');
var functionParenNewline = require('./function-paren-newline.js');
var generatorStarSpacing = require('./generator-star-spacing.js');
var implicitArrowLinebreak = require('./implicit-arrow-linebreak.js');
var indent = require('./indent.js');
var jsxQuotes = require('./jsx-quotes.js');
var keySpacing = require('./key-spacing.js');
var keywordSpacing = require('./keyword-spacing.js');
var lineCommentPosition = require('./line-comment-position.js');
var linebreakStyle = require('./linebreak-style.js');
var linesAroundComment = require('./lines-around-comment.js');
var linesBetweenClassMembers = require('./lines-between-class-members.js');
var maxLen = require('./max-len.js');
var maxStatementsPerLine = require('./max-statements-per-line.js');
var multilineCommentStyle = require('./multiline-comment-style.js');
var multilineTernary = require('./multiline-ternary.js');
var newParens = require('./new-parens.js');
var newlinePerChainedCall = require('./newline-per-chained-call.js');
var noConfusingArrow = require('./no-confusing-arrow.js');
var noExtraParens = require('./no-extra-parens.js');
var noExtraSemi = require('./no-extra-semi.js');
var noFloatingDecimal = require('./no-floating-decimal.js');
var noMixedOperators = require('./no-mixed-operators.js');
var noMixedSpacesAndTabs = require('./no-mixed-spaces-and-tabs.js');
var noMultiSpaces = require('./no-multi-spaces.js');
var noMultipleEmptyLines = require('./no-multiple-empty-lines.js');
var noTabs = require('./no-tabs.js');
var noTrailingSpaces = require('./no-trailing-spaces.js');
var noWhitespaceBeforeProperty = require('./no-whitespace-before-property.js');
var nonblockStatementBodyPosition = require('./nonblock-statement-body-position.js');
var objectCurlyNewline = require('./object-curly-newline.js');
var objectCurlySpacing = require('./object-curly-spacing.js');
var objectPropertyNewline = require('./object-property-newline.js');
var oneVarDeclarationPerLine = require('./one-var-declaration-per-line.js');
var operatorLinebreak = require('./operator-linebreak.js');
var paddedBlocks = require('./padded-blocks.js');
var paddingLineBetweenStatements = require('./padding-line-between-statements.js');
var quoteProps = require('./quote-props.js');
var quotes = require('./quotes.js');
var restSpreadSpacing = require('./rest-spread-spacing.js');
var semi = require('./semi.js');
var semiSpacing = require('./semi-spacing.js');
var semiStyle = require('./semi-style.js');
var spaceBeforeBlocks = require('./space-before-blocks.js');
var spaceBeforeFunctionParen = require('./space-before-function-paren.js');
var spaceInParens = require('./space-in-parens.js');
var spaceInfixOps = require('./space-infix-ops.js');
var spaceUnaryOps = require('./space-unary-ops.js');
var spacedComment = require('./spaced-comment.js');
var switchColonSpacing = require('./switch-colon-spacing.js');
var templateCurlySpacing = require('./template-curly-spacing.js');
var templateTagSpacing = require('./template-tag-spacing.js');
var wrapIife = require('./wrap-iife.js');
var wrapRegex = require('./wrap-regex.js');
var yieldStarSpacing = require('./yield-star-spacing.js');

function createAllConfigs(plugin, name, flat, filter) {
  const rules = Object.fromEntries(
    Object.entries(plugin.rules).filter(
      ([key, rule]) => (
        // Only include fixable rules
        rule.meta.fixable && !rule.meta.deprecated && key === rule.meta.docs.url.split("/").pop() && (!filter)
      )
    ).map(([key]) => [`${name}/${key}`, 2])
  );
  if (flat) {
    return {
      plugins: {
        [name]: plugin
      },
      rules
    };
  } else {
    return {
      plugins: [name],
      rules
    };
  }
}

var rules = {
  "array-bracket-newline": arrayBracketNewline.arrayBracketNewline,
  "array-bracket-spacing": arrayBracketSpacing.arrayBracketSpacing,
  "array-element-newline": arrayElementNewline.arrayElementNewline,
  "arrow-parens": arrowParens.arrowParens,
  "arrow-spacing": arrowSpacing.arrowSpacing,
  "block-spacing": blockSpacing.blockSpacing,
  "brace-style": braceStyle.braceStyle,
  "comma-dangle": commaDangle.commaDangle,
  "comma-spacing": commaSpacing.commaSpacing,
  "comma-style": commaStyle.commaStyle,
  "computed-property-spacing": computedPropertySpacing.computedPropertySpacing,
  "dot-location": dotLocation.dotLocation,
  "eol-last": eolLast.eolLast,
  "func-call-spacing": functionCallSpacing.functionCallSpacing,
  "function-call-argument-newline": functionCallArgumentNewline.functionCallArgumentNewline,
  "function-call-spacing": functionCallSpacing.functionCallSpacing,
  "function-paren-newline": functionParenNewline.functionParenNewline,
  "generator-star-spacing": generatorStarSpacing.generatorStarSpacing,
  "implicit-arrow-linebreak": implicitArrowLinebreak.implicitArrowLinebreak,
  "indent": indent.indent,
  "jsx-quotes": jsxQuotes.jsxQuotes,
  "key-spacing": keySpacing.keySpacing,
  "keyword-spacing": keywordSpacing.keywordSpacing,
  "line-comment-position": lineCommentPosition.lineCommentPosition,
  "linebreak-style": linebreakStyle.linebreakStyle,
  "lines-around-comment": linesAroundComment.linesAroundComment,
  "lines-between-class-members": linesBetweenClassMembers.linesBetweenClassMembers,
  "max-len": maxLen.maxLen,
  "max-statements-per-line": maxStatementsPerLine.maxStatementsPerLine,
  "multiline-comment-style": multilineCommentStyle.multilineCommentStyle,
  "multiline-ternary": multilineTernary.multilineTernary,
  "new-parens": newParens.newParens,
  "newline-per-chained-call": newlinePerChainedCall.newlinePerChainedCall,
  "no-confusing-arrow": noConfusingArrow.noConfusingArrow,
  "no-extra-parens": noExtraParens.noExtraParens,
  "no-extra-semi": noExtraSemi.noExtraSemi,
  "no-floating-decimal": noFloatingDecimal.noFloatingDecimal,
  "no-mixed-operators": noMixedOperators.noMixedOperators,
  "no-mixed-spaces-and-tabs": noMixedSpacesAndTabs.noMixedSpacesAndTabs,
  "no-multi-spaces": noMultiSpaces.noMultiSpaces,
  "no-multiple-empty-lines": noMultipleEmptyLines.noMultipleEmptyLines,
  "no-tabs": noTabs.noTabs,
  "no-trailing-spaces": noTrailingSpaces.noTrailingSpaces,
  "no-whitespace-before-property": noWhitespaceBeforeProperty.noWhitespaceBeforeProperty,
  "nonblock-statement-body-position": nonblockStatementBodyPosition.nonblockStatementBodyPosition,
  "object-curly-newline": objectCurlyNewline.objectCurlyNewline,
  "object-curly-spacing": objectCurlySpacing.objectCurlySpacing,
  "object-property-newline": objectPropertyNewline.objectPropertyNewline,
  "one-var-declaration-per-line": oneVarDeclarationPerLine.oneVarDeclarationPerLine,
  "operator-linebreak": operatorLinebreak.operatorLinebreak,
  "padded-blocks": paddedBlocks.paddedBlocks,
  "padding-line-between-statements": paddingLineBetweenStatements.paddingLineBetweenStatements,
  "quote-props": quoteProps.quoteProps,
  "quotes": quotes.quotes,
  "rest-spread-spacing": restSpreadSpacing.restSpreadSpacing,
  "semi": semi.semi,
  "semi-spacing": semiSpacing.semiSpacing,
  "semi-style": semiStyle.semiStyle,
  "space-before-blocks": spaceBeforeBlocks.spaceBeforeBlocks,
  "space-before-function-paren": spaceBeforeFunctionParen.spaceBeforeFunctionParen,
  "space-in-parens": spaceInParens.spaceInParens,
  "space-infix-ops": spaceInfixOps.spaceInfixOps,
  "space-unary-ops": spaceUnaryOps.spaceUnaryOps,
  "spaced-comment": spacedComment.spacedComment,
  "switch-colon-spacing": switchColonSpacing.switchColonSpacing,
  "template-curly-spacing": templateCurlySpacing.templateCurlySpacing,
  "template-tag-spacing": templateTagSpacing.templateTagSpacing,
  "wrap-iife": wrapIife.wrapIife,
  "wrap-regex": wrapRegex.wrapRegex,
  "yield-star-spacing": yieldStarSpacing.yieldStarSpacing
};

var plugin = {
  rules
};

const config = {
  rules: {
    "array-bracket-newline": 0,
    "array-bracket-spacing": 0,
    "array-element-newline": 0,
    "arrow-parens": 0,
    "arrow-spacing": 0,
    "block-spacing": 0,
    "brace-style": 0,
    "comma-dangle": 0,
    "comma-spacing": 0,
    "comma-style": 0,
    "computed-property-spacing": 0,
    "dot-location": 0,
    "eol-last": 0,
    "func-call-spacing": 0,
    "function-call-argument-newline": 0,
    "function-paren-newline": 0,
    "generator-star-spacing": 0,
    "implicit-arrow-linebreak": 0,
    "indent": 0,
    "jsx-quotes": 0,
    "key-spacing": 0,
    "keyword-spacing": 0,
    "line-comment-position": 0,
    "linebreak-style": 0,
    "lines-around-comment": 0,
    "lines-between-class-members": 0,
    "max-len": 0,
    "max-statements-per-line": 0,
    "multiline-comment-style": 0,
    "multiline-ternary": 0,
    "new-parens": 0,
    "newline-per-chained-call": 0,
    "no-confusing-arrow": 0,
    "no-extra-parens": 0,
    "no-extra-semi": 0,
    "no-floating-decimal": 0,
    "no-mixed-operators": 0,
    "no-mixed-spaces-and-tabs": 0,
    "no-multi-spaces": 0,
    "no-multiple-empty-lines": 0,
    "no-tabs": 0,
    "no-trailing-spaces": 0,
    "no-whitespace-before-property": 0,
    "nonblock-statement-body-position": 0,
    "object-curly-newline": 0,
    "object-curly-spacing": 0,
    "object-property-newline": 0,
    "one-var-declaration-per-line": 0,
    "operator-linebreak": 0,
    "padded-blocks": 0,
    "padding-line-between-statements": 0,
    "quote-props": 0,
    "quotes": 0,
    "rest-spread-spacing": 0,
    "semi": 0,
    "semi-spacing": 0,
    "semi-style": 0,
    "space-before-blocks": 0,
    "space-before-function-paren": 0,
    "space-in-parens": 0,
    "space-infix-ops": 0,
    "space-unary-ops": 0,
    "spaced-comment": 0,
    "switch-colon-spacing": 0,
    "template-curly-spacing": 0,
    "template-tag-spacing": 0,
    "wrap-iife": 0,
    "wrap-regex": 0,
    "yield-star-spacing": 0
  }
};

const configs = {
  "disable-legacy": config,
  "all-flat": createAllConfigs(plugin, "@stylistic/js", true),
  "all-extends": createAllConfigs(plugin, "@stylistic/js", false)
};

exports.configs = configs;
exports.plugin = plugin;
