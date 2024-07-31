'use strict'

const errors = require('./errors.js')
const { Response } = require('minipass-fetch')
const defaultOpts = require('./default-opts.js')
const { log } = require('proc-log')
const { redact: cleanUrl } = require('@npmcli/redact')

/* eslint-disable-next-line max-len */
const moreInfoUrl = 'https://github.com/npm/cli/wiki/No-auth-for-URI,-but-auth-present-for-scoped-registry'
const checkResponse =
  async ({ method, uri, res, startTime, auth, opts }) => {
    opts = { ...defaultOpts, ...opts }
    if (res.headers.has('npm-notice') && !res.headers.has('x-local-cache')) {
      log.notice('', res.headers.get('npm-notice'))
    }

    if (res.status >= 400) {
      logRequest(method, res, startTime)
      if (auth && auth.scopeAuthKey && !auth.token && !auth.auth) {
      // we didn't have auth for THIS request, but we do have auth for
      // requests to the registry indicated by the spec's scope value.
      // Warn the user.
        log.warn('registry', `No auth for URI, but auth present for scoped registry.

URI: ${uri}
Scoped Registry Key: ${auth.scopeAuthKey}

More info here: ${moreInfoUrl}`)
      }
      return checkErrors(method, res, startTime, opts)
    } else {
      res.body.on('end', () => logRequest(method, res, startTime, opts))
      if (opts.ignoreBody) {
        res.body.resume()
        return new Response(null, res)
      }
      return res
    }
  }
module.exports = checkResponse

function logRequest (method, res, startTime) {
  const elapsedTime = Date.now() - startTime
  const attempt = res.headers.get('x-fetch-attempts')
  const attemptStr = attempt && attempt > 1 ? ` attempt #${attempt}` : ''
  const cacheStatus = res.headers.get('x-local-cache-status')
  const cacheStr = cacheStatus ? ` (cache ${cacheStatus})` : ''
  const urlStr = cleanUrl(res.url)

  log.http(
    'fetch',
    `${method.toUpperCase()} ${res.status} ${urlStr} ${elapsedTime}ms${attemptStr}${cacheStr}`
  )
}

function checkErrors (method, res, startTime, opts) {
  return res.buffer()
    .catch(() => null)
    .then(body => {
      let parsed = body
      try {
        parsed = JSON.parse(body.toString('utf8'))
      } catch {
        // ignore errors
      }
      if (res.status === 401 && res.headers.get('www-authenticate')) {
        const auth = res.headers.get('www-authenticate')
          .split(/,\s*/)
          .map(s => s.toLowerCase())
        if (auth.indexOf('ipaddress') !== -1) {
          throw new errors.HttpErrorAuthIPAddress(
            method, res, parsed, opts.spec
          )
        } else if (auth.indexOf('otp') !== -1) {
          throw new errors.HttpErrorAuthOTP(
            method, res, parsed, opts.spec
          )
        } else {
          throw new errors.HttpErrorAuthUnknown(
            method, res, parsed, opts.spec
          )
        }
      } else if (
        res.status === 401 &&
        body != null &&
        /one-time pass/.test(body.toString('utf8'))
      ) {
        // Heuristic for malformed OTP responses that don't include the
        // www-authenticate header.
        throw new errors.HttpErrorAuthOTP(
          method, res, parsed, opts.spec
        )
      } else {
        throw new errors.HttpErrorGeneral(
          method, res, parsed, opts.spec
        )
      }
    })
}
