'use strict';

const {
  ArrayIsArray,
  DateNow,
  ObjectEntries,
  String,
  Symbol,
} = primordials;

const {
  kInspectorRequestId,
  kResourceType,
  getMonotonicTime,
  getNextRequestId,
} = require('internal/inspector/network');
const dc = require('diagnostics_channel');
const { Network } = require('inspector');

const kRequestUrl = Symbol('kRequestUrl');

// Convert a Headers object (Map<string, number | string | string[]>) to a plain object (Map<string, string>)
const convertHeaderObject = (headers = {}) => {
  // The 'host' header that contains the host and port of the URL.
  let host;
  const dict = {};
  for (const { 0: key, 1: value } of ObjectEntries(headers)) {
    if (key.toLowerCase() === 'host') {
      host = value;
    }
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
  return [host, dict];
};

/**
 * When a client request starts, emit Network.requestWillBeSent event.
 * https://chromedevtools.github.io/devtools-protocol/1-3/Network/#event-requestWillBeSent
 * @param {{ request: import('http').ClientRequest }} event
 */
function onClientRequestStart({ request }) {
  request[kInspectorRequestId] = getNextRequestId();

  const { 0: host, 1: headers } = convertHeaderObject(request.getHeaders());
  const url = `${request.protocol}//${host}${request.path}`;
  request[kRequestUrl] = url;

  Network.requestWillBeSent({
    requestId: request[kInspectorRequestId],
    timestamp: getMonotonicTime(),
    wallTime: DateNow(),
    request: {
      url,
      method: request.method,
      headers,
    },
  });
}

/**
 * When a client request errors, emit Network.loadingFailed event.
 * https://chromedevtools.github.io/devtools-protocol/1-3/Network/#event-loadingFailed
 * @param {{ request: import('http').ClientRequest, error: any }} event
 */
function onClientRequestError({ request, error }) {
  if (typeof request[kInspectorRequestId] !== 'string') {
    return;
  }
  Network.loadingFailed({
    requestId: request[kInspectorRequestId],
    timestamp: getMonotonicTime(),
    type: kResourceType.Other,
    errorText: error.message,
  });
}

/**
 * When response headers are received, emit Network.responseReceived event.
 * https://chromedevtools.github.io/devtools-protocol/1-3/Network/#event-responseReceived
 * @param {{ request: import('http').ClientRequest, error: any }} event
 */
function onClientResponseFinish({ request, response }) {
  if (typeof request[kInspectorRequestId] !== 'string') {
    return;
  }
  Network.responseReceived({
    requestId: request[kInspectorRequestId],
    timestamp: getMonotonicTime(),
    type: kResourceType.Other,
    response: {
      url: request[kRequestUrl],
      status: response.statusCode,
      statusText: response.statusMessage ?? '',
      headers: convertHeaderObject(response.headers)[1],
    },
  });

  // Wait until the response body is consumed by user code.
  response.once('end', () => {
    Network.loadingFinished({
      requestId: request[kInspectorRequestId],
      timestamp: getMonotonicTime(),
    });
  });
}

function enable() {
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
