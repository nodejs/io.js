const { Request, Response } = require('minipass-fetch')
const { Minipass } = require('minipass')
const MinipassFlush = require('minipass-flush')
const cacache = require('cacache')
const url = require('url')

const CachingMinipassPipeline = require('../pipeline.js')
const CachePolicy = require('./policy.js')
const cacheKey = require('./key.js')
const remote = require('../remote.js')

const hasOwnProperty = (obj, prop) => Object.prototype.hasOwnProperty.call(obj, prop)

// allow list for request headers that will be written to the cache index
// note: we will also store any request headers
// that are named in a response's vary header
const KEEP_REQUEST_HEADERS = [
  'accept-charset',
  'accept-encoding',
  'accept-language',
  'accept',
  'cache-control',
]

// allow list for response headers that will be written to the cache index
// note: we must not store the real response's age header, or when we load
// a cache policy based on the metadata it will think the cached response
// is always stale
const KEEP_RESPONSE_HEADERS = [
  'cache-control',
  'content-encoding',
  'content-language',
  'content-type',
  'date',
  'etag',
  'expires',
  'last-modified',
  'link',
  'location',
  'pragma',
  'vary',
]

// return an object containing all metadata to be written to the index
const getMetadata = (request, response, options) => {
  const metadata = {
    time: Date.now(),
    url: request.url,
    reqHeaders: {},
    resHeaders: {},

    // options on which we must match the request and vary the response
    options: {
      compress: options.compress != null ? options.compress : request.compress,
    },
  }

  // only save the status if it's not a 200 or 304
  if (response.status !== 200 && response.status !== 304) {
    metadata.status = response.status
  }

  for (const name of KEEP_REQUEST_HEADERS) {
    if (request.headers.has(name)) {
      metadata.reqHeaders[name] = request.headers.get(name)
    }
  }

  // if the request's host header differs from the host in the url
  // we need to keep it, otherwise it's just noise and we ignore it
  const host = request.headers.get('host')
  const parsedUrl = new url.URL(request.url)
  if (host && parsedUrl.host !== host) {
    metadata.reqHeaders.host = host
  }

  // if the response has a vary header, make sure
  // we store the relevant request headers too
  if (response.headers.has('vary')) {
    const vary = response.headers.get('vary')
    // a vary of "*" means every header causes a different response.
    // in that scenario, we do not include any additional headers
    // as the freshness check will always fail anyway and we don't
    // want to bloat the cache indexes
    if (vary !== '*') {
      // copy any other request headers that will vary the response
      const varyHeaders = vary.trim().toLowerCase().split(/\s*,\s*/)
      for (const name of varyHeaders) {
        if (request.headers.has(name)) {
          metadata.reqHeaders[name] = request.headers.get(name)
        }
      }
    }
  }

  for (const name of KEEP_RESPONSE_HEADERS) {
    if (response.headers.has(name)) {
      metadata.resHeaders[name] = response.headers.get(name)
    }
  }

  for (const name of options.cacheAdditionalHeaders) {
    if (response.headers.has(name)) {
      metadata.resHeaders[name] = response.headers.get(name)
    }
  }

  return metadata
}

// symbols used to hide objects that may be lazily evaluated in a getter
const _request = Symbol('request')
const _response = Symbol('response')
const _policy = Symbol('policy')

class CacheEntry {
  constructor ({ entry, request, response, options }) {
    if (entry) {
      this.key = entry.key
      this.entry = entry
      // previous versions of this module didn't write an explicit timestamp in
      // the metadata, so fall back to the entry's timestamp. we can't use the
      // entry timestamp to determine staleness because cacache will update it
      // when it verifies its data
      this.entry.metadata.time = this.entry.metadata.time || this.entry.time
    } else {
      this.key = cacheKey(request)
    }

    this.options = options

    // these properties are behind getters that lazily evaluate
    this[_request] = request
    this[_response] = response
    this[_policy] = null
  }

  // returns a CacheEntry instance that satisfies the given request
  // or undefined if no existing entry satisfies
  static async find (request, options) {
    try {
      // compacts the index and returns an array of unique entries
      var matches = await cacache.index.compact(options.cachePath, cacheKey(request), (A, B) => {
        const entryA = new CacheEntry({ entry: A, options })
        const entryB = new CacheEntry({ entry: B, options })
        return entryA.policy.satisfies(entryB.request)
      }, {
        validateEntry: (entry) => {
          // clean out entries with a buggy content-encoding value
          if (entry.metadata &&
              entry.metadata.resHeaders &&
              entry.metadata.resHeaders['content-encoding'] === null) {
            return false
          }

          // if an integrity is null, it needs to have a status specified
          if (entry.integrity === null) {
            return !!(entry.metadata && entry.metadata.status)
          }

          return true
        },
      })
    } catch (err) {
      // if the compact request fails, ignore the error and return
      return
    }

    // a cache mode of 'reload' means to behave as though we have no cache
    // on the way to the network. return undefined to allow cacheFetch to
    // create a brand new request no matter what.
    if (options.cache === 'reload') {
      return
    }

    // find the specific entry that satisfies the request
    let match
    for (const entry of matches) {
      const _entry = new CacheEntry({
        entry,
        options,
      })

      if (_entry.policy.satisfies(request)) {
        match = _entry
        break
      }
    }

    return match
  }

  // if the user made a PUT/POST/PATCH then we invalidate our
  // cache for the same url by deleting the index entirely
  static async invalidate (request, options) {
    const key = cacheKey(request)
    try {
      await cacache.rm.entry(options.cachePath, key, { removeFully: true })
    } catch (err) {
      // ignore errors
    }
  }

  get request () {
    if (!this[_request]) {
      this[_request] = new Request(this.entry.metadata.url, {
        method: 'GET',
        headers: this.entry.metadata.reqHeaders,
        ...this.entry.metadata.options,
      })
    }

    return this[_request]
  }

  get response () {
    if (!this[_response]) {
      this[_response] = new Response(null, {
        url: this.entry.metadata.url,
        counter: this.options.counter,
        status: this.entry.metadata.status || 200,
        headers: {
          ...this.entry.metadata.resHeaders,
          'content-length': this.entry.size,
        },
      })
    }

    return this[_response]
  }

  get policy () {
    if (!this[_policy]) {
      this[_policy] = new CachePolicy({
        entry: this.entry,
        request: this.request,
        response: this.response,
        options: this.options,
      })
    }

    return this[_policy]
  }

  // wraps the response in a pipeline that stores the data
  // in the cache while the user consumes it
  async store (status) {
    // if we got a status other than 200, 301, or 308,
    // or the CachePolicy forbid storage, append the
    // cache status header and return it untouched
    if (
      this.request.method !== 'GET' ||
      ![200, 301, 308].includes(this.response.status) ||
      !this.policy.storable()
    ) {
      this.response.headers.set('x-local-cache-status', 'skip')
      return this.response
    }

    const size = this.response.headers.get('content-length')
    const cacheOpts = {
      algorithms: this.options.algorithms,
      metadata: getMetadata(this.request, this.response, this.options),
      size,
      integrity: this.options.integrity,
      integrityEmitter: this.response.body.hasIntegrityEmitter && this.response.body,
    }

    let body = null
    // we only set a body if the status is a 200, redirects are
    // stored as metadata only
    if (this.response.status === 200) {
      let cacheWriteResolve, cacheWriteReject
      const cacheWritePromise = new Promise((resolve, reject) => {
        cacheWriteResolve = resolve
        cacheWriteReject = reject
      }).catch((err) => {
        body.emit('error', err)
      })

      body = new CachingMinipassPipeline({ events: ['integrity', 'size'] }, new MinipassFlush({
        flush () {
          return cacheWritePromise
        },
      }))
      // this is always true since if we aren't reusing the one from the remote fetch, we
      // are using the one from cacache
      body.hasIntegrityEmitter = true

      const onResume = () => {
        const tee = new Minipass()
        const cacheStream = cacache.put.stream(this.options.cachePath, this.key, cacheOpts)
        // re-emit the integrity and size events on our new response body so they can be reused
        cacheStream.on('integrity', i => body.emit('integrity', i))
        cacheStream.on('size', s => body.emit('size', s))
        // stick a flag on here so downstream users will know if they can expect integrity events
        tee.pipe(cacheStream)
        // TODO if the cache write fails, log a warning but return the response anyway
        // eslint-disable-next-line promise/catch-or-return
        cacheStream.promise().then(cacheWriteResolve, cacheWriteReject)
        body.unshift(tee)
        body.unshift(this.response.body)
      }

      body.once('resume', onResume)
      body.once('end', () => body.removeListener('resume', onResume))
    } else {
      await cacache.index.insert(this.options.cachePath, this.key, null, cacheOpts)
    }

    // note: we do not set the x-local-cache-hash header because we do not know
    // the hash value until after the write to the cache completes, which doesn't
    // happen until after the response has been sent and it's too late to write
    // the header anyway
    this.response.headers.set('x-local-cache', encodeURIComponent(this.options.cachePath))
    this.response.headers.set('x-local-cache-key', encodeURIComponent(this.key))
    this.response.headers.set('x-local-cache-mode', 'stream')
    this.response.headers.set('x-local-cache-status', status)
    this.response.headers.set('x-local-cache-time', new Date().toISOString())
    const newResponse = new Response(body, {
      url: this.response.url,
      status: this.response.status,
      headers: this.response.headers,
      counter: this.options.counter,
    })
    return newResponse
  }

  // use the cached data to create a response and return it
  async respond (method, options, status) {
    let response
    if (method === 'HEAD' || [301, 308].includes(this.response.status)) {
      // if the request is a HEAD, or the response is a redirect,
      // then the metadata in the entry already includes everything
      // we need to build a response
      response = this.response
    } else {
      // we're responding with a full cached response, so create a body
      // that reads from cacache and attach it to a new Response
      const body = new Minipass()
      const headers = { ...this.policy.responseHeaders() }

      const onResume = () => {
        const cacheStream = cacache.get.stream.byDigest(
          this.options.cachePath, this.entry.integrity, { memoize: this.options.memoize }
        )
        cacheStream.on('error', async (err) => {
          cacheStream.pause()
          if (err.code === 'EINTEGRITY') {
            await cacache.rm.content(
              this.options.cachePath, this.entry.integrity, { memoize: this.options.memoize }
            )
          }
          if (err.code === 'ENOENT' || err.code === 'EINTEGRITY') {
            await CacheEntry.invalidate(this.request, this.options)
          }
          body.emit('error', err)
          cacheStream.resume()
        })
        // emit the integrity and size events based on our metadata so we're consistent
        body.emit('integrity', this.entry.integrity)
        body.emit('size', Number(headers['content-length']))
        cacheStream.pipe(body)
      }

      body.once('resume', onResume)
      body.once('end', () => body.removeListener('resume', onResume))
      response = new Response(body, {
        url: this.entry.metadata.url,
        counter: options.counter,
        status: 200,
        headers,
      })
    }

    response.headers.set('x-local-cache', encodeURIComponent(this.options.cachePath))
    response.headers.set('x-local-cache-hash', encodeURIComponent(this.entry.integrity))
    response.headers.set('x-local-cache-key', encodeURIComponent(this.key))
    response.headers.set('x-local-cache-mode', 'stream')
    response.headers.set('x-local-cache-status', status)
    response.headers.set('x-local-cache-time', new Date(this.entry.metadata.time).toUTCString())
    return response
  }

  // use the provided request along with this cache entry to
  // revalidate the stored response. returns a response, either
  // from the cache or from the update
  async revalidate (request, options) {
    const revalidateRequest = new Request(request, {
      headers: this.policy.revalidationHeaders(request),
    })

    try {
      // NOTE: be sure to remove the headers property from the
      // user supplied options, since we have already defined
      // them on the new request object. if they're still in the
      // options then those will overwrite the ones from the policy
      var response = await remote(revalidateRequest, {
        ...options,
        headers: undefined,
      })
    } catch (err) {
      // if the network fetch fails, return the stale
      // cached response unless it has a cache-control
      // of 'must-revalidate'
      if (!this.policy.mustRevalidate) {
        return this.respond(request.method, options, 'stale')
      }

      throw err
    }

    if (this.policy.revalidated(revalidateRequest, response)) {
      // we got a 304, write a new index to the cache and respond from cache
      const metadata = getMetadata(request, response, options)
      // 304 responses do not include headers that are specific to the response data
      // since they do not include a body, so we copy values for headers that were
      // in the old cache entry to the new one, if the new metadata does not already
      // include that header
      for (const name of KEEP_RESPONSE_HEADERS) {
        if (
          !hasOwnProperty(metadata.resHeaders, name) &&
          hasOwnProperty(this.entry.metadata.resHeaders, name)
        ) {
          metadata.resHeaders[name] = this.entry.metadata.resHeaders[name]
        }
      }

      for (const name of options.cacheAdditionalHeaders) {
        const inMeta = hasOwnProperty(metadata.resHeaders, name)
        const inEntry = hasOwnProperty(this.entry.metadata.resHeaders, name)
        const inPolicy = hasOwnProperty(this.policy.response.headers, name)

        // if the header is in the existing entry, but it is not in the metadata
        // then we need to write it to the metadata as this will refresh the on-disk cache
        if (!inMeta && inEntry) {
          metadata.resHeaders[name] = this.entry.metadata.resHeaders[name]
        }
        // if the header is in the metadata, but not in the policy, then we need to set
        // it in the policy so that it's included in the immediate response. future
        // responses will load a new cache entry, so we don't need to change that
        if (!inPolicy && inMeta) {
          this.policy.response.headers[name] = metadata.resHeaders[name]
        }
      }

      try {
        await cacache.index.insert(options.cachePath, this.key, this.entry.integrity, {
          size: this.entry.size,
          metadata,
        })
      } catch (err) {
        // if updating the cache index fails, we ignore it and
        // respond anyway
      }
      return this.respond(request.method, options, 'revalidated')
    }

    // if we got a modified response, create a new entry based on it
    const newEntry = new CacheEntry({
      request,
      response,
      options,
    })

    // respond with the new entry while writing it to the cache
    return newEntry.store('updated')
  }
}

module.exports = CacheEntry
