'use strict';

const {
  ObjectSetPrototypeOf,
  Symbol,
} = primordials;

const {
  SocketAddress: _SocketAddress,
  AF_INET,
  AF_INET6,
} = internalBinding('block_list');

const {
  validateObject,
  validateString,
  validatePort,
  validateUint32,
} = require('internal/validators');

const {
  codes: {
    ERR_INVALID_ARG_VALUE,
  },
} = require('internal/errors');

const {
  customInspectSymbol: kInspect,
  kEmptyObject,
} = require('internal/util');

const { inspect } = require('internal/util/inspect');

const {
  markTransferMode,
  kClone,
  kDeserialize,
} = require('internal/worker/js_transferable');

const { URL } = require('internal/url');

const kHandle = Symbol('kHandle');
const kDetail = Symbol('kDetail');

class SocketAddress {
  static isSocketAddress(value) {
    return value?.[kHandle] !== undefined;
  }

  constructor(options = kEmptyObject) {
    markTransferMode(this, true, false);

    validateObject(options, 'options');
    let { family = 'ipv4' } = options;
    const {
      address = (family === 'ipv4' ? '127.0.0.1' : '::'),
      port = 0,
      flowlabel = 0,
    } = options;

    let type;
    if (typeof family?.toLowerCase === 'function')
      family = family.toLowerCase();
    switch (family) {
      case 'ipv4':
        type = AF_INET;
        break;
      case 'ipv6':
        type = AF_INET6;
        break;
      default:
        throw new ERR_INVALID_ARG_VALUE('options.family', options.family);
    }

    validateString(address, 'options.address');
    validatePort(port, 'options.port');
    validateUint32(flowlabel, 'options.flowlabel', false);

    this[kHandle] = new _SocketAddress(address, port | 0, type, flowlabel | 0);
    this[kDetail] = this[kHandle].detail({
      address: undefined,
      port: undefined,
      family: undefined,
      flowlabel: undefined,
    });
  }

  get address() {
    return this[kDetail].address;
  }

  get port() {
    return this[kDetail].port;
  }

  get family() {
    return this[kDetail].family === AF_INET ? 'ipv4' : 'ipv6';
  }

  get flowlabel() {
    // The flow label can be changed internally.
    return this[kHandle].flowlabel();
  }

  [kInspect](depth, options) {
    if (depth < 0)
      return this;

    const opts = {
      ...options,
      depth: options.depth == null ? null : options.depth - 1,
    };

    return `SocketAddress ${inspect(this.toJSON(), opts)}`;
  }

  [kClone]() {
    const handle = this[kHandle];
    return {
      data: { handle },
      deserializeInfo: 'internal/socketaddress:InternalSocketAddress',
    };
  }

  [kDeserialize]({ handle }) {
    this[kHandle] = handle;
    this[kDetail] = handle.detail({
      address: undefined,
      port: undefined,
      family: undefined,
      flowlabel: undefined,
    });
  }

  toJSON() {
    return {
      address: this.address,
      port: this.port,
      family: this.family,
      flowlabel: this.flowlabel,
    };
  }

  /**
   * Parse an "${ip}:${port}" formatted string into a SocketAddress.
   * Returns undefined if the input cannot be successfully parsed.
   * @param {string} input
   * @returns {SocketAddress|undefined}
   */
  static parse(input) {
    validateString(input, 'input');
    // While URL.parse is not expected to throw, there are several
    // other pieces here that do... the destucturing, the SocketAddress
    // constructor, etc. So we wrap this in a try/catch to be safe.
    try {
      const {
        hostname: address,
        port,
      } = URL.parse(`http://${input}`);
      if (address.startsWith('[') && address.endsWith(']')) {
        return new SocketAddress({
          address: address.slice(1, -1),
          port: port | 0,
          family: 'ipv6',
        });
      }
      return new SocketAddress({ address, port: port | 0 });
    } catch {
      // Ignore errors here. Return undefined if the input cannot
      // be successfully parsed or is not a proper socket address.
    }
  }
}

class InternalSocketAddress {
  constructor(handle) {
    markTransferMode(this, true, false);

    this[kHandle] = handle;
    this[kDetail] = this[kHandle]?.detail({
      address: undefined,
      port: undefined,
      family: undefined,
      flowlabel: undefined,
    });
  }
}

InternalSocketAddress.prototype.constructor =
  SocketAddress.prototype.constructor;
ObjectSetPrototypeOf(InternalSocketAddress.prototype, SocketAddress.prototype);

module.exports = {
  SocketAddress,
  InternalSocketAddress,
  kHandle,
};
