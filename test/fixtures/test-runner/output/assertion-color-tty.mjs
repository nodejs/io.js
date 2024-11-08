import assert from 'node:assert/strict'
import { test } from 'node:test'

test('failing assertion', () => {
  assert.strictEqual('!Hello World', 'Hello World!')
})
