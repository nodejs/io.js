'use strict'

const { URL } = require('node:url')

function packageName (href) {
  try {
    let basePath = new URL(href).pathname.slice(1)
    if (!basePath.match(/^-/)) {
      basePath = basePath.split('/')
      var index = basePath.indexOf('_rewrite')
      if (index === -1) {
        index = basePath.length - 1
      } else {
        index++
      }
      return decodeURIComponent(basePath[index])
    }
  } catch {
    // this is ok
  }
}

class HttpErrorBase extends Error {
  constructor (method, res, body, spec) {
    super()
    this.name = this.constructor.name
    this.headers = typeof res.headers?.raw === 'function' ? res.headers.raw() : res.headers
    this.statusCode = res.status
    this.code = `E${res.status}`
    this.method = method
    this.uri = res.url
    this.body = body
    this.pkgid = spec ? spec.toString() : packageName(res.url)
    Error.captureStackTrace(this, this.constructor)
  }
}

class HttpErrorGeneral extends HttpErrorBase {
  constructor (method, res, body, spec) {
    super(method, res, body, spec)
    this.message = `${res.status} ${res.statusText} - ${
      this.method.toUpperCase()
    } ${
      this.spec || this.uri
    }${
      (body && body.error) ? ' - ' + body.error : ''
    }`
  }
}

class HttpErrorAuthOTP extends HttpErrorBase {
  constructor (method, res, body, spec) {
    super(method, res, body, spec)
    this.message = 'OTP required for authentication'
    this.code = 'EOTP'
  }
}

class HttpErrorAuthIPAddress extends HttpErrorBase {
  constructor (method, res, body, spec) {
    super(method, res, body, spec)
    this.message = 'Login is not allowed from your IP address'
    this.code = 'EAUTHIP'
  }
}

class HttpErrorAuthUnknown extends HttpErrorBase {
  constructor (method, res, body, spec) {
    super(method, res, body, spec)
    this.message = 'Unable to authenticate, need: ' + res.headers.get('www-authenticate')
  }
}

module.exports = {
  HttpErrorBase,
  HttpErrorGeneral,
  HttpErrorAuthOTP,
  HttpErrorAuthIPAddress,
  HttpErrorAuthUnknown,
}
