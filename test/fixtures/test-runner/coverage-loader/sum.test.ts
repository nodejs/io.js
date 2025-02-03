import { describe, it } from 'node:test'
import assert from 'node:assert'
import { sum } from './sum'

describe('sum', () => {
  it('should sum two numbers', () => {
    assert.deepStrictEqual(sum(1, 2), 3)
  })

  it('should error out if one is not a number', () => {
    assert.throws(() => sum(1, 'b'), Error)
  })
})
