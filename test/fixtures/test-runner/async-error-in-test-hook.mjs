import { after, afterEach, before, beforeEach, test } from 'node:test'

before(() => {
  setTimeout(() => {
    throw new Error('before')
  }, 100)
})

beforeEach(() => {
  setTimeout(() => {
    throw new Error('beforeEach')
  }, 100)
})

after(() => {
  setTimeout(() => {
    throw new Error('after')
  }, 100)
})

afterEach(() => {
  setTimeout(() => {
    throw new Error('afterEach')
  }, 100)
})

test('ok', () => {})
