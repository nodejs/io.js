'use strict'
const { URL } = require('url')
const http = require('http')
const https = require('https')
const zlib = require('minizlib')
const { Minipass } = require('minipass')

const Body = require('./body.js')
const { writeToStream, getTotalBytes } = Body
const Response = require('./response.js')
const Headers = require('./headers.js')
const { createHeadersLenient } = Headers
const Request = require('./request.js')
const { getNodeRequestOptions } = Request
const FetchError = require('./fetch-error.js')
const AbortError = require('./abort-error.js')

// XXX this should really be split up and unit-ized for easier testing
// and better DRY implementation of data/http request aborting
const fetch = async (url, opts) => {
  if (/^data:/.test(url)) {
    const request = new Request(url, opts)
    // delay 1 promise tick so that the consumer can abort right away
    return Promise.resolve().then(() => new Promise((resolve, reject) => {
      let type, data
      try {
        const { pathname, search } = new URL(url)
        const split = pathname.split(',')
        if (split.length < 2) {
          throw new Error('invalid data: URI')
        }
        const mime = split.shift()
        const base64 = /;base64$/.test(mime)
        type = base64 ? mime.slice(0, -1 * ';base64'.length) : mime
        const rawData = decodeURIComponent(split.join(',') + search)
        data = base64 ? Buffer.from(rawData, 'base64') : Buffer.from(rawData)
      } catch (er) {
        return reject(new FetchError(`[${request.method}] ${
          request.url} invalid URL, ${er.message}`, 'system', er))
      }

      const { signal } = request
      if (signal && signal.aborted) {
        return reject(new AbortError('The user aborted a request.'))
      }

      const headers = { 'Content-Length': data.length }
      if (type) {
        headers['Content-Type'] = type
      }
      return resolve(new Response(data, { headers }))
    }))
  }

  return new Promise((resolve, reject) => {
    // build request object
    const request = new Request(url, opts)
    let options
    try {
      options = getNodeRequestOptions(request)
    } catch (er) {
      return reject(er)
    }

    const send = (options.protocol === 'https:' ? https : http).request
    const { signal } = request
    let response = null
    const abort = () => {
      const error = new AbortError('The user aborted a request.')
      reject(error)
      if (Minipass.isStream(request.body) &&
          typeof request.body.destroy === 'function') {
        request.body.destroy(error)
      }
      if (response && response.body) {
        response.body.emit('error', error)
      }
    }

    if (signal && signal.aborted) {
      return abort()
    }

    const abortAndFinalize = () => {
      abort()
      finalize()
    }

    const finalize = () => {
      req.abort()
      if (signal) {
        signal.removeEventListener('abort', abortAndFinalize)
      }
      clearTimeout(reqTimeout)
    }

    // send request
    const req = send(options)

    if (signal) {
      signal.addEventListener('abort', abortAndFinalize)
    }

    let reqTimeout = null
    if (request.timeout) {
      req.once('socket', () => {
        reqTimeout = setTimeout(() => {
          reject(new FetchError(`network timeout at: ${
            request.url}`, 'request-timeout'))
          finalize()
        }, request.timeout)
      })
    }

    req.on('error', er => {
      // if a 'response' event is emitted before the 'error' event, then by the
      // time this handler is run it's too late to reject the Promise for the
      // response. instead, we forward the error event to the response stream
      // so that the error will surface to the user when they try to consume
      // the body. this is done as a side effect of aborting the request except
      // for in windows, where we must forward the event manually, otherwise
      // there is no longer a ref'd socket attached to the request and the
      // stream never ends so the event loop runs out of work and the process
      // exits without warning.
      // coverage skipped here due to the difficulty in testing
      // istanbul ignore next
      if (req.res) {
        req.res.emit('error', er)
      }
      reject(new FetchError(`request to ${request.url} failed, reason: ${
        er.message}`, 'system', er))
      finalize()
    })

    req.on('response', res => {
      clearTimeout(reqTimeout)

      const headers = createHeadersLenient(res.headers)

      // HTTP fetch step 5
      if (fetch.isRedirect(res.statusCode)) {
        // HTTP fetch step 5.2
        const location = headers.get('Location')

        // HTTP fetch step 5.3
        let locationURL = null
        try {
          locationURL = location === null ? null : new URL(location, request.url).toString()
        } catch {
          // error here can only be invalid URL in Location: header
          // do not throw when options.redirect == manual
          // let the user extract the errorneous redirect URL
          if (request.redirect !== 'manual') {
            /* eslint-disable-next-line max-len */
            reject(new FetchError(`uri requested responds with an invalid redirect URL: ${location}`, 'invalid-redirect'))
            finalize()
            return
          }
        }

        // HTTP fetch step 5.5
        if (request.redirect === 'error') {
          reject(new FetchError('uri requested responds with a redirect, ' +
            `redirect mode is set to error: ${request.url}`, 'no-redirect'))
          finalize()
          return
        } else if (request.redirect === 'manual') {
          // node-fetch-specific step: make manual redirect a bit easier to
          // use by setting the Location header value to the resolved URL.
          if (locationURL !== null) {
            // handle corrupted header
            try {
              headers.set('Location', locationURL)
            } catch (err) {
              /* istanbul ignore next: nodejs server prevent invalid
                 response headers, we can't test this through normal
                 request */
              reject(err)
            }
          }
        } else if (request.redirect === 'follow' && locationURL !== null) {
          // HTTP-redirect fetch step 5
          if (request.counter >= request.follow) {
            reject(new FetchError(`maximum redirect reached at: ${
              request.url}`, 'max-redirect'))
            finalize()
            return
          }

          // HTTP-redirect fetch step 9
          if (res.statusCode !== 303 &&
              request.body &&
              getTotalBytes(request) === null) {
            reject(new FetchError(
              'Cannot follow redirect with body being a readable stream',
              'unsupported-redirect'
            ))
            finalize()
            return
          }

          // Update host due to redirection
          request.headers.set('host', (new URL(locationURL)).host)

          // HTTP-redirect fetch step 6 (counter increment)
          // Create a new Request object.
          const requestOpts = {
            headers: new Headers(request.headers),
            follow: request.follow,
            counter: request.counter + 1,
            agent: request.agent,
            compress: request.compress,
            method: request.method,
            body: request.body,
            signal: request.signal,
            timeout: request.timeout,
          }

          // if the redirect is to a new hostname, strip the authorization and cookie headers
          const parsedOriginal = new URL(request.url)
          const parsedRedirect = new URL(locationURL)
          if (parsedOriginal.hostname !== parsedRedirect.hostname) {
            requestOpts.headers.delete('authorization')
            requestOpts.headers.delete('cookie')
          }

          // HTTP-redirect fetch step 11
          if (res.statusCode === 303 || (
            (res.statusCode === 301 || res.statusCode === 302) &&
              request.method === 'POST'
          )) {
            requestOpts.method = 'GET'
            requestOpts.body = undefined
            requestOpts.headers.delete('content-length')
          }

          // HTTP-redirect fetch step 15
          resolve(fetch(new Request(locationURL, requestOpts)))
          finalize()
          return
        }
      } // end if(isRedirect)

      // prepare response
      res.once('end', () =>
        signal && signal.removeEventListener('abort', abortAndFinalize))

      const body = new Minipass()
      // if an error occurs, either on the response stream itself, on one of the
      // decoder streams, or a response length timeout from the Body class, we
      // forward the error through to our internal body stream. If we see an
      // error event on that, we call finalize to abort the request and ensure
      // we don't leave a socket believing a request is in flight.
      // this is difficult to test, so lacks specific coverage.
      body.on('error', finalize)
      // exceedingly rare that the stream would have an error,
      // but just in case we proxy it to the stream in use.
      res.on('error', /* istanbul ignore next */ er => body.emit('error', er))
      res.on('data', (chunk) => body.write(chunk))
      res.on('end', () => body.end())

      const responseOptions = {
        url: request.url,
        status: res.statusCode,
        statusText: res.statusMessage,
        headers: headers,
        size: request.size,
        timeout: request.timeout,
        counter: request.counter,
        trailer: new Promise(resolveTrailer =>
          res.on('end', () => resolveTrailer(createHeadersLenient(res.trailers)))),
      }

      // HTTP-network fetch step 12.1.1.3
      const codings = headers.get('Content-Encoding')

      // HTTP-network fetch step 12.1.1.4: handle content codings

      // in following scenarios we ignore compression support
      // 1. compression support is disabled
      // 2. HEAD request
      // 3. no Content-Encoding header
      // 4. no content response (204)
      // 5. content not modified response (304)
      if (!request.compress ||
          request.method === 'HEAD' ||
          codings === null ||
          res.statusCode === 204 ||
          res.statusCode === 304) {
        response = new Response(body, responseOptions)
        resolve(response)
        return
      }

      // Be less strict when decoding compressed responses, since sometimes
      // servers send slightly invalid responses that are still accepted
      // by common browsers.
      // Always using Z_SYNC_FLUSH is what cURL does.
      const zlibOptions = {
        flush: zlib.constants.Z_SYNC_FLUSH,
        finishFlush: zlib.constants.Z_SYNC_FLUSH,
      }

      // for gzip
      if (codings === 'gzip' || codings === 'x-gzip') {
        const unzip = new zlib.Gunzip(zlibOptions)
        response = new Response(
          // exceedingly rare that the stream would have an error,
          // but just in case we proxy it to the stream in use.
          body.on('error', /* istanbul ignore next */ er => unzip.emit('error', er)).pipe(unzip),
          responseOptions
        )
        resolve(response)
        return
      }

      // for deflate
      if (codings === 'deflate' || codings === 'x-deflate') {
        // handle the infamous raw deflate response from old servers
        // a hack for old IIS and Apache servers
        const raw = res.pipe(new Minipass())
        raw.once('data', chunk => {
          // see http://stackoverflow.com/questions/37519828
          const decoder = (chunk[0] & 0x0F) === 0x08
            ? new zlib.Inflate()
            : new zlib.InflateRaw()
          // exceedingly rare that the stream would have an error,
          // but just in case we proxy it to the stream in use.
          body.on('error', /* istanbul ignore next */ er => decoder.emit('error', er)).pipe(decoder)
          response = new Response(decoder, responseOptions)
          resolve(response)
        })
        return
      }

      // for br
      if (codings === 'br') {
        // ignoring coverage so tests don't have to fake support (or lack of) for brotli
        // istanbul ignore next
        try {
          var decoder = new zlib.BrotliDecompress()
        } catch (err) {
          reject(err)
          finalize()
          return
        }
        // exceedingly rare that the stream would have an error,
        // but just in case we proxy it to the stream in use.
        body.on('error', /* istanbul ignore next */ er => decoder.emit('error', er)).pipe(decoder)
        response = new Response(decoder, responseOptions)
        resolve(response)
        return
      }

      // otherwise, use response as-is
      response = new Response(body, responseOptions)
      resolve(response)
    })

    writeToStream(req, request)
  })
}

module.exports = fetch

fetch.isRedirect = code =>
  code === 301 ||
  code === 302 ||
  code === 303 ||
  code === 307 ||
  code === 308

fetch.Headers = Headers
fetch.Request = Request
fetch.Response = Response
fetch.FetchError = FetchError
fetch.AbortError = AbortError
