'use strict';

const {
  ArrayIsArray,
  DateNow,
  ObjectEntries,
  String,
} = primordials;

let dc;
let Network;

let requestId = 0;
const getNextRequestId = () => `node-network-event-${++requestId}`;

// Convert a Headers object (Map<string, number | string | string[]>) to a plain object (Map<string, string>)
const headerObjectToDictionary = (headers = {}) => {
  const dict = {};
  for (const { 0: key, 1: value } of ObjectEntries(headers)) {
    if (typeof value === 'string') {
      dict[key] = value;
    } else if (ArrayIsArray(value)) {
      if (key.toLowerCase() === 'cookie') dict[key] = value.join('; ');
      // ChromeDevTools frontend treats 'set-cookie' as a special case
      // https://github.com/ChromeDevTools/devtools-frontend/blob/4275917f84266ef40613db3c1784a25f902ea74e/front_end/core/sdk/NetworkRequest.ts#L1368
      else if (key.toLowerCase() === 'set-cookie') dict[key] = value.join('\n');
      else dict[key] = value.join(', ');
    } else {
      dict[key] = String(value);
    }
  }
  return dict;
};

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
      headers: headerObjectToDictionary(request.getHeaders()),
    },
  });
}

function onClientRequestError({ request, error }) {
  if (typeof request._inspectorRequestId !== 'string') {
    return;
  }
  const timestamp = DateNow() / 1000;
  Network.loadingFailed({
    requestId: request._inspectorRequestId,
    timestamp,
    type: 'Other',
    errorText: error.message,
  });
}

function onClientResponseFinish({ request, response }) {
  if (typeof request._inspectorRequestId !== 'string') {
    return;
  }
  const url = `${request.protocol}//${request.host}${request.path}`;
  const timestamp = DateNow() / 1000;
  Network.responseReceived({
    requestId: request._inspectorRequestId,
    timestamp,
    type: 'Other',
    response: {
      url,
      status: response.statusCode,
      statusText: response.statusMessage ?? '',
      headers: headerObjectToDictionary(response.headers),
    },
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
  dc.subscribe('http.client.request.error', onClientRequestError);
  dc.subscribe('http.client.response.finish', onClientResponseFinish);
}

function disable() {
  dc.unsubscribe('http.client.request.start', onClientRequestStart);
  dc.unsubscribe('http.client.request.error', onClientRequestError);
  dc.unsubscribe('http.client.response.finish', onClientResponseFinish);
}

module.exports = {
  enable,
  disable,
};
