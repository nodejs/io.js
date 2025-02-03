'use strict'

const assert = require('node:assert')
const { Readable } = require('node:stream')
const util = require('../core/util')
const CacheHandler = require('../handler/cache-handler')
const MemoryCacheStore = require('../cache/memory-cache-store')
const CacheRevalidationHandler = require('../handler/cache-revalidation-handler')
const { assertCacheStore, assertCacheMethods, makeCacheKey, parseCacheControlHeader } = require('../util/cache.js')
const { AbortError } = require('../core/errors.js')

/**
 * @typedef {(options: import('../../types/dispatcher.d.ts').default.DispatchOptions, handler: import('../../types/dispatcher.d.ts').default.DispatchHandler) => void} DispatchFn
 */

/**
 * @param {import('../../types/cache-interceptor.d.ts').default.GetResult} result
 * @param {import('../../types/cache-interceptor.d.ts').default.CacheControlDirectives | undefined} cacheControlDirectives
 * @returns {boolean}
 */
function needsRevalidation (result, cacheControlDirectives) {
  if (cacheControlDirectives?.['no-cache']) {
    // Always revalidate requests with the no-cache directive
    return true
  }

  const now = Date.now()
  if (now > result.staleAt) {
    // Response is stale
    if (cacheControlDirectives?.['max-stale']) {
      // There's a threshold where we can serve stale responses, let's see if
      //  we're in it
      // https://www.rfc-editor.org/rfc/rfc9111.html#name-max-stale
      const gracePeriod = result.staleAt + (cacheControlDirectives['max-stale'] * 1000)
      return now > gracePeriod
    }

    return true
  }

  if (cacheControlDirectives?.['min-fresh']) {
    // https://www.rfc-editor.org/rfc/rfc9111.html#section-5.2.1.3

    // At this point, staleAt is always > now
    const timeLeftTillStale = result.staleAt - now
    const threshold = cacheControlDirectives['min-fresh'] * 1000

    return timeLeftTillStale <= threshold
  }

  return false
}

/**
 * @param {DispatchFn} dispatch
 * @param {import('../../types/cache-interceptor.d.ts').default.CacheHandlerOptions} globalOpts
 * @param {import('../../types/cache-interceptor.d.ts').default.CacheKey} cacheKey
 * @param {import('../../types/dispatcher.d.ts').default.DispatchHandler} handler
 * @param {import('../../types/dispatcher.d.ts').default.RequestOptions} opts
 * @param {import('../../types/cache-interceptor.d.ts').default.CacheControlDirectives | undefined} reqCacheControl
 */
function handleUncachedResponse (
  dispatch,
  globalOpts,
  cacheKey,
  handler,
  opts,
  reqCacheControl
) {
  if (reqCacheControl?.['only-if-cached']) {
    let aborted = false
    try {
      if (typeof handler.onConnect === 'function') {
        handler.onConnect(() => {
          aborted = true
        })

        if (aborted) {
          return
        }
      }

      if (typeof handler.onHeaders === 'function') {
        handler.onHeaders(504, [], () => {}, 'Gateway Timeout')
        if (aborted) {
          return
        }
      }

      if (typeof handler.onComplete === 'function') {
        handler.onComplete([])
      }
    } catch (err) {
      if (typeof handler.onError === 'function') {
        handler.onError(err)
      }
    }

    return true
  }

  return dispatch(opts, new CacheHandler(globalOpts, cacheKey, handler))
}

/**
 * @param {import('../../types/dispatcher.d.ts').default.DispatchHandler} handler
 * @param {import('../../types/dispatcher.d.ts').default.RequestOptions} opts
 * @param {import('../../types/cache-interceptor.d.ts').default.GetResult} result
 * @param {number} age
 * @param {any} context
 * @param {boolean} isStale
 */
function sendCachedValue (handler, opts, result, age, context, isStale) {
  // TODO (perf): Readable.from path can be optimized...
  const stream = util.isStream(result.body)
    ? result.body
    : Readable.from(result.body ?? [])

  assert(!stream.destroyed, 'stream should not be destroyed')
  assert(!stream.readableDidRead, 'stream should not be readableDidRead')

  const controller = {
    resume () {
      stream.resume()
    },
    pause () {
      stream.pause()
    },
    get paused () {
      return stream.isPaused()
    },
    get aborted () {
      return stream.destroyed
    },
    get reason () {
      return stream.errored
    },
    abort (reason) {
      stream.destroy(reason ?? new AbortError())
    }
  }

  stream
    .on('error', function (err) {
      if (!this.readableEnded) {
        if (typeof handler.onResponseError === 'function') {
          handler.onResponseError(controller, err)
        } else {
          throw err
        }
      }
    })
    .on('close', function () {
      if (!this.errored) {
        handler.onResponseEnd?.(controller, {})
      }
    })

  handler.onRequestStart?.(controller, context)

  if (stream.destroyed) {
    return
  }

  // Add the age header
  // https://www.rfc-editor.org/rfc/rfc9111.html#name-age
  const headers = { ...result.headers, age: String(age) }

  if (isStale) {
    // Add warning header
    //  https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Warning
    headers.warning = '110 - "response is stale"'
  }

  handler.onResponseStart?.(controller, result.statusCode, headers, result.statusMessage)

  if (opts.method === 'HEAD') {
    stream.destroy()
  } else {
    stream.on('data', function (chunk) {
      handler.onResponseData?.(controller, chunk)
    })
  }
}

/**
 * @param {DispatchFn} dispatch
 * @param {import('../../types/cache-interceptor.d.ts').default.CacheHandlerOptions} globalOpts
 * @param {import('../../types/cache-interceptor.d.ts').default.CacheKey} cacheKey
 * @param {import('../../types/dispatcher.d.ts').default.DispatchHandler} handler
 * @param {import('../../types/dispatcher.d.ts').default.RequestOptions} opts
 * @param {import('../../types/cache-interceptor.d.ts').default.CacheControlDirectives | undefined} reqCacheControl
 * @param {import('../../types/cache-interceptor.d.ts').default.GetResult | undefined} result
 */
function handleResult (
  dispatch,
  globalOpts,
  cacheKey,
  handler,
  opts,
  reqCacheControl,
  result
) {
  if (!result) {
    return handleUncachedResponse(dispatch, globalOpts, cacheKey, handler, opts, reqCacheControl)
  }

  const now = Date.now()
  if (now > result.deleteAt) {
    // Response is expired, cache store shouldn't have given this to us
    return dispatch(opts, new CacheHandler(globalOpts, cacheKey, handler))
  }

  const age = Math.round((now - result.cachedAt) / 1000)
  if (reqCacheControl?.['max-age'] && age >= reqCacheControl['max-age']) {
    // Response is considered expired for this specific request
    //  https://www.rfc-editor.org/rfc/rfc9111.html#section-5.2.1.1
    return dispatch(opts, handler)
  }

  // Check if the response is stale
  if (needsRevalidation(result, reqCacheControl)) {
    if (util.isStream(opts.body) && util.bodyLength(opts.body) !== 0) {
      // If body is is stream we can't revalidate...
      // TODO (fix): This could be less strict...
      return dispatch(opts, new CacheHandler(globalOpts, cacheKey, handler))
    }

    let withinStaleIfErrorThreshold = false
    const staleIfErrorExpiry = result.cacheControlDirectives['stale-if-error'] ?? reqCacheControl?.['stale-if-error']
    if (staleIfErrorExpiry) {
      withinStaleIfErrorThreshold = now < (result.staleAt + (staleIfErrorExpiry * 1000))
    }

    let headers = {
      ...opts.headers,
      'if-modified-since': new Date(result.cachedAt).toUTCString()
    }

    if (result.etag) {
      headers['if-none-match'] = result.etag
    }

    if (result.vary) {
      headers = {
        ...headers,
        ...result.vary
      }
    }

    // We need to revalidate the response
    return dispatch(
      {
        ...opts,
        headers
      },
      new CacheRevalidationHandler(
        (success, context) => {
          if (success) {
            sendCachedValue(handler, opts, result, age, context, true)
          } else if (util.isStream(result.body)) {
            result.body.on('error', () => {}).destroy()
          }
        },
        new CacheHandler(globalOpts, cacheKey, handler),
        withinStaleIfErrorThreshold
      )
    )
  }

  // Dump request body.
  if (util.isStream(opts.body)) {
    opts.body.on('error', () => {}).destroy()
  }

  sendCachedValue(handler, opts, result, age, null, false)
}

/**
 * @param {import('../../types/cache-interceptor.d.ts').default.CacheOptions} [opts]
 * @returns {import('../../types/dispatcher.d.ts').default.DispatcherComposeInterceptor}
 */
module.exports = (opts = {}) => {
  const {
    store = new MemoryCacheStore(),
    methods = ['GET'],
    cacheByDefault = undefined,
    type = 'shared'
  } = opts

  if (typeof opts !== 'object' || opts === null) {
    throw new TypeError(`expected type of opts to be an Object, got ${opts === null ? 'null' : typeof opts}`)
  }

  assertCacheStore(store, 'opts.store')
  assertCacheMethods(methods, 'opts.methods')

  if (typeof cacheByDefault !== 'undefined' && typeof cacheByDefault !== 'number') {
    throw new TypeError(`exepcted opts.cacheByDefault to be number or undefined, got ${typeof cacheByDefault}`)
  }

  if (typeof type !== 'undefined' && type !== 'shared' && type !== 'private') {
    throw new TypeError(`exepcted opts.type to be shared, private, or undefined, got ${typeof type}`)
  }

  const globalOpts = {
    store,
    methods,
    cacheByDefault,
    type
  }

  const safeMethodsToNotCache = util.safeHTTPMethods.filter(method => methods.includes(method) === false)

  return dispatch => {
    return (opts, handler) => {
      if (!opts.origin || safeMethodsToNotCache.includes(opts.method)) {
        // Not a method we want to cache or we don't have the origin, skip
        return dispatch(opts, handler)
      }

      const reqCacheControl = opts.headers?.['cache-control']
        ? parseCacheControlHeader(opts.headers['cache-control'])
        : undefined

      if (reqCacheControl?.['no-store']) {
        return dispatch(opts, handler)
      }

      /**
       * @type {import('../../types/cache-interceptor.d.ts').default.CacheKey}
       */
      const cacheKey = makeCacheKey(opts)
      const result = store.get(cacheKey)

      if (result && typeof result.then === 'function') {
        result.then(result => {
          handleResult(dispatch,
            globalOpts,
            cacheKey,
            handler,
            opts,
            reqCacheControl,
            result
          )
        })
      } else {
        handleResult(
          dispatch,
          globalOpts,
          cacheKey,
          handler,
          opts,
          reqCacheControl,
          result
        )
      }

      return true
    }
  }
}
