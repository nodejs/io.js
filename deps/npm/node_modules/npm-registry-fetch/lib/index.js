'use strict'

const { HttpErrorAuthOTP } = require('./errors.js')
const checkResponse = require('./check-response.js')
const getAuth = require('./auth.js')
const fetch = require('make-fetch-happen')
const JSONStream = require('./json-stream')
const npa = require('npm-package-arg')
const qs = require('querystring')
const url = require('url')
const zlib = require('minizlib')
const { Minipass } = require('minipass')

const defaultOpts = require('./default-opts.js')

// WhatWG URL throws if it's not fully resolved
const urlIsValid = u => {
  try {
    return !!new url.URL(u)
  } catch (_) {
    return false
  }
}

module.exports = regFetch
function regFetch (uri, /* istanbul ignore next */ opts_ = {}) {
  const opts = {
    ...defaultOpts,
    ...opts_,
  }

  // if we did not get a fully qualified URI, then we look at the registry
  // config or relevant scope to resolve it.
  const uriValid = urlIsValid(uri)
  let registry = opts.registry || defaultOpts.registry
  if (!uriValid) {
    registry = opts.registry = (
      (opts.spec && pickRegistry(opts.spec, opts)) ||
      opts.registry ||
      registry
    )
    uri = `${
      registry.trim().replace(/\/?$/g, '')
    }/${
      uri.trim().replace(/^\//, '')
    }`
    // asserts that this is now valid
    new url.URL(uri)
  }

  const method = opts.method || 'GET'

  // through that takes into account the scope, the prefix of `uri`, etc
  const startTime = Date.now()
  const auth = getAuth(uri, opts)
  const headers = getHeaders(uri, auth, opts)
  let body = opts.body
  const bodyIsStream = Minipass.isStream(body)
  const bodyIsPromise = body &&
    typeof body === 'object' &&
    typeof body.then === 'function'

  if (
    body && !bodyIsStream && !bodyIsPromise && typeof body !== 'string' && !Buffer.isBuffer(body)
  ) {
    headers['content-type'] = headers['content-type'] || 'application/json'
    body = JSON.stringify(body)
  } else if (body && !headers['content-type']) {
    headers['content-type'] = 'application/octet-stream'
  }

  if (opts.gzip) {
    headers['content-encoding'] = 'gzip'
    if (bodyIsStream) {
      const gz = new zlib.Gzip()
      body.on('error', /* istanbul ignore next: unlikely and hard to test */
        err => gz.emit('error', err))
      body = body.pipe(gz)
    } else if (!bodyIsPromise) {
      body = new zlib.Gzip().end(body).concat()
    }
  }

  const parsed = new url.URL(uri)

  if (opts.query) {
    const q = typeof opts.query === 'string' ? qs.parse(opts.query)
      : opts.query

    Object.keys(q).forEach(key => {
      if (q[key] !== undefined) {
        parsed.searchParams.set(key, q[key])
      }
    })
    uri = url.format(parsed)
  }

  if (parsed.searchParams.get('write') === 'true' && method === 'GET') {
    // do not cache, because this GET is fetching a rev that will be
    // used for a subsequent PUT or DELETE, so we need to conditionally
    // update cache.
    opts.offline = false
    opts.preferOffline = false
    opts.preferOnline = true
  }

  const doFetch = async fetchBody => {
    const p = fetch(uri, {
      agent: opts.agent,
      algorithms: opts.algorithms,
      body: fetchBody,
      cache: getCacheMode(opts),
      cachePath: opts.cache,
      ca: opts.ca,
      cert: auth.cert || opts.cert,
      headers,
      integrity: opts.integrity,
      key: auth.key || opts.key,
      localAddress: opts.localAddress,
      maxSockets: opts.maxSockets,
      memoize: opts.memoize,
      method: method,
      noProxy: opts.noProxy,
      proxy: opts.httpsProxy || opts.proxy,
      retry: opts.retry ? opts.retry : {
        retries: opts.fetchRetries,
        factor: opts.fetchRetryFactor,
        minTimeout: opts.fetchRetryMintimeout,
        maxTimeout: opts.fetchRetryMaxtimeout,
      },
      strictSSL: opts.strictSSL,
      timeout: opts.timeout || 30 * 1000,
    }).then(res => checkResponse({
      method,
      uri,
      res,
      registry,
      startTime,
      auth,
      opts,
    }))

    if (typeof opts.otpPrompt === 'function') {
      return p.catch(async er => {
        if (er instanceof HttpErrorAuthOTP) {
          let otp
          // if otp fails to complete, we fail with that failure
          try {
            otp = await opts.otpPrompt()
          } catch (_) {
            // ignore this error
          }
          // if no otp provided, or otpPrompt errored, throw the original HTTP error
          if (!otp) {
            throw er
          }
          return regFetch(uri, { ...opts, otp })
        }
        throw er
      })
    } else {
      return p
    }
  }

  return Promise.resolve(body).then(doFetch)
}

module.exports.getAuth = getAuth

module.exports.json = fetchJSON
function fetchJSON (uri, opts) {
  return regFetch(uri, opts).then(res => res.json())
}

module.exports.json.stream = fetchJSONStream
function fetchJSONStream (uri, jsonPath,
  /* istanbul ignore next */ opts_ = {}) {
  const opts = { ...defaultOpts, ...opts_ }
  const parser = JSONStream.parse(jsonPath, opts.mapJSON)
  regFetch(uri, opts).then(res =>
    res.body.on('error',
      /* istanbul ignore next: unlikely and difficult to test */
      er => parser.emit('error', er)).pipe(parser)
  ).catch(er => parser.emit('error', er))
  return parser
}

module.exports.pickRegistry = pickRegistry
function pickRegistry (spec, opts = {}) {
  spec = npa(spec)
  let registry = spec.scope &&
    opts[spec.scope.replace(/^@?/, '@') + ':registry']

  if (!registry && opts.scope) {
    registry = opts[opts.scope.replace(/^@?/, '@') + ':registry']
  }

  if (!registry) {
    registry = opts.registry || defaultOpts.registry
  }

  return registry
}

function getCacheMode (opts) {
  return opts.offline ? 'only-if-cached'
    : opts.preferOffline ? 'force-cache'
    : opts.preferOnline ? 'no-cache'
    : 'default'
}

function getHeaders (uri, auth, opts) {
  const headers = Object.assign({
    'user-agent': opts.userAgent,
  }, opts.headers || {})

  if (opts.authType) {
    headers['npm-auth-type'] = opts.authType
  }

  if (opts.scope) {
    headers['npm-scope'] = opts.scope
  }

  if (opts.npmSession) {
    headers['npm-session'] = opts.npmSession
  }

  if (opts.npmCommand) {
    headers['npm-command'] = opts.npmCommand
  }

  // If a tarball is hosted on a different place than the manifest, only send
  // credentials on `alwaysAuth`
  if (auth.token) {
    headers.authorization = `Bearer ${auth.token}`
  } else if (auth.auth) {
    headers.authorization = `Basic ${auth.auth}`
  }

  if (opts.otp) {
    headers['npm-otp'] = opts.otp
  }

  return headers
}
