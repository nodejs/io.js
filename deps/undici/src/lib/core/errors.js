'use strict'

class UndiciError extends Error {
  constructor (message, options) {
    super(message, options)
    this.name = 'UndiciError'
    this.code = 'UND_ERR'
  }
}

class ConnectTimeoutError extends UndiciError {
  constructor (message) {
    super(message)
    this.name = 'ConnectTimeoutError'
    this.message = message || 'Connect Timeout Error'
    this.code = 'UND_ERR_CONNECT_TIMEOUT'
  }
}

class HeadersTimeoutError extends UndiciError {
  constructor (message) {
    super(message)
    this.name = 'HeadersTimeoutError'
    this.message = message || 'Headers Timeout Error'
    this.code = 'UND_ERR_HEADERS_TIMEOUT'
  }
}

class HeadersOverflowError extends UndiciError {
  constructor (message) {
    super(message)
    this.name = 'HeadersOverflowError'
    this.message = message || 'Headers Overflow Error'
    this.code = 'UND_ERR_HEADERS_OVERFLOW'
  }
}

class BodyTimeoutError extends UndiciError {
  constructor (message) {
    super(message)
    this.name = 'BodyTimeoutError'
    this.message = message || 'Body Timeout Error'
    this.code = 'UND_ERR_BODY_TIMEOUT'
  }
}

class ResponseStatusCodeError extends UndiciError {
  constructor (message, statusCode, headers, body) {
    super(message)
    this.name = 'ResponseStatusCodeError'
    this.message = message || 'Response Status Code Error'
    this.code = 'UND_ERR_RESPONSE_STATUS_CODE'
    this.body = body
    this.status = statusCode
    this.statusCode = statusCode
    this.headers = headers
  }
}

class InvalidArgumentError extends UndiciError {
  constructor (message) {
    super(message)
    this.name = 'InvalidArgumentError'
    this.message = message || 'Invalid Argument Error'
    this.code = 'UND_ERR_INVALID_ARG'
  }
}

class InvalidReturnValueError extends UndiciError {
  constructor (message) {
    super(message)
    this.name = 'InvalidReturnValueError'
    this.message = message || 'Invalid Return Value Error'
    this.code = 'UND_ERR_INVALID_RETURN_VALUE'
  }
}

class AbortError extends UndiciError {
  constructor (message) {
    super(message)
    this.name = 'AbortError'
    this.message = message || 'The operation was aborted'
  }
}

class RequestAbortedError extends AbortError {
  constructor (message) {
    super(message)
    this.name = 'AbortError'
    this.message = message || 'Request aborted'
    this.code = 'UND_ERR_ABORTED'
  }
}

class InformationalError extends UndiciError {
  constructor (message) {
    super(message)
    this.name = 'InformationalError'
    this.message = message || 'Request information'
    this.code = 'UND_ERR_INFO'
  }
}

class RequestContentLengthMismatchError extends UndiciError {
  constructor (message) {
    super(message)
    this.name = 'RequestContentLengthMismatchError'
    this.message = message || 'Request body length does not match content-length header'
    this.code = 'UND_ERR_REQ_CONTENT_LENGTH_MISMATCH'
  }
}

class ResponseContentLengthMismatchError extends UndiciError {
  constructor (message) {
    super(message)
    this.name = 'ResponseContentLengthMismatchError'
    this.message = message || 'Response body length does not match content-length header'
    this.code = 'UND_ERR_RES_CONTENT_LENGTH_MISMATCH'
  }
}

class ClientDestroyedError extends UndiciError {
  constructor (message) {
    super(message)
    this.name = 'ClientDestroyedError'
    this.message = message || 'The client is destroyed'
    this.code = 'UND_ERR_DESTROYED'
  }
}

class ClientClosedError extends UndiciError {
  constructor (message) {
    super(message)
    this.name = 'ClientClosedError'
    this.message = message || 'The client is closed'
    this.code = 'UND_ERR_CLOSED'
  }
}

class SocketError extends UndiciError {
  constructor (message, socket) {
    super(message)
    this.name = 'SocketError'
    this.message = message || 'Socket error'
    this.code = 'UND_ERR_SOCKET'
    this.socket = socket
  }
}

class NotSupportedError extends UndiciError {
  constructor (message) {
    super(message)
    this.name = 'NotSupportedError'
    this.message = message || 'Not supported error'
    this.code = 'UND_ERR_NOT_SUPPORTED'
  }
}

class BalancedPoolMissingUpstreamError extends UndiciError {
  constructor (message) {
    super(message)
    this.name = 'MissingUpstreamError'
    this.message = message || 'No upstream has been added to the BalancedPool'
    this.code = 'UND_ERR_BPL_MISSING_UPSTREAM'
  }
}

class HTTPParserError extends Error {
  constructor (message, code, data) {
    super(message)
    this.name = 'HTTPParserError'
    this.code = code ? `HPE_${code}` : undefined
    this.data = data ? data.toString() : undefined
  }
}

class ResponseExceededMaxSizeError extends UndiciError {
  constructor (message) {
    super(message)
    this.name = 'ResponseExceededMaxSizeError'
    this.message = message || 'Response content exceeded max size'
    this.code = 'UND_ERR_RES_EXCEEDED_MAX_SIZE'
  }
}

class RequestRetryError extends UndiciError {
  constructor (message, code, { headers, data }) {
    super(message)
    this.name = 'RequestRetryError'
    this.message = message || 'Request retry error'
    this.code = 'UND_ERR_REQ_RETRY'
    this.statusCode = code
    this.data = data
    this.headers = headers
  }
}

class ResponseError extends UndiciError {
  constructor (message, code, { headers, body }) {
    super(message)
    this.name = 'ResponseError'
    this.message = message || 'Response error'
    this.code = 'UND_ERR_RESPONSE'
    this.statusCode = code
    this.body = body
    this.headers = headers
  }
}

class SecureProxyConnectionError extends UndiciError {
  constructor (cause, message, options = {}) {
    super(message, { cause, ...options })
    this.name = 'SecureProxyConnectionError'
    this.message = message || 'Secure Proxy Connection failed'
    this.code = 'UND_ERR_PRX_TLS'
    this.cause = cause
  }
}

module.exports = {
  AbortError,
  HTTPParserError,
  UndiciError,
  HeadersTimeoutError,
  HeadersOverflowError,
  BodyTimeoutError,
  RequestContentLengthMismatchError,
  ConnectTimeoutError,
  ResponseStatusCodeError,
  InvalidArgumentError,
  InvalidReturnValueError,
  RequestAbortedError,
  ClientDestroyedError,
  ClientClosedError,
  InformationalError,
  SocketError,
  NotSupportedError,
  ResponseContentLengthMismatchError,
  BalancedPoolMissingUpstreamError,
  ResponseExceededMaxSizeError,
  RequestRetryError,
  ResponseError,
  SecureProxyConnectionError
}
