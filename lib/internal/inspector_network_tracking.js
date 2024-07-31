'use strict';

const {
  DateNow,
} = primordials;

let dc;
let Network;

let requestId = 0;
const getNextRequestId = () => `node-network-event-${++requestId}`;

function onClientRequestStart({ request }) {
  const url = `${request.protocol}//${request.host}${request.path}`;
  const wallTime = DateNow();
  const timestamp = wallTime / 1000;
  request._inspectorRequestId = getNextRequestId();
  Network.requestWillBeSent({
    requestId: request._inspectorRequestId,
    timestamp,
    wallTime,
    request: {
      url,
      method: request.method,
    },
  });
}

function onClientResponseFinish({ request }) {
  if (typeof request._inspectorRequestId !== 'string') {
    return;
  }
  const timestamp = DateNow() / 1000;
  Network.responseReceived({
    requestId: request._inspectorRequestId,
    timestamp,
  });
  Network.loadingFinished({
    requestId: request._inspectorRequestId,
    timestamp,
  });
}

function enable() {
  if (!dc) {
    dc = require('diagnostics_channel');
  }
  if (!Network) {
    Network = require('inspector').Network;
  }
  dc.subscribe('http.client.request.start', onClientRequestStart);
  dc.subscribe('http.client.response.finish', onClientResponseFinish);
}

function disable() {
  dc.unsubscribe('http.client.request.start', onClientRequestStart);
  dc.unsubscribe('http.client.response.finish', onClientResponseFinish);
}

module.exports = {
  enable,
  disable,
};
