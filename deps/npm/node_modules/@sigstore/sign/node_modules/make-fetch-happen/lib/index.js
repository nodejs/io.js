const { FetchError, Headers, Request, Response } = require('minipass-fetch')

const configureOptions = require('./options.js')
const fetch = require('./fetch.js')

const makeFetchHappen = (url, opts) => {
  const options = configureOptions(opts)

  const request = new Request(url, options)
  return fetch(request, options)
}

makeFetchHappen.defaults = (defaultUrl, defaultOptions = {}, wrappedFetch = makeFetchHappen) => {
  if (typeof defaultUrl === 'object') {
    defaultOptions = defaultUrl
    defaultUrl = null
  }

  const defaultedFetch = (url, options = {}) => {
    const finalUrl = url || defaultUrl
    const finalOptions = {
      ...defaultOptions,
      ...options,
      headers: {
        ...defaultOptions.headers,
        ...options.headers,
      },
    }
    return wrappedFetch(finalUrl, finalOptions)
  }

  defaultedFetch.defaults = (defaultUrl1, defaultOptions1 = {}) =>
    makeFetchHappen.defaults(defaultUrl1, defaultOptions1, defaultedFetch)
  return defaultedFetch
}

module.exports = makeFetchHappen
module.exports.FetchError = FetchError
module.exports.Headers = Headers
module.exports.Request = Request
module.exports.Response = Response
