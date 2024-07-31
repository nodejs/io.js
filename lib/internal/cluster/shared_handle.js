'use strict';

const {
  SafeMap,
} = primordials;

const assert = require('internal/assert');
const dgram = require('internal/dgram');
const net = require('net');

module.exports = SharedHandle;

function SharedHandle(key, address, { port, addressType, fd, flags }) {
  this.key = key;
  this.workers = new SafeMap();
  this.handle = null;
  this.errno = 0;

  let rval;
  if (addressType === 'udp4' || addressType === 'udp6')
    rval = dgram._createSocketHandle(address, port, addressType, fd, flags);
  else
    rval = net._createServerHandle(address, port, addressType, fd, flags);

  if (typeof rval === 'number')
    this.errno = rval;
  else
    this.handle = rval;
}

SharedHandle.prototype.add = function(worker, send) {
  assert(!this.workers.has(worker.id));
  this.workers.set(worker.id, worker);
  send(this.errno, null, this.handle);
};

SharedHandle.prototype.remove = function(worker) {
  if (!this.workers.has(worker.id))
    return false;

  this.workers.delete(worker.id);

  if (this.workers.size !== 0)
    return false;

  this.handle.close();
  this.handle = null;
  return true;
};
