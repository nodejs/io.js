// Flags: --env-file test/fixtures/dotenv/invalid-syntax.env
'use strict';

require('../common');
const assert = require('node:assert');
const { parseEnv } = require('node:util');

// Test direct parseEnv usage
{
  const input = `foo

bar
baz=whatever
VALID_AFTER_INVALID=test
multiple_invalid
lines_without_equals
ANOTHER_VALID=value`;

  const result = parseEnv(input);

  // Using individual assertions for better error messages
  assert.strictEqual(Object.keys(result).length, 3, 'Should only have 3 valid entries');
  assert.strictEqual(result.baz, 'whatever', 'baz should have value "whatever"');
  assert.strictEqual(result.VALID_AFTER_INVALID, 'test', 'VALID_AFTER_INVALID should have value "test"');
  assert.strictEqual(result.ANOTHER_VALID, 'value', 'ANOTHER_VALID should have value "value"');

  // Ensure invalid entries are not present
  assert.strictEqual(result.foo, undefined, 'foo should not be present');
  assert.strictEqual(result.bar, undefined, 'bar should not be present');
  assert.strictEqual(result.multiple_invalid, undefined, 'multiple_invalid should not be present');
  assert.strictEqual(result.lines_without_equals, undefined, 'lines_without_equals should not be present');
}

// Test edge cases
{
  const edgeCases = [
    // Empty file
    {
      input: '\n\n  \n    ',
      expected: {}
    },
    // Only invalid lines
    {
      input: 'no_equals_here\nanother_invalid_line\n  just_text',
      expected: {}
    },
    // Mixed valid and invalid
    {
      input: 'VALID1=value1\ninvalid\nVALID2=value2',
      expected: {
        VALID1: 'value1',
        VALID2: 'value2'
      }
    },
    // Lines with spaces but no equals
    {
      input: '   spaces   \nVALID=value\n  more  spaces  ',
      expected: {
        VALID: 'value'
      }
    }
  ];

  for (const { input, expected } of edgeCases) {
    assert.deepStrictEqual(
      parseEnv(input),
      expected,
      `Failed parsing: ${JSON.stringify(input)}`
    );
  }
}
