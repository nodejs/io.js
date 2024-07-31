'use strict';
const {
  ObjectPrototypeHasOwnProperty,
  PromisePrototypeThen,
  SafeMap,
  StringPrototypeEndsWith,
  StringPrototypeSlice,
  StringPrototypeStartsWith,
} = primordials;
const {
  Buffer: { concat: BufferConcat },
} = require('buffer');
const {
  ERR_NETWORK_IMPORT_DISALLOWED,
  ERR_NETWORK_IMPORT_BAD_RESPONSE,
  ERR_MODULE_NOT_FOUND,
} = require('internal/errors').codes;
const { URL } = require('internal/url');
const net = require('net');
const { once } = require('events');
const { compose } = require('stream');
/**
 * @typedef CacheEntry
 * @property {Promise<string> | string} resolvedHREF Parsed HREF of the request.
 * @property {Record<string, string>} headers HTTP headers of the response.
 * @property {Promise<Buffer> | Buffer} body Response body.
 */

/**
 * Only for GET requests, other requests would need new Map
 * HTTP cache semantics keep diff caches
 *
 * It caches either the promise or the cache entry since import.meta.url needs
 * the value synchronously for the response location after all redirects.
 *
 * Maps HREF to pending cache entry
 * @type {Map<string, Promise<CacheEntry> | CacheEntry>}
 */
const cacheForGET = new SafeMap();

// [1] The V8 snapshot doesn't like some C++ APIs to be loaded eagerly. Do it
// lazily/at runtime and not top level of an internal module.

// [2] Creating a new agent instead of using the gloabl agent improves
// performance and precludes the agent becoming tainted.

/** @type {import('https').Agent} The Cached HTTP Agent for **secure** HTTP requests. */
let HTTPSAgent;
/**
 * Make a HTTPs GET request (handling agent setup if needed, caching the agent to avoid
 * redudant instantiations).
 * @param {Parameters<import('https').get>[0]} input - The URI to fetch.
 * @param {Parameters<import('https').get>[1]} options - See https.get() options.
 */
function HTTPSGet(input, options) {
  const https = require('https'); // [1]
  HTTPSAgent ??= new https.Agent({ // [2]
    keepAlive: true,
  });
  return https.get(input, {
    agent: HTTPSAgent,
    ...options,
  });
}

/** @type {import('https').Agent} The Cached HTTP Agent for **insecure** HTTP requests. */
let HTTPAgent;
/**
 * Make a HTTP GET request (handling agent setup if needed, caching the agent to avoid
 * redudant instantiations).
 * @param {Parameters<import('http').get>[0]} input - The URI to fetch.
 * @param {Parameters<import('http').get>[1]} options - See http.get() options.
 */
function HTTPGet(input, options) {
  const http = require('http'); // [1]
  HTTPAgent ??= new http.Agent({ // [2]
    keepAlive: true,
  });
  return http.get(input, {
    agent: HTTPAgent,
    ...options,
  });
}

/** @type {import('../../dns/promises.js').lookup} */
function dnsLookup(hostname, options) {
  // eslint-disable-next-line no-func-assign
  dnsLookup = require('dns/promises').lookup;
  return dnsLookup(hostname, options);
}

let zlib;
/**
 * Create a decompressor for the Brotli format.
 * @returns {import('zlib').BrotliDecompress}
 */
function createBrotliDecompress() {
  zlib ??= require('zlib'); // [1]
  // eslint-disable-next-line no-func-assign
  createBrotliDecompress = zlib.createBrotliDecompress;
  return createBrotliDecompress();
}

/**
 * Create an unzip handler.
 * @returns {import('zlib').Unzip}
 */
function createUnzip() {
  zlib ??= require('zlib'); // [1]
  // eslint-disable-next-line no-func-assign
  createUnzip = zlib.createUnzip;
  return createUnzip();
}

/**
 * Redirection status code as per section 6.4 of RFC 7231:
 * https://datatracker.ietf.org/doc/html/rfc7231#section-6.4
 * and RFC 7238:
 * https://datatracker.ietf.org/doc/html/rfc7238
 * @param {number} statusCode
 * @returns {boolean}
 */
function isRedirect(statusCode) {
  switch (statusCode) {
    case 300: // Multiple Choices
    case 301: // Moved Permanently
    case 302: // Found
    case 303: // See Other
    case 307: // Temporary Redirect
    case 308: // Permanent Redirect
      return true;
    default:
      return false;
  }
}

/**
 * @typedef AcceptMimes possible values of Accept header when fetching a module
 * @property {Promise<string> | string} default default Accept header value.
 * @property {Record<string, string>} json Accept header value when fetching module with importAttributes json.
 * @type {AcceptMimes}
 */
const acceptMimes = {
  __proto_: null,
  default: '*/*',
  json: 'application/json,*/*;charset=utf-8;q=0.5',
};

/**
 * @param {URL} parsed
 * @returns {Promise<CacheEntry> | CacheEntry}
 */
function fetchWithRedirects(parsed, context) {
  const existing = cacheForGET.get(parsed.href);
  if (existing) {
    return existing;
  }
  const handler = parsed.protocol === 'http:' ? HTTPGet : HTTPSGet;
  const result = (async () => {
    const accept = acceptMimes[context.importAttributes?.type] ?? acceptMimes.default;
    const req = handler(parsed, {
      headers: { Accept: accept },
    });
    // Note that `once` is used here to handle `error` and that it hits the
    // `finally` on network error/timeout.
    const { 0: res } = await once(req, 'response');
    try {
      const hasLocation = ObjectPrototypeHasOwnProperty(res.headers, 'location');
      if (isRedirect(res.statusCode) && hasLocation) {
        const location = new URL(res.headers.location, parsed);
        if (location.protocol !== 'http:' && location.protocol !== 'https:') {
          throw new ERR_NETWORK_IMPORT_DISALLOWED(
            res.headers.location,
            parsed.href,
            'cannot redirect to non-network location',
          );
        }
        const entry = await fetchWithRedirects(location, context);
        cacheForGET.set(parsed.href, entry);
        return entry;
      }
      if (res.statusCode === 404) {
        const err = new ERR_MODULE_NOT_FOUND(parsed.href, null, parsed);
        err.message = `Cannot find module '${parsed.href}', HTTP 404`;
        throw err;
      }
      // This condition catches all unsupported status codes, including
      // 3xx redirection codes without `Location` HTTP header.
      if (res.statusCode < 200 || res.statusCode >= 300) {
        throw new ERR_NETWORK_IMPORT_DISALLOWED(
          res.headers.location,
          parsed.href,
          'cannot redirect to non-network location');
      }
      const { headers } = res;
      const contentType = headers['content-type'];
      if (!contentType) {
        throw new ERR_NETWORK_IMPORT_BAD_RESPONSE(
          parsed.href,
          "the 'Content-Type' header is required",
        );
      }
      /**
       * @type {CacheEntry}
       */
      const entry = {
        resolvedHREF: parsed.href,
        headers: {
          'content-type': res.headers['content-type'],
        },
        body: (async () => {
          let bodyStream = res;
          if (res.headers['content-encoding'] === 'br') {
            bodyStream = compose(res, createBrotliDecompress());
          } else if (
            res.headers['content-encoding'] === 'gzip' ||
            res.headers['content-encoding'] === 'deflate'
          ) {
            bodyStream = compose(res, createUnzip());
          }
          const buffers = await bodyStream.toArray();
          const body = BufferConcat(buffers);
          entry.body = body;
          return body;
        })(),
      };
      cacheForGET.set(parsed.href, entry);
      await entry.body;
      return entry;
    } finally {
      req.destroy();
    }
  })();
  cacheForGET.set(parsed.href, result);
  return result;
}

const allowList = new net.BlockList();
allowList.addAddress('::1', 'ipv6');
allowList.addRange('127.0.0.1', '127.255.255.255');

/**
 * Returns if an address has local status by if it is going to a local
 * interface or is an address resolved by DNS to be a local interface
 * @param {string} hostname url.hostname to test
 * @returns {Promise<boolean>}
 */
async function isLocalAddress(hostname) {
  try {
    if (
      StringPrototypeStartsWith(hostname, '[') &&
      StringPrototypeEndsWith(hostname, ']')
    ) {
      hostname = StringPrototypeSlice(hostname, 1, -1);
    }
    const addr = await dnsLookup(hostname, { order: 'verbatim' });
    const ipv = addr.family === 4 ? 'ipv4' : 'ipv6';
    return allowList.check(addr.address, ipv);
  } catch {
    // If it errored, the answer is no.
  }
  return false;
}

/**
 * Fetches a location with a shared cache following redirects.
 * Does not respect HTTP cache headers.
 *
 * This splits the header and body Promises so that things only needing
 * headers don't need to wait on the body.
 *
 * In cases where the request & response have already settled, this returns the
 * cache value synchronously.
 * @param {URL} parsed
 * @param {ESModuleContext} context
 * @returns {ReturnType<typeof fetchWithRedirects>}
 */
function fetchModule(parsed, context) {
  const { parentURL } = context;
  const { href } = parsed;
  const existing = cacheForGET.get(href);
  if (existing) {
    return existing;
  }
  if (parsed.protocol === 'http:') {
    return PromisePrototypeThen(isLocalAddress(parsed.hostname), (is) => {
      if (is !== true) {
        throw new ERR_NETWORK_IMPORT_DISALLOWED(
          href,
          parentURL,
          'http can only be used to load local resources (use https instead).',
        );
      }
      return fetchWithRedirects(parsed, context);
    });
  }
  return fetchWithRedirects(parsed, context);
}

module.exports = {
  fetchModule,
};
