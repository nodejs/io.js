const { Minipass } = require('minipass')
const fetch = require('minipass-fetch')
const promiseRetry = require('promise-retry')
const ssri = require('ssri')
const { log } = require('proc-log')

const CachingMinipassPipeline = require('./pipeline.js')
const { getAgent } = require('@npmcli/agent')
const pkg = require('../package.json')

const USER_AGENT = `${pkg.name}/${pkg.version} (+https://npm.im/${pkg.name})`

const RETRY_ERRORS = [
  'ECONNRESET', // remote socket closed on us
  'ECONNREFUSED', // remote host refused to open connection
  'EADDRINUSE', // failed to bind to a local port (proxy?)
  'ETIMEDOUT', // someone in the transaction is WAY TOO SLOW
  // from @npmcli/agent
  'ECONNECTIONTIMEOUT',
  'EIDLETIMEOUT',
  'ERESPONSETIMEOUT',
  'ETRANSFERTIMEOUT',
  // Known codes we do NOT retry on:
  // ENOTFOUND (getaddrinfo failure. Either bad hostname, or offline)
  // EINVALIDPROXY // invalid protocol from @npmcli/agent
  // EINVALIDRESPONSE // invalid status code from @npmcli/agent
]

const RETRY_TYPES = [
  'request-timeout',
]

// make a request directly to the remote source,
// retrying certain classes of errors as well as
// following redirects (through the cache if necessary)
// and verifying response integrity
const remoteFetch = (request, options) => {
  const agent = getAgent(request.url, options)
  if (!request.headers.has('connection')) {
    request.headers.set('connection', agent ? 'keep-alive' : 'close')
  }

  if (!request.headers.has('user-agent')) {
    request.headers.set('user-agent', USER_AGENT)
  }

  // keep our own options since we're overriding the agent
  // and the redirect mode
  const _opts = {
    ...options,
    agent,
    redirect: 'manual',
  }

  return promiseRetry(async (retryHandler, attemptNum) => {
    const req = new fetch.Request(request, _opts)
    try {
      let res = await fetch(req, _opts)
      if (_opts.integrity && res.status === 200) {
        // we got a 200 response and the user has specified an expected
        // integrity value, so wrap the response in an ssri stream to verify it
        const integrityStream = ssri.integrityStream({
          algorithms: _opts.algorithms,
          integrity: _opts.integrity,
          size: _opts.size,
        })
        const pipeline = new CachingMinipassPipeline({
          events: ['integrity', 'size'],
        }, res.body, integrityStream)
        // we also propagate the integrity and size events out to the pipeline so we can use
        // this new response body as an integrityEmitter for cacache
        integrityStream.on('integrity', i => pipeline.emit('integrity', i))
        integrityStream.on('size', s => pipeline.emit('size', s))
        res = new fetch.Response(pipeline, res)
        // set an explicit flag so we know if our response body will emit integrity and size
        res.body.hasIntegrityEmitter = true
      }

      res.headers.set('x-fetch-attempts', attemptNum)

      // do not retry POST requests, or requests with a streaming body
      // do retry requests with a 408, 420, 429 or 500+ status in the response
      const isStream = Minipass.isStream(req.body)
      const isRetriable = req.method !== 'POST' &&
          !isStream &&
          ([408, 420, 429].includes(res.status) || res.status >= 500)

      if (isRetriable) {
        if (typeof options.onRetry === 'function') {
          options.onRetry(res)
        }

        /* eslint-disable-next-line max-len */
        log.http('fetch', `${req.method} ${req.url} attempt ${attemptNum} failed with ${res.status}`)
        return retryHandler(res)
      }

      return res
    } catch (err) {
      const code = (err.code === 'EPROMISERETRY')
        ? err.retried.code
        : err.code

      // err.retried will be the thing that was thrown from above
      // if it's a response, we just got a bad status code and we
      // can re-throw to allow the retry
      const isRetryError = err.retried instanceof fetch.Response ||
        (RETRY_ERRORS.includes(code) && RETRY_TYPES.includes(err.type))

      if (req.method === 'POST' || isRetryError) {
        throw err
      }

      if (typeof options.onRetry === 'function') {
        options.onRetry(err)
      }

      log.http('fetch', `${req.method} ${req.url} attempt ${attemptNum} failed with ${err.code}`)
      return retryHandler(err)
    }
  }, options.retry).catch((err) => {
    // don't reject for http errors, just return them
    if (err.status >= 400 && err.type !== 'system') {
      return err
    }

    throw err
  })
}

module.exports = remoteFetch
