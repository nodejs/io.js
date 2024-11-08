'use strict'

class InvalidProxyProtocolError extends Error {
  constructor (url) {
    super(`Invalid protocol \`${url.protocol}\` connecting to proxy \`${url.host}\``)
    this.code = 'EINVALIDPROXY'
    this.proxy = url
  }
}

class ConnectionTimeoutError extends Error {
  constructor (host) {
    super(`Timeout connecting to host \`${host}\``)
    this.code = 'ECONNECTIONTIMEOUT'
    this.host = host
  }
}

class IdleTimeoutError extends Error {
  constructor (host) {
    super(`Idle timeout reached for host \`${host}\``)
    this.code = 'EIDLETIMEOUT'
    this.host = host
  }
}

class ResponseTimeoutError extends Error {
  constructor (request, proxy) {
    let msg = 'Response timeout '
    if (proxy) {
      msg += `from proxy \`${proxy.host}\` `
    }
    msg += `connecting to host \`${request.host}\``
    super(msg)
    this.code = 'ERESPONSETIMEOUT'
    this.proxy = proxy
    this.request = request
  }
}

class TransferTimeoutError extends Error {
  constructor (request, proxy) {
    let msg = 'Transfer timeout '
    if (proxy) {
      msg += `from proxy \`${proxy.host}\` `
    }
    msg += `for \`${request.host}\``
    super(msg)
    this.code = 'ETRANSFERTIMEOUT'
    this.proxy = proxy
    this.request = request
  }
}

module.exports = {
  InvalidProxyProtocolError,
  ConnectionTimeoutError,
  IdleTimeoutError,
  ResponseTimeoutError,
  TransferTimeoutError,
}
