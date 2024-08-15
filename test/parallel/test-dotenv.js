// Flags: --env-file test/fixtures/dotenv/valid.env
'use strict';

require('../common');
const assert = require('node:assert');

// Sets basic environment variable
assert.strictEqual(process.env.BASIC, 'basic');
// Reads after a skipped line
assert.strictEqual(process.env.AFTER_LINE, 'after_line');
// Defaults empty values to empty string
assert.strictEqual(process.env.EMPTY, '');
assert.strictEqual(process.env.EMPTY_SINGLE_QUOTES, '');
assert.strictEqual(process.env.EMPTY_DOUBLE_QUOTES, '');
assert.strictEqual(process.env.EMPTY_BACKTICKS, '');
// Escapes single quoted values
assert.strictEqual(process.env.SINGLE_QUOTES, 'single_quotes');
// Respects surrounding spaces in single quotes
assert.strictEqual(process.env.SINGLE_QUOTES_SPACED, '    single quotes    ');
// Escapes double quoted values
assert.strictEqual(process.env.DOUBLE_QUOTES, 'double_quotes');
// Respects surrounding spaces in double quotes
assert.strictEqual(process.env.DOUBLE_QUOTES_SPACED, '    double quotes    ');
// Respects double quotes inside single quotes
assert.strictEqual(process.env.DOUBLE_QUOTES_INSIDE_SINGLE, 'double "quotes" work inside single quotes');
// Respects spacing for badly formed brackets
assert.strictEqual(process.env.DOUBLE_QUOTES_WITH_NO_SPACE_BRACKET, '{ port: $MONGOLAB_PORT}');
// Respects single quotes inside double quotes
assert.strictEqual(process.env.SINGLE_QUOTES_INSIDE_DOUBLE, "single 'quotes' work inside double quotes");
// Respects backticks inside single quotes
assert.strictEqual(process.env.BACKTICKS_INSIDE_SINGLE, '`backticks` work inside single quotes');
// Respects backticks inside double quotes
assert.strictEqual(process.env.BACKTICKS_INSIDE_DOUBLE, '`backticks` work inside double quotes');
assert.strictEqual(process.env.BACKTICKS, 'backticks');
assert.strictEqual(process.env.BACKTICKS_SPACED, '    backticks    ');
// Respects double quotes inside backticks
assert.strictEqual(process.env.DOUBLE_QUOTES_INSIDE_BACKTICKS, 'double "quotes" work inside backticks');
// Respects single quotes inside backticks
assert.strictEqual(process.env.SINGLE_QUOTES_INSIDE_BACKTICKS, "single 'quotes' work inside backticks");
// Respects single quotes inside backticks
assert.strictEqual(
  process.env.DOUBLE_AND_SINGLE_QUOTES_INSIDE_BACKTICKS,
  "double \"quotes\" and single 'quotes' work inside backticks",
);
// Ignores inline comments
assert.strictEqual(process.env.INLINE_COMMENTS, 'inline comments');
// Ignores inline comments and respects # character inside of single quotes
assert.strictEqual(process.env.INLINE_COMMENTS_SINGLE_QUOTES, 'inline comments outside of #singlequotes');
// Ignores inline comments and respects # character inside of double quotes
assert.strictEqual(process.env.INLINE_COMMENTS_DOUBLE_QUOTES, 'inline comments outside of #doublequotes');
// Ignores inline comments and respects # character inside of backticks
assert.strictEqual(process.env.INLINE_COMMENTS_BACKTICKS, 'inline comments outside of #backticks');
// Treats # character as start of comment
assert.strictEqual(process.env.INLINE_COMMENTS_SPACE, 'inline comments start with a');
// ignore comment
assert.strictEqual(process.env.COMMENTS, undefined);
// Respects equals signs in values
assert.strictEqual(process.env.EQUAL_SIGNS, 'equals==');
// Retains inner quotes
assert.strictEqual(process.env.RETAIN_INNER_QUOTES, '{"foo": "bar"}');
assert.strictEqual(process.env.RETAIN_INNER_QUOTES_AS_STRING, '{"foo": "bar"}');
assert.strictEqual(process.env.RETAIN_INNER_QUOTES_AS_BACKTICKS, '{"foo": "bar\'s"}');
// Retains spaces in string
assert.strictEqual(process.env.TRIM_SPACE_FROM_UNQUOTED, 'some spaced out string');
// Parses email addresses completely
assert.strictEqual(process.env.EMAIL, 'therealnerdybeast@example.tld');
// Parses keys and values surrounded by spaces
assert.strictEqual(process.env.SPACED_KEY, 'parsed');
// Parse inline comments correctly when multiple quotes
assert.strictEqual(process.env.EDGE_CASE_INLINE_COMMENTS, 'VALUE1');
// Test multi-line values with line breaks
assert.strictEqual(process.env.MULTI_DOUBLE_QUOTED, 'THIS\nIS\nA\nMULTILINE\nSTRING');
assert.strictEqual(process.env.MULTI_SINGLE_QUOTED, 'THIS\nIS\nA\nMULTILINE\nSTRING');
assert.strictEqual(process.env.MULTI_BACKTICKED, 'THIS\nIS\nA\n"MULTILINE\'S"\nSTRING');
assert.strictEqual(process.env.MULTI_NOT_VALID_QUOTE, '"');
assert.strictEqual(process.env.MULTI_NOT_VALID, 'THIS');
// Test that \n is expanded to a newline in double-quoted string
assert.strictEqual(process.env.EXPAND_NEWLINES, 'expand\nnew\nlines');
assert.strictEqual(process.env.DONT_EXPAND_UNQUOTED, 'dontexpand\\nnewlines');
assert.strictEqual(process.env.DONT_EXPAND_SQUOTED, 'dontexpand\\nnewlines');
// Ignore export before key
assert.strictEqual(process.env.EXPORT_EXAMPLE, 'ignore export');
// Ignore spaces before double quotes to avoid quoted strings as value
assert.strictEqual(process.env.SPACE_BEFORE_DOUBLE_QUOTES, 'space before double quotes');
