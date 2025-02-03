'use strict';

const {
  ArrayPrototypeMap,
  ObjectDefineProperty,
  ReflectApply,
  Symbol,
} = primordials;

const {
  DNSException,
  codes: {
    ERR_INVALID_ARG_TYPE,
    ERR_INVALID_ARG_VALUE,
  },
} = require('internal/errors');

const {
  createResolverClass,
} = require('internal/dns/utils');

const {
  validateFunction,
  validateString,
} = require('internal/validators');

const {
  QueryReqWrap,
} = internalBinding('cares_wrap');

const {
  hasObserver,
  startPerf,
  stopPerf,
} = require('internal/perf/observe');

const kPerfHooksDnsLookupResolveContext = Symbol('kPerfHooksDnsLookupResolveContext');

function onresolve(err, result, ttls) {
  if (ttls && this.ttl)
    result = ArrayPrototypeMap(
      result, (address, index) => ({ address, ttl: ttls[index] }));

  if (err)
    this.callback(new DNSException(err, this.bindingName, this.hostname));
  else {
    this.callback(null, result);
    if (this[kPerfHooksDnsLookupResolveContext] && hasObserver('dns')) {
      stopPerf(this, kPerfHooksDnsLookupResolveContext, { detail: { result } });
    }
  }
}

function resolver(bindingName) {
  function query(name, /* options, */ callback) {
    let options;
    if (arguments.length > 2) {
      options = callback;
      callback = arguments[2];
    }

    validateString(name, 'name');
    validateFunction(callback, 'callback');

    const req = new QueryReqWrap();
    req.bindingName = bindingName;
    req.callback = callback;
    req.hostname = name;
    req.oncomplete = onresolve;
    req.ttl = !!(options?.ttl);
    const err = this._handle[bindingName](req, name);
    if (err) throw new DNSException(err, bindingName, name);
    if (hasObserver('dns')) {
      startPerf(req, kPerfHooksDnsLookupResolveContext, {
        type: 'dns',
        name: bindingName,
        detail: {
          host: name,
          ttl: req.ttl,
        },
      });
    }
    return req;
  }
  ObjectDefineProperty(query, 'name', { __proto__: null, value: bindingName });
  return query;
}

// This is the callback-based resolver. There is another similar
// resolver in dns/promises.js with resolve methods that are based
// on promises instead.
const { Resolver, resolveMap } = createResolverClass(resolver);
Resolver.prototype.resolve = resolve;

function resolve(hostname, rrtype, callback) {
  let resolver;
  if (typeof rrtype === 'string') {
    resolver = resolveMap[rrtype];
  } else if (typeof rrtype === 'function') {
    resolver = resolveMap.A;
    callback = rrtype;
  } else {
    throw new ERR_INVALID_ARG_TYPE('rrtype', 'string', rrtype);
  }

  if (typeof resolver === 'function') {
    return ReflectApply(resolver, this, [hostname, callback]);
  }
  throw new ERR_INVALID_ARG_VALUE('rrtype', rrtype);
}

module.exports = {
  Resolver,
};
