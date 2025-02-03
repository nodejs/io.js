'use strict';

const {
  Boolean,
  ObjectSetPrototypeOf,
  Symbol,
} = primordials;

const {
  BlockList: BlockListHandle,
} = internalBinding('block_list');

const {
  customInspectSymbol: kInspect,
} = require('internal/util');

const {
  SocketAddress,
  kHandle: kSocketAddressHandle,
} = require('internal/socketaddress');

const {
  markTransferMode,
  kClone,
  kDeserialize,
} = require('internal/worker/js_transferable');

const { inspect } = require('internal/util/inspect');

const kHandle = Symbol('kHandle');
const { owner_symbol } = internalBinding('symbols');

const {
  ERR_INVALID_ARG_VALUE,
} = require('internal/errors').codes;

const { validateInt32, validateString } = require('internal/validators');

class BlockList {
  constructor() {
    markTransferMode(this, true, false);
    this[kHandle] = new BlockListHandle();
    this[kHandle][owner_symbol] = this;
  }

  /**
   * Returns true if the value is a BlockList
   * @param {any} value
   * @returns {boolean}
   */
  static isBlockList(value) {
    return value?.[kHandle] !== undefined;
  }

  [kInspect](depth, options) {
    if (depth < 0)
      return this;

    const opts = {
      ...options,
      depth: options.depth == null ? null : options.depth - 1,
    };

    return `BlockList ${inspect({
      rules: this.rules,
    }, opts)}`;
  }

  addAddress(address, family = 'ipv4') {
    if (!SocketAddress.isSocketAddress(address)) {
      validateString(address, 'address');
      validateString(family, 'family');
      address = new SocketAddress({
        address,
        family,
      });
    }
    this[kHandle].addAddress(address[kSocketAddressHandle]);
  }

  addRange(start, end, family = 'ipv4') {
    if (!SocketAddress.isSocketAddress(start)) {
      validateString(start, 'start');
      validateString(family, 'family');
      start = new SocketAddress({
        address: start,
        family,
      });
    }
    if (!SocketAddress.isSocketAddress(end)) {
      validateString(end, 'end');
      validateString(family, 'family');
      end = new SocketAddress({
        address: end,
        family,
      });
    }
    const ret = this[kHandle].addRange(
      start[kSocketAddressHandle],
      end[kSocketAddressHandle]);
    if (ret === false)
      throw new ERR_INVALID_ARG_VALUE('start', start, 'must come before end');
  }

  addSubnet(network, prefix, family = 'ipv4') {
    if (!SocketAddress.isSocketAddress(network)) {
      validateString(network, 'network');
      validateString(family, 'family');
      network = new SocketAddress({
        address: network,
        family,
      });
    }
    switch (network.family) {
      case 'ipv4':
        validateInt32(prefix, 'prefix', 0, 32);
        break;
      case 'ipv6':
        validateInt32(prefix, 'prefix', 0, 128);
        break;
    }
    this[kHandle].addSubnet(network[kSocketAddressHandle], prefix);
  }

  check(address, family = 'ipv4') {
    if (!SocketAddress.isSocketAddress(address)) {
      validateString(address, 'address');
      validateString(family, 'family');
      try {
        address = new SocketAddress({
          address,
          family,
        });
      } catch {
        // Ignore the error. If it's not a valid address, return false.
        return false;
      }
    }
    return Boolean(this[kHandle].check(address[kSocketAddressHandle]));
  }

  get rules() {
    return this[kHandle].getRules();
  }

  [kClone]() {
    const handle = this[kHandle];
    return {
      data: { handle },
      deserializeInfo: 'internal/blocklist:InternalBlockList',
    };
  }

  [kDeserialize]({ handle }) {
    this[kHandle] = handle;
    this[kHandle][owner_symbol] = this;
  }
}

class InternalBlockList {
  constructor(handle) {
    markTransferMode(this, true, false);
    this[kHandle] = handle;
    if (handle !== undefined)
      handle[owner_symbol] = this;
  }
}

InternalBlockList.prototype.constructor = BlockList.prototype.constructor;
ObjectSetPrototypeOf(InternalBlockList.prototype, BlockList.prototype);

module.exports = {
  BlockList,
  InternalBlockList,
};
