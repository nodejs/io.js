'use strict';

function enable() {
  require('internal/inspector/network_http').enable();
  require('internal/inspector/network_undici').enable();
}

function disable() {
  require('internal/inspector/network_http').disable();
  require('internal/inspector/network_undici').disable();
}

module.exports = {
  enable,
  disable,
};
