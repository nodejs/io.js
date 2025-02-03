'use strict';
const common = require('../common');
if (!common.hasCrypto) { common.skip('missing crypto'); }

const { Readable } = require('stream');
const process = require('process');
const { randomBytes } = require('crypto');
const assert = require('assert');

// Based on: https://github.com/nodejs/node/issues/46347#issuecomment-1413886707
// edit: make it cross-platform as /dev/urandom is not available on Windows
{
  let currentMemoryUsage = process.memoryUsage().arrayBuffers;

  // We initialize a stream, but not start consuming it
  const randomNodeStream = new Readable({
    read(size) {
      randomBytes(size, (err, buffer) => {
        if (err) {
          // If an error occurs, emit an 'error' event
          this.emit('error', err);
          return;
        }

        // Push the random bytes to the stream
        this.push(buffer);
      });
    }
  });
  // after 2 seconds, it'll get converted to web stream
  let randomWebStream;

  // We check memory usage every second
  // since it's a stream, it shouldn't be higher than the chunk size
  const reportMemoryUsage = () => {
    const { arrayBuffers } = process.memoryUsage();
    currentMemoryUsage = arrayBuffers;

    assert(currentMemoryUsage <= 256 * 1024 * 1024);
  };
  setInterval(reportMemoryUsage, 1000);

  // after 1 second we use Readable.toWeb
  // memory usage should stay pretty much the same since it's still a stream
  setTimeout(() => {
    randomWebStream = Readable.toWeb(randomNodeStream);
  }, 1000);

  // after 2 seconds we start consuming the stream
  // memory usage will grow, but the old chunks should be garbage-collected pretty quickly
  setTimeout(async () => {
    // eslint-disable-next-line no-unused-vars
    for await (const _ of randomWebStream) {
      // Do nothing, just let the stream flow
    }
  }, 2000);

  setTimeout(() => {
    // Test considered passed if we don't crash
    process.exit(0);
  }, 5000);
}
