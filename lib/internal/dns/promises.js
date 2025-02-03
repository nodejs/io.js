'use strict';
const {
  ArrayPrototypeMap,
  ObjectDefineProperty,
  Promise,
  ReflectApply,
  Symbol,
} = primordials;

const {
  bindDefaultResolver,
  createResolverClass,
  validateHints,
  emitInvalidHostnameWarning,
  errorCodes: dnsErrorCodes,
  getDefaultResultOrder,
  setDefaultResultOrder,
  setDefaultResolver,
} = require('internal/dns/utils');

const {
  NODATA,
  FORMERR,
  SERVFAIL,
  NOTFOUND,
  NOTIMP,
  REFUSED,
  BADQUERY,
  BADNAME,
  BADFAMILY,
  BADRESP,
  CONNREFUSED,
  TIMEOUT,
  EOF,
  FILE,
  NOMEM,
  DESTRUCTION,
  BADSTR,
  BADFLAGS,
  NONAME,
  BADHINTS,
  NOTINITIALIZED,
  LOADIPHLPAPI,
  ADDRGETNETWORKPARAMS,
  CANCELLED,
} = dnsErrorCodes;
const {
  DNSException,
  codes: {
    ERR_INVALID_ARG_TYPE,
    ERR_INVALID_ARG_VALUE,
    ERR_MISSING_ARGS,
  },
} = require('internal/errors');
const { isIP } = require('internal/net');
const {
  getaddrinfo,
  getnameinfo,
  GetAddrInfoReqWrap,
  GetNameInfoReqWrap,
  QueryReqWrap,
  DNS_ORDER_VERBATIM,
  DNS_ORDER_IPV4_FIRST,
  DNS_ORDER_IPV6_FIRST,
} = internalBinding('cares_wrap');
const {
  validateBoolean,
  validateNumber,
  validateOneOf,
  validatePort,
  validateString,
} = require('internal/validators');

const kPerfHooksDnsLookupContext = Symbol('kPerfHooksDnsLookupContext');
const kPerfHooksDnsLookupServiceContext = Symbol('kPerfHooksDnsLookupServiceContext');
const kPerfHooksDnsLookupResolveContext = Symbol('kPerfHooksDnsLookupResolveContext');

const {
  hasObserver,
  startPerf,
  stopPerf,
} = require('internal/perf/observe');

function onlookup(err, addresses) {
  if (err) {
    this.reject(new DNSException(err, 'getaddrinfo', this.hostname));
    return;
  }

  const family = this.family || isIP(addresses[0]);
  this.resolve({ address: addresses[0], family });
  if (this[kPerfHooksDnsLookupContext] && hasObserver('dns')) {
    stopPerf(this, kPerfHooksDnsLookupContext, { detail: { addresses } });
  }
}

function onlookupall(err, addresses) {
  if (err) {
    this.reject(new DNSException(err, 'getaddrinfo', this.hostname));
    return;
  }

  const family = this.family;

  for (let i = 0; i < addresses.length; i++) {
    const address = addresses[i];

    addresses[i] = {
      address,
      family: family || isIP(addresses[i]),
    };
  }

  this.resolve(addresses);
  if (this[kPerfHooksDnsLookupContext] && hasObserver('dns')) {
    stopPerf(this, kPerfHooksDnsLookupContext, { detail: { addresses } });
  }
}

/**
 * Creates a promise that resolves with the IP address of the given hostname.
 * @param {0 | 4 | 6} family - The IP address family (4 or 6, or 0 for both).
 * @param {string} hostname - The hostname to resolve.
 * @param {boolean} all - Whether to resolve with all IP addresses for the hostname.
 * @param {number} hints - One or more supported getaddrinfo flags (supply multiple via
 * bitwise OR).
 * @param {number} dnsOrder - How to sort results. Must be `ipv4first`, `ipv6first` or `verbatim`.
 * @returns {Promise<DNSLookupResult | DNSLookupResult[]>} The IP address(es) of the hostname.
 * @typedef {object} DNSLookupResult
 * @property {string} address - The IP address.
 * @property {0 | 4 | 6} family - The IP address type. 4 for IPv4 or 6 for IPv6, or 0 (for both).
 */
function createLookupPromise(family, hostname, all, hints, dnsOrder) {
  return new Promise((resolve, reject) => {
    if (!hostname) {
      emitInvalidHostnameWarning(hostname);
      resolve(all ? [] : { address: null, family: family === 6 ? 6 : 4 });
      return;
    }

    const matchedFamily = isIP(hostname);

    if (matchedFamily !== 0) {
      const result = { address: hostname, family: matchedFamily };
      resolve(all ? [result] : result);
      return;
    }

    const req = new GetAddrInfoReqWrap();

    req.family = family;
    req.hostname = hostname;
    req.oncomplete = all ? onlookupall : onlookup;
    req.resolve = resolve;
    req.reject = reject;

    let order = DNS_ORDER_VERBATIM;

    if (dnsOrder === 'ipv4first') {
      order = DNS_ORDER_IPV4_FIRST;
    } else if (dnsOrder === 'ipv6first') {
      order = DNS_ORDER_IPV6_FIRST;
    }

    const err = getaddrinfo(req, hostname, family, hints, order);

    if (err) {
      reject(new DNSException(err, 'getaddrinfo', hostname));
    } else if (hasObserver('dns')) {
      const detail = {
        hostname,
        family,
        hints,
        verbatim: order === DNS_ORDER_VERBATIM,
        order: dnsOrder,
      };
      startPerf(req, kPerfHooksDnsLookupContext, { type: 'dns', name: 'lookup', detail });
    }
  });
}

const validFamilies = [0, 4, 6];
/**
 * Get the IP address for a given hostname.
 * @param {string} hostname - The hostname to resolve (ex. 'nodejs.org').
 * @param {object} [options] - Optional settings.
 * @param {boolean} [options.all=false] - Whether to return all or just the first resolved address.
 * @param {0 | 4 | 6} [options.family=0] - The record family. Must be 4, 6, or 0 (for both).
 * @param {number} [options.hints] - One or more supported getaddrinfo flags (supply multiple via
 * bitwise OR).
 * @param {string} [options.order='verbatim'] - Return results in same order DNS resolved them;
 * Must be `ipv4first`, `ipv6first` or `verbatim`.
 * New code should supply `verbatim`.
 */
function lookup(hostname, options) {
  let hints = 0;
  let family = 0;
  let all = false;
  let dnsOrder = getDefaultResultOrder();

  // Parse arguments
  if (hostname) {
    validateString(hostname, 'hostname');
  }

  if (typeof options === 'number') {
    validateOneOf(options, 'family', validFamilies);
    family = options;
  } else if (options !== undefined && typeof options !== 'object') {
    throw new ERR_INVALID_ARG_TYPE('options', ['integer', 'object'], options);
  } else {
    if (options?.hints != null) {
      validateNumber(options.hints, 'options.hints');
      hints = options.hints >>> 0;
      validateHints(hints);
    }
    if (options?.family != null) {
      validateOneOf(options.family, 'options.family', validFamilies);
      family = options.family;
    }
    if (options?.all != null) {
      validateBoolean(options.all, 'options.all');
      all = options.all;
    }
    if (options?.verbatim != null) {
      validateBoolean(options.verbatim, 'options.verbatim');
      dnsOrder = options.verbatim ? 'verbatim' : 'ipv4first';
    }
    if (options?.order != null) {
      validateOneOf(options.order, 'options.order', ['ipv4first', 'ipv6first', 'verbatim']);
      dnsOrder = options.order;
    }
  }

  return createLookupPromise(family, hostname, all, hints, dnsOrder);
}


function onlookupservice(err, hostname, service) {
  if (err) {
    this.reject(new DNSException(err, 'getnameinfo', this.host));
    return;
  }

  this.resolve({ hostname, service });
  if (this[kPerfHooksDnsLookupServiceContext] && hasObserver('dns')) {
    stopPerf(this, kPerfHooksDnsLookupServiceContext, { detail: { hostname, service } });
  }
}

function createLookupServicePromise(hostname, port) {
  return new Promise((resolve, reject) => {
    const req = new GetNameInfoReqWrap();

    req.hostname = hostname;
    req.port = port;
    req.oncomplete = onlookupservice;
    req.resolve = resolve;
    req.reject = reject;

    const err = getnameinfo(req, hostname, port);

    if (err)
      reject(new DNSException(err, 'getnameinfo', hostname));
    else if (hasObserver('dns')) {
      startPerf(req, kPerfHooksDnsLookupServiceContext, {
        type: 'dns',
        name: 'lookupService',
        detail: {
          host: hostname,
          port,
        },
      });
    }
  });
}

function lookupService(address, port) {
  if (arguments.length !== 2)
    throw new ERR_MISSING_ARGS('address', 'port');

  if (isIP(address) === 0)
    throw new ERR_INVALID_ARG_VALUE('address', address);

  validatePort(port);

  return createLookupServicePromise(address, +port);
}


function onresolve(err, result, ttls) {
  if (err) {
    this.reject(new DNSException(err, this.bindingName, this.hostname));
    return;
  }

  if (ttls && this.ttl)
    result = ArrayPrototypeMap(
      result, (address, index) => ({ address, ttl: ttls[index] }));

  this.resolve(result);
  if (this[kPerfHooksDnsLookupResolveContext] && hasObserver('dns')) {
    stopPerf(this, kPerfHooksDnsLookupResolveContext, { detail: { result } });
  }
}

function createResolverPromise(resolver, bindingName, hostname, ttl) {
  return new Promise((resolve, reject) => {
    const req = new QueryReqWrap();

    req.bindingName = bindingName;
    req.hostname = hostname;
    req.oncomplete = onresolve;
    req.resolve = resolve;
    req.reject = reject;
    req.ttl = ttl;

    const err = resolver._handle[bindingName](req, hostname);

    if (err)
      reject(new DNSException(err, bindingName, hostname));
    else if (hasObserver('dns')) {
      startPerf(req, kPerfHooksDnsLookupResolveContext, {
        type: 'dns',
        name: bindingName,
        detail: {
          host: hostname,
          ttl,
        },
      });
    }
  });
}

function resolver(bindingName) {
  function query(name, options) {
    validateString(name, 'name');

    const ttl = !!(options?.ttl);
    return createResolverPromise(this, bindingName, name, ttl);
  }

  ObjectDefineProperty(query, 'name', { __proto__: null, value: bindingName });
  return query;
}

function resolve(hostname, rrtype) {
  let resolver;

  if (rrtype !== undefined) {
    validateString(rrtype, 'rrtype');

    resolver = resolveMap[rrtype];

    if (typeof resolver !== 'function')
      throw new ERR_INVALID_ARG_VALUE('rrtype', rrtype);
  } else {
    resolver = resolveMap.A;
  }

  return ReflectApply(resolver, this, [hostname]);
}

// Promise-based resolver.
const { Resolver, resolveMap } = createResolverClass(resolver);
Resolver.prototype.resolve = resolve;

function defaultResolverSetServers(servers) {
  const resolver = new Resolver();

  resolver.setServers(servers);
  setDefaultResolver(resolver);
  bindDefaultResolver(module.exports, Resolver.prototype);
}

module.exports = {
  lookup,
  lookupService,
  Resolver,
  getDefaultResultOrder,
  setDefaultResultOrder,
  setServers: defaultResolverSetServers,

  // ERROR CODES
  NODATA,
  FORMERR,
  SERVFAIL,
  NOTFOUND,
  NOTIMP,
  REFUSED,
  BADQUERY,
  BADNAME,
  BADFAMILY,
  BADRESP,
  CONNREFUSED,
  TIMEOUT,
  EOF,
  FILE,
  NOMEM,
  DESTRUCTION,
  BADSTR,
  BADFLAGS,
  NONAME,
  BADHINTS,
  NOTINITIALIZED,
  LOADIPHLPAPI,
  ADDRGETNETWORKPARAMS,
  CANCELLED,
};
bindDefaultResolver(module.exports, Resolver.prototype);
