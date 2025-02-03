'use strict'

const { UndiciError } = require('../core/errors')

/**
 * The request does not match any registered mock dispatches.
 */
class MockNotMatchedError extends UndiciError {
  constructor (message) {
    super(message)
    this.name = 'MockNotMatchedError'
    this.message = message || 'The request does not match any registered mock dispatches'
    this.code = 'UND_MOCK_ERR_MOCK_NOT_MATCHED'
  }
}

module.exports = {
  MockNotMatchedError
}
