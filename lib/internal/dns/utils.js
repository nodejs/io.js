'use strict';

const {
  ArrayPrototypeForEach,
  ArrayPrototypeMap,
  ArrayPrototypePush,
  FunctionPrototypeBind,
  NumberParseInt,
  RegExpPrototypeExec,
  RegExpPrototypeSymbolReplace,
  Symbol,
} = primordials;

const {
  codes: {
    ERR_DNS_SET_SERVERS_FAILED,
    ERR_INVALID_ARG_VALUE,
    ERR_INVALID_IP_ADDRESS,
  },
} = require('internal/errors');
const { isIP } = require('internal/net');
const { getOptionValue } = require('internal/options');
const {
  validateArray,
  validateInt32,
  validateOneOf,
  validateString,
} = require('internal/validators');
let binding;
function lazyBinding() {
  binding ??= internalBinding('cares_wrap');
  return binding;
}
const IANA_DNS_PORT = 53;
const IPv6RE = /^\[([^[\]]*)\]/;
const addrSplitRE = /(^.+?)(?::(\d+))?$/;

const {
  namespace: {
    addSerializeCallback,
    addDeserializeCallback,
    isBuildingSnapshot,
  },
} = require('internal/v8/startup_snapshot');

function validateTimeout(options) {
  const { timeout = -1 } = { ...options };
  validateInt32(timeout, 'options.timeout', -1);
  return timeout;
}

function validateTries(options) {
  const { tries = 4 } = { ...options };
  validateInt32(tries, 'options.tries', 1);
  return tries;
}

const kSerializeResolver = Symbol('dns:resolver:serialize');
const kDeserializeResolver = Symbol('dns:resolver:deserialize');
const kSnapshotStates = Symbol('dns:resolver:config');
const kInitializeHandle = Symbol('dns:resolver:initializeHandle');
const kSetServersInternal = Symbol('dns:resolver:setServers');

// Resolver instances correspond 1:1 to c-ares channels.

class ResolverBase {
  constructor(options = undefined) {
    const timeout = validateTimeout(options);
    const tries = validateTries(options);
    // If we are building snapshot, save the states of the resolver along
    // the way.
    if (isBuildingSnapshot()) {
      this[kSnapshotStates] = { timeout, tries };
    }
    this[kInitializeHandle](timeout, tries);
  }

  [kInitializeHandle](timeout, tries) {
    const { ChannelWrap } = lazyBinding();
    this._handle = new ChannelWrap(timeout, tries);
  }

  cancel() {
    this._handle.cancel();
  }

  getServers() {
    return ArrayPrototypeMap(this._handle.getServers() || [], (val) => {
      if (!val[1] || val[1] === IANA_DNS_PORT)
        return val[0];

      const host = isIP(val[0]) === 6 ? `[${val[0]}]` : val[0];
      return `${host}:${val[1]}`;
    });
  }

  setServers(servers) {
    validateArray(servers, 'servers');

    // Cache the original servers because in the event of an error while
    // setting the servers, c-ares won't have any servers available for
    // resolution.
    const newSet = [];
    ArrayPrototypeForEach(servers, (serv, index) => {
      validateString(serv, `servers[${index}]`);
      let ipVersion = isIP(serv);

      if (ipVersion !== 0)
        return ArrayPrototypePush(newSet, [ipVersion, serv, IANA_DNS_PORT]);

      const match = RegExpPrototypeExec(IPv6RE, serv);

      // Check for an IPv6 in brackets.
      if (match) {
        ipVersion = isIP(match[1]);

        if (ipVersion !== 0) {
          const port = NumberParseInt(
            RegExpPrototypeSymbolReplace(addrSplitRE, serv, '$2')) || IANA_DNS_PORT;
          return ArrayPrototypePush(newSet, [ipVersion, match[1], port]);
        }
      }

      // addr::port
      const addrSplitMatch = RegExpPrototypeExec(addrSplitRE, serv);

      if (addrSplitMatch) {
        const hostIP = addrSplitMatch[1];
        const port = addrSplitMatch[2] || IANA_DNS_PORT;

        ipVersion = isIP(hostIP);

        if (ipVersion !== 0) {
          return ArrayPrototypePush(
            newSet, [ipVersion, hostIP, NumberParseInt(port)]);
        }
      }

      throw new ERR_INVALID_IP_ADDRESS(serv);
    });

    this[kSetServersInternal](newSet, servers);
  }

  [kSetServersInternal](newSet, servers) {
    const orig = ArrayPrototypeMap(this._handle.getServers() || [], (val) => {
      val.unshift(isIP(val[0]));
      return val;
    });
    const errorNumber = this._handle.setServers(newSet);

    if (errorNumber !== 0) {
      // Reset the servers to the old servers, because ares probably unset them.
      this._handle.setServers(orig);
      const { strerror } = lazyBinding();
      const err = strerror(errorNumber);
      throw new ERR_DNS_SET_SERVERS_FAILED(err, servers);
    }

    if (isBuildingSnapshot()) {
      this[kSnapshotStates].servers = newSet;
    }
  }


  setLocalAddress(ipv4, ipv6) {
    validateString(ipv4, 'ipv4');

    if (ipv6 !== undefined) {
      validateString(ipv6, 'ipv6');
    }

    this._handle.setLocalAddress(ipv4, ipv6);

    if (isBuildingSnapshot()) {
      this[kSnapshotStates].localAddress = { ipv4, ipv6 };
    }
  }

  // TODO(joyeecheung): consider exposing this if custom DNS resolvers
  // end up being useful for snapshot users.
  [kSerializeResolver]() {
    this._handle = null;  // We'll restore it during deserialization.
    addDeserializeCallback(function deserializeResolver(resolver) {
      resolver[kDeserializeResolver]();
    }, this);
  }

  [kDeserializeResolver]() {
    const { timeout, tries, localAddress, servers } = this[kSnapshotStates];
    this[kInitializeHandle](timeout, tries);
    if (localAddress) {
      const { ipv4, ipv6 } = localAddress;
      this._handle.setLocalAddress(ipv4, ipv6);
    }
    if (servers) {
      this[kSetServersInternal](servers, servers);
    }
  }
}

let defaultResolver;
let dnsOrder;

function initializeDns() {
  const orderFromCLI = getOptionValue('--dns-result-order');
  if (!orderFromCLI) {
    dnsOrder ??= 'verbatim';
  } else {
    // Allow the deserialized application to override order from CLI.
    validateOneOf(orderFromCLI, '--dns-result-order', ['verbatim', 'ipv4first', 'ipv6first']);
    dnsOrder = orderFromCLI;
  }

  if (!isBuildingSnapshot()) {
    return;
  }

  addSerializeCallback(() => {
    defaultResolver?.[kSerializeResolver]();
  });
}

const resolverKeys = [
  'getServers',
  'resolve',
  'resolve4',
  'resolve6',
  'resolveAny',
  'resolveCaa',
  'resolveCname',
  'resolveMx',
  'resolveNaptr',
  'resolveNs',
  'resolvePtr',
  'resolveSoa',
  'resolveSrv',
  'resolveTxt',
  'reverse',
];

function getDefaultResolver() {
  // We do this here instead of pre-execution so that the default resolver is
  // only ever created when the user loads any dns module.
  if (defaultResolver === undefined) {
    defaultResolver = new ResolverBase();
  }
  return defaultResolver;
}

function setDefaultResolver(resolver) {
  defaultResolver = resolver;
}

function bindDefaultResolver(target, source) {
  const defaultResolver = getDefaultResolver();
  ArrayPrototypeForEach(resolverKeys, (key) => {
    target[key] = FunctionPrototypeBind(source[key], defaultResolver);
  });
}

function validateHints(hints) {
  const { AI_ADDRCONFIG, AI_ALL, AI_V4MAPPED } = lazyBinding();
  if ((hints & ~(AI_ADDRCONFIG | AI_ALL | AI_V4MAPPED)) !== 0) {
    throw new ERR_INVALID_ARG_VALUE('hints', hints);
  }
}

let invalidHostnameWarningEmitted = false;
function emitInvalidHostnameWarning(hostname) {
  if (!invalidHostnameWarningEmitted) {
    process.emitWarning(
      `The provided hostname "${hostname}" is not a valid ` +
      'hostname, and is supported in the dns module solely for compatibility.',
      'DeprecationWarning',
      'DEP0118',
    );
    invalidHostnameWarningEmitted = true;
  }
}

function setDefaultResultOrder(value) {
  validateOneOf(value, 'dnsOrder', ['verbatim', 'ipv4first', 'ipv6first']);
  dnsOrder = value;
}

function getDefaultResultOrder() {
  return dnsOrder;
}

function createResolverClass(resolver) {
  const resolveMap = { __proto__: null };

  class Resolver extends ResolverBase {}

  Resolver.prototype.resolveAny = resolveMap.ANY = resolver('queryAny');
  Resolver.prototype.resolve4 = resolveMap.A = resolver('queryA');
  Resolver.prototype.resolve6 = resolveMap.AAAA = resolver('queryAaaa');
  Resolver.prototype.resolveCaa = resolveMap.CAA = resolver('queryCaa');
  Resolver.prototype.resolveCname = resolveMap.CNAME = resolver('queryCname');
  Resolver.prototype.resolveMx = resolveMap.MX = resolver('queryMx');
  Resolver.prototype.resolveNs = resolveMap.NS = resolver('queryNs');
  Resolver.prototype.resolveTxt = resolveMap.TXT = resolver('queryTxt');
  Resolver.prototype.resolveSrv = resolveMap.SRV = resolver('querySrv');
  Resolver.prototype.resolvePtr = resolveMap.PTR = resolver('queryPtr');
  Resolver.prototype.resolveNaptr = resolveMap.NAPTR = resolver('queryNaptr');
  Resolver.prototype.resolveSoa = resolveMap.SOA = resolver('querySoa');
  Resolver.prototype.reverse = resolver('getHostByAddr');

  return {
    resolveMap,
    Resolver,
  };
}

// ERROR CODES
const errorCodes = {
  NODATA: 'ENODATA',
  FORMERR: 'EFORMERR',
  SERVFAIL: 'ESERVFAIL',
  NOTFOUND: 'ENOTFOUND',
  NOTIMP: 'ENOTIMP',
  REFUSED: 'EREFUSED',
  BADQUERY: 'EBADQUERY',
  BADNAME: 'EBADNAME',
  BADFAMILY: 'EBADFAMILY',
  BADRESP: 'EBADRESP',
  CONNREFUSED: 'ECONNREFUSED',
  TIMEOUT: 'ETIMEOUT',
  EOF: 'EOF',
  FILE: 'EFILE',
  NOMEM: 'ENOMEM',
  DESTRUCTION: 'EDESTRUCTION',
  BADSTR: 'EBADSTR',
  BADFLAGS: 'EBADFLAGS',
  NONAME: 'ENONAME',
  BADHINTS: 'EBADHINTS',
  NOTINITIALIZED: 'ENOTINITIALIZED',
  LOADIPHLPAPI: 'ELOADIPHLPAPI',
  ADDRGETNETWORKPARAMS: 'EADDRGETNETWORKPARAMS',
  CANCELLED: 'ECANCELLED',
};

module.exports = {
  bindDefaultResolver,
  getDefaultResolver,
  setDefaultResolver,
  validateHints,
  validateTimeout,
  validateTries,
  emitInvalidHostnameWarning,
  getDefaultResultOrder,
  setDefaultResultOrder,
  errorCodes,
  createResolverClass,
  initializeDns,
};
