'use strict';

const {
  ArrayFrom,
  ArrayIsArray,
  MathMin,
  Number,
  ObjectAssign,
  ObjectDefineProperty,
  ObjectEntries,
  ObjectHasOwn,
  ObjectKeys,
  Promise,
  Proxy,
  ReflectApply,
  ReflectGet,
  ReflectGetPrototypeOf,
  ReflectSet,
  SafeMap,
  SafeSet,
  Symbol,
  SymbolAsyncDispose,
  SymbolDispose,
  Uint32Array,
  Uint8Array,
} = primordials;

const {
  assertCrypto,
  customInspectSymbol: kInspect,
  kEmptyObject,
  promisify,
} = require('internal/util');

assertCrypto();

const assert = require('assert');
const EventEmitter = require('events');
const { addAbortListener } = require('internal/events/abort_listener');
const fs = require('fs');
const http = require('http');
const { readUInt16BE, readUInt32BE } = require('internal/buffer');
const { URL, getURLOrigin } = require('internal/url');
const net = require('net');
const { Duplex } = require('stream');
const tls = require('tls');
const { setImmediate, setTimeout, clearTimeout } = require('timers');

const {
  kIncomingMessage,
  _checkIsHttpToken: checkIsHttpToken,
} = require('_http_common');
const { kServerResponse, Server: HttpServer, httpServerPreClose, setupConnectionsTracking } = require('_http_server');
const JSStreamSocket = require('internal/js_stream_socket');

const {
  defaultTriggerAsyncIdScope,
  symbols: {
    async_id_symbol,
    owner_symbol,
  },
} = require('internal/async_hooks');
const { AsyncResource } = require('async_hooks');

const {
  AbortError,
  aggregateTwoErrors,
  codes: {
    ERR_HTTP2_ALTSVC_INVALID_ORIGIN,
    ERR_HTTP2_ALTSVC_LENGTH,
    ERR_HTTP2_CONNECT_AUTHORITY,
    ERR_HTTP2_CONNECT_PATH,
    ERR_HTTP2_CONNECT_SCHEME,
    ERR_HTTP2_GOAWAY_SESSION,
    ERR_HTTP2_HEADERS_AFTER_RESPOND,
    ERR_HTTP2_HEADERS_SENT,
    ERR_HTTP2_INVALID_INFO_STATUS,
    ERR_HTTP2_INVALID_ORIGIN,
    ERR_HTTP2_INVALID_PACKED_SETTINGS_LENGTH,
    ERR_HTTP2_INVALID_SESSION,
    ERR_HTTP2_INVALID_SETTING_VALUE,
    ERR_HTTP2_INVALID_STREAM,
    ERR_HTTP2_MAX_PENDING_SETTINGS_ACK,
    ERR_HTTP2_NESTED_PUSH,
    ERR_HTTP2_NO_MEM,
    ERR_HTTP2_NO_SOCKET_MANIPULATION,
    ERR_HTTP2_ORIGIN_LENGTH,
    ERR_HTTP2_OUT_OF_STREAMS,
    ERR_HTTP2_PAYLOAD_FORBIDDEN,
    ERR_HTTP2_PING_CANCEL,
    ERR_HTTP2_PING_LENGTH,
    ERR_HTTP2_PUSH_DISABLED,
    ERR_HTTP2_SEND_FILE,
    ERR_HTTP2_SEND_FILE_NOSEEK,
    ERR_HTTP2_SESSION_ERROR,
    ERR_HTTP2_SETTINGS_CANCEL,
    ERR_HTTP2_SOCKET_BOUND,
    ERR_HTTP2_SOCKET_UNBOUND,
    ERR_HTTP2_STATUS_101,
    ERR_HTTP2_STATUS_INVALID,
    ERR_HTTP2_STREAM_CANCEL,
    ERR_HTTP2_STREAM_ERROR,
    ERR_HTTP2_STREAM_SELF_DEPENDENCY,
    ERR_HTTP2_TOO_MANY_CUSTOM_SETTINGS,
    ERR_HTTP2_TRAILERS_ALREADY_SENT,
    ERR_HTTP2_TRAILERS_NOT_READY,
    ERR_HTTP2_UNSUPPORTED_PROTOCOL,
    ERR_INVALID_ARG_TYPE,
    ERR_INVALID_ARG_VALUE,
    ERR_INVALID_CHAR,
    ERR_INVALID_HTTP_TOKEN,
    ERR_OUT_OF_RANGE,
    ERR_SOCKET_CLOSED,
  },
  hideStackFrames,
} = require('internal/errors');
const {
  isUint32,
  validateAbortSignal,
  validateBoolean,
  validateBuffer,
  validateFunction,
  validateInt32,
  validateInteger,
  validateNumber,
  validateString,
  validateUint32,
} = require('internal/validators');
const fsPromisesInternal = require('internal/fs/promises');
const { utcDate } = require('internal/http');
const {
  Http2ServerRequest,
  Http2ServerResponse,
  onServerStream,
} = require('internal/http2/compat');

const {
  assertIsObject,
  assertIsArray,
  assertValidPseudoHeader,
  assertValidPseudoHeaderResponse,
  assertValidPseudoHeaderTrailer,
  assertWithinRange,
  getAuthority,
  getDefaultSettings,
  getSessionState,
  getSettings,
  getStreamState,
  isPayloadMeaningless,
  kSensitiveHeaders,
  kSocket,
  kRequest,
  kProxySocket,
  mapToHeaders,
  MAX_ADDITIONAL_SETTINGS,
  NghttpError,
  remoteCustomSettingsToBuffer,
  sessionName,
  toHeaderObject,
  updateOptionsBuffer,
  updateSettingsBuffer,
} = require('internal/http2/util');
const {
  writeGeneric,
  writevGeneric,
  onStreamRead,
  kAfterAsyncWrite,
  kMaybeDestroy,
  kUpdateTimer,
  kHandle,
  kSession,
  kBoundSession,
  setStreamTimeout,
} = require('internal/stream_base_commons');
const { kTimeout } = require('internal/timers');
const { isArrayBufferView } = require('internal/util/types');
const { format } = require('internal/util/inspect');

const { FileHandle } = internalBinding('fs');
const binding = internalBinding('http2');
const {
  ShutdownWrap,
  kReadBytesOrError,
  streamBaseState,
} = internalBinding('stream_wrap');
const { UV_EOF } = internalBinding('uv');

const { StreamPipe } = internalBinding('stream_pipe');
const { _connectionListener: httpConnectionListener } = http;
let debug = require('internal/util/debuglog').debuglog('http2', (fn) => {
  debug = fn;
});
const debugEnabled = debug.enabled;

function debugStream(id, sessionType, message, ...args) {
  if (!debugEnabled) {
    return;
  }
  debug('Http2Stream %s [Http2Session %s]: ' + message,
        id, sessionName(sessionType), ...args);
}

function debugStreamObj(stream, message, ...args) {
  const session = stream[kSession];
  const type = session ? session[kType] : undefined;
  debugStream(stream[kID], type, message, ...args);
}

function debugSession(sessionType, message, ...args) {
  debug('Http2Session %s: ' + message, sessionName(sessionType),
        ...args);
}

function debugSessionObj(session, message, ...args) {
  debugSession(session[kType], message, ...args);
}

const kMaxFrameSize = (2 ** 24) - 1;
const kMaxInt = (2 ** 32) - 1;
const kMaxStreams = (2 ** 32) - 1;
const kMaxALTSVC = (2 ** 14) - 2;

// eslint-disable-next-line no-control-regex
const kQuotedString = /^[\x09\x20-\x5b\x5d-\x7e\x80-\xff]*$/;

const { constants, nameForErrorCode } = binding;

const NETServer = net.Server;
const TLSServer = tls.Server;

const kAlpnProtocol = Symbol('alpnProtocol');
const kAuthority = Symbol('authority');
const kEncrypted = Symbol('encrypted');
const kID = Symbol('id');
const kInit = Symbol('init');
const kInfoHeaders = Symbol('sent-info-headers');
const kLocalSettings = Symbol('local-settings');
const kNativeFields = Symbol('kNativeFields');
const kOptions = Symbol('options');
const kOwner = owner_symbol;
const kOrigin = Symbol('origin');
const kPendingRequestCalls = Symbol('kPendingRequestCalls');
const kProceed = Symbol('proceed');
const kProtocol = Symbol('protocol');
const kRemoteSettings = Symbol('remote-settings');
const kRequestAsyncResource = Symbol('requestAsyncResource');
const kSelectPadding = Symbol('select-padding');
const kSentHeaders = Symbol('sent-headers');
const kSentTrailers = Symbol('sent-trailers');
const kServer = Symbol('server');
const kState = Symbol('state');
const kType = Symbol('type');
const kWriteGeneric = Symbol('write-generic');

const {
  kBitfield,
  kSessionPriorityListenerCount,
  kSessionFrameErrorListenerCount,
  kSessionMaxInvalidFrames,
  kSessionMaxRejectedStreams,
  kSessionUint8FieldCount,
  kSessionHasRemoteSettingsListeners,
  kSessionRemoteSettingsIsUpToDate,
  kSessionHasPingListeners,
  kSessionHasAltsvcListeners,
} = binding;

const {
  NGHTTP2_CANCEL,
  NGHTTP2_REFUSED_STREAM,
  NGHTTP2_DEFAULT_WEIGHT,
  NGHTTP2_FLAG_END_STREAM,
  NGHTTP2_HCAT_PUSH_RESPONSE,
  NGHTTP2_HCAT_RESPONSE,
  NGHTTP2_INTERNAL_ERROR,
  NGHTTP2_NO_ERROR,
  NGHTTP2_SESSION_CLIENT,
  NGHTTP2_SESSION_SERVER,
  NGHTTP2_ERR_STREAM_ID_NOT_AVAILABLE,
  NGHTTP2_ERR_INVALID_ARGUMENT,
  NGHTTP2_ERR_STREAM_CLOSED,
  NGHTTP2_ERR_NOMEM,

  HTTP2_HEADER_AUTHORITY,
  HTTP2_HEADER_DATE,
  HTTP2_HEADER_METHOD,
  HTTP2_HEADER_PATH,
  HTTP2_HEADER_PROTOCOL,
  HTTP2_HEADER_SCHEME,
  HTTP2_HEADER_STATUS,
  HTTP2_HEADER_CONTENT_LENGTH,

  NGHTTP2_SETTINGS_HEADER_TABLE_SIZE,
  NGHTTP2_SETTINGS_ENABLE_PUSH,
  NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS,
  NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE,
  NGHTTP2_SETTINGS_MAX_FRAME_SIZE,
  NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE,
  NGHTTP2_SETTINGS_ENABLE_CONNECT_PROTOCOL,

  HTTP2_METHOD_GET,
  HTTP2_METHOD_HEAD,
  HTTP2_METHOD_CONNECT,

  HTTP_STATUS_CONTINUE,
  HTTP_STATUS_RESET_CONTENT,
  HTTP_STATUS_OK,
  HTTP_STATUS_NO_CONTENT,
  HTTP_STATUS_NOT_MODIFIED,
  HTTP_STATUS_SWITCHING_PROTOCOLS,
  HTTP_STATUS_MISDIRECTED_REQUEST,

  STREAM_OPTION_EMPTY_PAYLOAD,
  STREAM_OPTION_GET_TRAILERS,
} = constants;

const STREAM_FLAGS_PENDING = 0x0;
const STREAM_FLAGS_READY = 0x1;
const STREAM_FLAGS_CLOSED = 0x2;
const STREAM_FLAGS_HEADERS_SENT = 0x4;
const STREAM_FLAGS_HEAD_REQUEST = 0x8;
const STREAM_FLAGS_ABORTED = 0x10;
const STREAM_FLAGS_HAS_TRAILERS = 0x20;

const SESSION_FLAGS_PENDING = 0x0;
const SESSION_FLAGS_READY = 0x1;
const SESSION_FLAGS_CLOSED = 0x2;
const SESSION_FLAGS_DESTROYED = 0x4;

// Top level to avoid creating a closure
function emit(self, ...args) {
  ReflectApply(self.emit, self, args);
}

// Called when a new block of headers has been received for a given
// stream. The stream may or may not be new. If the stream is new,
// create the associated Http2Stream instance and emit the 'stream'
// event. If the stream is not new, emit the 'headers' event to pass
// the block of headers on.
function onSessionHeaders(handle, id, cat, flags, headers, sensitiveHeaders) {
  const session = this[kOwner];
  if (session.destroyed)
    return;

  const type = session[kType];
  session[kUpdateTimer]();
  debugStream(id, type, 'headers received');
  const streams = session[kState].streams;

  const endOfStream = !!(flags & NGHTTP2_FLAG_END_STREAM);
  let stream = streams.get(id);

  // Convert the array of header name value pairs into an object
  const obj = toHeaderObject(headers, sensitiveHeaders);

  if (stream === undefined) {
    if (session.closed) {
      // We are not accepting any new streams at this point. This callback
      // should not be invoked at this point in time, but just in case it is,
      // refuse the stream using an RST_STREAM and destroy the handle.
      handle.rstStream(NGHTTP2_REFUSED_STREAM);
      handle.destroy();
      return;
    }
    // session[kType] can be only one of two possible values
    if (type === NGHTTP2_SESSION_SERVER) {
      // eslint-disable-next-line no-use-before-define
      stream = new ServerHttp2Stream(session, handle, id, {}, obj);
      if (endOfStream) {
        stream.push(null);
      }
      if (obj[HTTP2_HEADER_METHOD] === HTTP2_METHOD_HEAD) {
        // For head requests, there must not be a body...
        // end the writable side immediately.
        stream.end();
        stream[kState].flags |= STREAM_FLAGS_HEAD_REQUEST;
      }
    } else {
      // eslint-disable-next-line no-use-before-define
      stream = new ClientHttp2Stream(session, handle, id, {});
      if (endOfStream) {
        stream.push(null);
      }
      stream.end();
    }
    if (endOfStream)
      stream[kState].endAfterHeaders = true;
    process.nextTick(emit, session, 'stream', stream, obj, flags, headers);
  } else {
    let event;
    const status = obj[HTTP2_HEADER_STATUS];
    if (cat === NGHTTP2_HCAT_RESPONSE) {
      if (!endOfStream &&
          status !== undefined &&
          status >= 100 &&
          status < 200) {
        event = 'headers';
      } else {
        event = 'response';
      }
    } else if (cat === NGHTTP2_HCAT_PUSH_RESPONSE) {
      event = 'push';
    } else if (status !== undefined && status >= 200) {
      event = 'response';
    } else {
      event = endOfStream ? 'trailers' : 'headers';
    }
    const session = stream.session;
    if (status === HTTP_STATUS_MISDIRECTED_REQUEST) {
      const originSet = session[kState].originSet = initOriginSet(session);
      originSet.delete(stream[kOrigin]);
    }
    debugStream(id, type, "emitting stream '%s' event", event);
    const reqAsync = stream[kRequestAsyncResource];
    if (reqAsync)
      reqAsync.runInAsyncScope(process.nextTick, null, emit, stream, event, obj, flags, headers);
    else
      process.nextTick(emit, stream, event, obj, flags, headers);
  }
  if (endOfStream) {
    stream.push(null);
  }
}

function tryClose(fd) {
  // Try to close the file descriptor. If closing fails, assert because
  // an error really should not happen at this point.
  fs.close(fd, assert.ifError);
}

// Called when the Http2Stream has finished sending data and is ready for
// trailers to be sent. This will only be called if the { hasOptions: true }
// option is set.
function onStreamTrailers() {
  const stream = this[kOwner];
  stream[kState].trailersReady = true;
  if (stream.destroyed || stream.closed)
    return;
  if (!stream.emit('wantTrailers')) {
    // There are no listeners, send empty trailing HEADERS frame and close.
    stream.sendTrailers({});
  }
}

// Submit an RST-STREAM frame to be sent to the remote peer.
// This will cause the Http2Stream to be closed.
function submitRstStream(code) {
  if (this[kHandle] !== undefined) {
    this[kHandle].rstStream(code);
  }
}

// Keep track of the number/presence of JS event listeners. Knowing that there
// are no listeners allows the C++ code to skip calling into JS for an event.
function sessionListenerAdded(name) {
  switch (name) {
    case 'ping':
      this[kNativeFields][kBitfield] |= 1 << kSessionHasPingListeners;
      break;
    case 'altsvc':
      this[kNativeFields][kBitfield] |= 1 << kSessionHasAltsvcListeners;
      break;
    case 'remoteSettings':
      this[kNativeFields][kBitfield] |= 1 << kSessionHasRemoteSettingsListeners;
      break;
    case 'priority':
      this[kNativeFields][kSessionPriorityListenerCount]++;
      break;
    case 'frameError':
      this[kNativeFields][kSessionFrameErrorListenerCount]++;
      break;
  }
}

function sessionListenerRemoved(name) {
  switch (name) {
    case 'ping':
      if (this.listenerCount(name) > 0) return;
      this[kNativeFields][kBitfield] &= ~(1 << kSessionHasPingListeners);
      break;
    case 'altsvc':
      if (this.listenerCount(name) > 0) return;
      this[kNativeFields][kBitfield] &= ~(1 << kSessionHasAltsvcListeners);
      break;
    case 'remoteSettings':
      if (this.listenerCount(name) > 0) return;
      this[kNativeFields][kBitfield] &=
          ~(1 << kSessionHasRemoteSettingsListeners);
      break;
    case 'priority':
      this[kNativeFields][kSessionPriorityListenerCount]--;
      break;
    case 'frameError':
      this[kNativeFields][kSessionFrameErrorListenerCount]--;
      break;
  }
}

// Also keep track of listeners for the Http2Stream instances, as some events
// are emitted on those objects.
function streamListenerAdded(name) {
  const session = this[kSession];
  if (!session) return;
  switch (name) {
    case 'priority':
      session[kNativeFields][kSessionPriorityListenerCount]++;
      break;
    case 'frameError':
      session[kNativeFields][kSessionFrameErrorListenerCount]++;
      break;
  }
}

function streamListenerRemoved(name) {
  const session = this[kSession];
  if (!session) return;
  switch (name) {
    case 'priority':
      session[kNativeFields][kSessionPriorityListenerCount]--;
      break;
    case 'frameError':
      session[kNativeFields][kSessionFrameErrorListenerCount]--;
      break;
  }
}

function onPing(payload) {
  const session = this[kOwner];
  if (session.destroyed)
    return;
  session[kUpdateTimer]();
  debugSessionObj(session, 'new ping received');
  session.emit('ping', payload);
}

// Called when the stream is closed either by sending or receiving an
// RST_STREAM frame, or through a natural end-of-stream.
// If the writable and readable sides of the stream are still open at this
// point, close them. If there is an open fd for file send, close that also.
// At this point the underlying node::http2:Http2Stream handle is no
// longer usable so destroy it also.
function onStreamClose(code) {
  const stream = this[kOwner];
  if (!stream || stream.destroyed)
    return false;

  debugStreamObj(
    stream, 'closed with code %d, closed %s, readable %s',
    code, stream.closed, stream.readable,
  );

  if (!stream.closed)
    closeStream(stream, code, kNoRstStream);

  stream[kState].fd = -1;
  // Defer destroy we actually emit end.
  if (!stream.readable || code !== NGHTTP2_NO_ERROR) {
    // If errored or ended, we can destroy immediately.
    stream.destroy();
  } else {
    // Wait for end to destroy.
    stream.on('end', stream[kMaybeDestroy]);
    // Push a null so the stream can end whenever the client consumes
    // it completely.
    stream.push(null);

    // If the user hasn't tried to consume the stream (and this is a server
    // session) then just dump the incoming data so that the stream can
    // be destroyed.
    if (stream[kSession][kType] === NGHTTP2_SESSION_SERVER &&
        !stream[kState].didRead &&
        stream.readableFlowing === null)
      stream.resume();
    else
      stream.read(0);
  }
  return true;
}

// Called when the remote peer settings have been updated.
// Resets the cached settings.
function onSettings() {
  const session = this[kOwner];
  if (session.destroyed)
    return;
  session[kUpdateTimer]();
  debugSessionObj(session, 'new settings received');
  session[kRemoteSettings] = undefined;
  session.emit('remoteSettings', session.remoteSettings);
}

// If the stream exists, an attempt will be made to emit an event
// on the stream object itself. Otherwise, forward it on to the
// session (which may, in turn, forward it on to the server)
function onPriority(id, parent, weight, exclusive) {
  const session = this[kOwner];
  if (session.destroyed)
    return;
  debugStream(id, session[kType],
              'priority [parent: %d, weight: %d, exclusive: %s]',
              parent, weight, exclusive);
  const emitter = session[kState].streams.get(id) || session;
  if (!emitter.destroyed) {
    emitter[kUpdateTimer]();
    emitter.emit('priority', id, parent, weight, exclusive);
  }
}

// Called by the native layer when an error has occurred sending a
// frame. This should be exceedingly rare.
function onFrameError(id, type, code) {
  const session = this[kOwner];
  if (session.destroyed)
    return;
  debugSessionObj(session, 'error sending frame type %d on stream %d, code: %d',
                  type, id, code);

  const stream = session[kState].streams.get(id);
  const emitter = stream || session;
  emitter[kUpdateTimer]();
  emitter.emit('frameError', type, code, id);

  // When a frameError happens is not uncommon that a pending GOAWAY
  // package from nghttp2 is on flight with a correct error code.
  // We schedule it using setImmediate to give some time for that
  // package to arrive.
  setImmediate(() => {
    stream?.close(code);
    session.close();
  });
}

function onAltSvc(stream, origin, alt) {
  const session = this[kOwner];
  if (session.destroyed)
    return;
  debugSessionObj(session, 'altsvc received: stream: %d, origin: %s, alt: %s',
                  stream, origin, alt);
  session[kUpdateTimer]();
  session.emit('altsvc', alt, origin, stream);
}

function initOriginSet(session) {
  let originSet = session[kState].originSet;
  if (originSet === undefined) {
    const socket = session[kSocket];
    session[kState].originSet = originSet = new SafeSet();
    let hostName = socket.servername;
    if (hostName === null || hostName === false) {
      if (socket.remoteFamily === 'IPv6') {
        hostName = `[${socket.remoteAddress}]`;
      } else {
        hostName = socket.remoteAddress;
      }
    }
    let originString = `https://${hostName}`;
    if (socket.remotePort != null)
      originString += `:${socket.remotePort}`;
    // We have to ensure that it is a properly serialized
    // ASCII origin string. The socket.servername might not
    // be properly ASCII encoded.
    originSet.add(getURLOrigin(originString));
  }
  return originSet;
}

function onOrigin(origins) {
  const session = this[kOwner];
  if (session.destroyed)
    return;
  debugSessionObj(session, 'origin received: %j', origins);
  session[kUpdateTimer]();
  if (!session.encrypted || session.destroyed)
    return undefined;
  const originSet = initOriginSet(session);
  for (let n = 0; n < origins.length; n++)
    originSet.add(origins[n]);
  session.emit('origin', origins);
}

// Receiving a GOAWAY frame from the connected peer is a signal that no
// new streams should be created. If the code === NGHTTP2_NO_ERROR, we
// are going to send our close, but allow existing frames to close
// normally. If code !== NGHTTP2_NO_ERROR, we are going to send our own
// close using the same code then destroy the session with an error.
// The goaway event will be emitted on next tick.
function onGoawayData(code, lastStreamID, buf) {
  const session = this[kOwner];
  if (session.destroyed)
    return;
  debugSessionObj(session, 'goaway %d received [last stream id: %d]',
                  code, lastStreamID);

  const state = session[kState];
  state.goawayCode = code;
  state.goawayLastStreamID = lastStreamID;

  session.emit('goaway', code, lastStreamID, buf);
  if (code === NGHTTP2_NO_ERROR) {
    // If this is a no error goaway, begin shutting down.
    // No new streams permitted, but existing streams may
    // close naturally on their own.
    session.close();
  } else {
    // However, if the code is not NGHTTP_NO_ERROR, destroy the
    // session immediately. We destroy with an error but send a
    // goaway using NGHTTP2_NO_ERROR because there was no error
    // condition on this side of the session that caused the
    // shutdown.
    session.destroy(new ERR_HTTP2_SESSION_ERROR(code), NGHTTP2_NO_ERROR);
  }
}

// When a ClientHttp2Session is first created, the socket may not yet be
// connected. If request() is called during this time, the actual request
// will be deferred until the socket is ready to go.
function requestOnConnect(headers, options) {
  const session = this[kSession];

  // At this point, the stream should have already been destroyed during
  // the session.destroy() method. Do nothing else.
  if (session === undefined || session.destroyed)
    return;

  // If the session was closed while waiting for the connect, destroy
  // the stream and do not continue with the request.
  if (session.closed) {
    const err = new ERR_HTTP2_GOAWAY_SESSION();
    this.destroy(err);
    return;
  }

  debugSessionObj(session, 'connected, initializing request');

  let streamOptions = 0;
  if (options.endStream)
    streamOptions |= STREAM_OPTION_EMPTY_PAYLOAD;

  if (options.waitForTrailers)
    streamOptions |= STREAM_OPTION_GET_TRAILERS;

  // `ret` will be either the reserved stream ID (if positive)
  // or an error code (if negative)
  const ret = session[kHandle].request(headers,
                                       streamOptions,
                                       options.parent | 0,
                                       options.weight | 0,
                                       !!options.exclusive);

  // In an error condition, one of three possible response codes will be
  // possible:
  // * NGHTTP2_ERR_STREAM_ID_NOT_AVAILABLE - Maximum stream ID is reached, this
  //   is fatal for the session
  // * NGHTTP2_ERR_INVALID_ARGUMENT - Stream was made dependent on itself, this
  //   impacts on this stream.
  // For the first two, emit the error on the session,
  // For the third, emit the error on the stream, it will bubble up to the
  // session if not handled.
  if (typeof ret === 'number') {
    let err;
    switch (ret) {
      case NGHTTP2_ERR_STREAM_ID_NOT_AVAILABLE:
        err = new ERR_HTTP2_OUT_OF_STREAMS();
        this.destroy(err);
        break;
      case NGHTTP2_ERR_INVALID_ARGUMENT:
        err = new ERR_HTTP2_STREAM_SELF_DEPENDENCY();
        this.destroy(err);
        break;
      default:
        session.destroy(new NghttpError(ret));
    }
    return;
  }
  this[kInit](ret.id(), ret);
}

// Validates that priority options are correct, specifically:
// 1. options.weight must be a number
// 2. options.parent must be a positive number
// 3. options.exclusive must be a boolean
// 4. if specified, options.silent must be a boolean
//
// Also sets the default priority options if they are not set.
const setAndValidatePriorityOptions = hideStackFrames((options) => {
  if (options.weight === undefined) {
    options.weight = NGHTTP2_DEFAULT_WEIGHT;
  } else {
    validateNumber.withoutStackTrace(options.weight, 'options.weight');
  }

  if (options.parent === undefined) {
    options.parent = 0;
  } else {
    validateNumber.withoutStackTrace(options.parent, 'options.parent', 0);
  }

  if (options.exclusive === undefined) {
    options.exclusive = false;
  } else {
    validateBoolean.withoutStackTrace(options.exclusive, 'options.exclusive');
  }

  if (options.silent === undefined) {
    options.silent = false;
  } else {
    validateBoolean.withoutStackTrace(options.silent, 'options.silent');
  }
});

// When an error occurs internally at the binding level, immediately
// destroy the session.
function onSessionInternalError(integerCode, customErrorCode) {
  if (this[kOwner] !== undefined)
    this[kOwner].destroy(new NghttpError(integerCode, customErrorCode));
}

function settingsCallback(cb, ack, duration) {
  this[kState].pendingAck--;
  this[kLocalSettings] = undefined;
  if (ack) {
    debugSessionObj(this, 'settings received');
    const settings = this.localSettings;
    if (typeof cb === 'function')
      cb(null, settings, duration);
    this.emit('localSettings', settings);
  } else {
    debugSessionObj(this, 'settings canceled');
    if (typeof cb === 'function')
      cb(new ERR_HTTP2_SETTINGS_CANCEL());
  }
}

// Submits a SETTINGS frame to be sent to the remote peer.
function submitSettings(settings, callback) {
  if (this.destroyed)
    return;
  debugSessionObj(this, 'submitting settings');
  this[kUpdateTimer]();
  updateSettingsBuffer(settings);
  if (!this[kHandle].settings(settingsCallback.bind(this, callback))) {
    this.destroy(new ERR_HTTP2_MAX_PENDING_SETTINGS_ACK());
  }
}

// Submits a PRIORITY frame to be sent to the remote peer
// Note: If the silent option is true, the change will be made
// locally with no PRIORITY frame sent.
function submitPriority(options) {
  if (this.destroyed)
    return;
  this[kUpdateTimer]();

  // If the parent is the id, do nothing because a
  // stream cannot be made to depend on itself.
  if (options.parent === this[kID])
    return;

  this[kHandle].priority(options.parent | 0,
                         options.weight | 0,
                         !!options.exclusive,
                         !!options.silent);
}

// Submit a GOAWAY frame to be sent to the remote peer.
// If the lastStreamID is set to <= 0, then the lastProcStreamID will
// be used. The opaqueData must either be a typed array or undefined
// (which will be checked elsewhere).
function submitGoaway(code, lastStreamID, opaqueData) {
  if (this.destroyed)
    return;
  debugSessionObj(this, 'submitting goaway');
  this[kUpdateTimer]();
  this[kHandle].goaway(code, lastStreamID, opaqueData);
}

const proxySocketHandler = {
  get(session, prop) {
    switch (prop) {
      case 'setTimeout':
      case 'ref':
      case 'unref':
        return session[prop].bind(session);
      case 'destroy':
      case 'emit':
      case 'end':
      case 'pause':
      case 'read':
      case 'resume':
      case 'write':
      case 'setEncoding':
      case 'setKeepAlive':
      case 'setNoDelay':
        throw new ERR_HTTP2_NO_SOCKET_MANIPULATION();
      default: {
        const socket = session[kSocket];
        if (socket === undefined)
          throw new ERR_HTTP2_SOCKET_UNBOUND();
        const value = socket[prop];
        return typeof value === 'function' ?
          value.bind(socket) :
          value;
      }
    }
  },
  getPrototypeOf(session) {
    const socket = session[kSocket];
    if (socket === undefined)
      throw new ERR_HTTP2_SOCKET_UNBOUND();
    return ReflectGetPrototypeOf(socket);
  },
  set(session, prop, value) {
    switch (prop) {
      case 'setTimeout':
      case 'ref':
      case 'unref':
        session[prop] = value;
        return true;
      case 'destroy':
      case 'emit':
      case 'end':
      case 'pause':
      case 'read':
      case 'resume':
      case 'write':
      case 'setEncoding':
      case 'setKeepAlive':
      case 'setNoDelay':
        throw new ERR_HTTP2_NO_SOCKET_MANIPULATION();
      default: {
        const socket = session[kSocket];
        if (socket === undefined)
          throw new ERR_HTTP2_SOCKET_UNBOUND();
        socket[prop] = value;
        return true;
      }
    }
  },
};

// pingCallback() returns a function that is invoked when an HTTP2 PING
// frame acknowledgement is received. The ack is either true or false to
// indicate if the ping was successful or not. The duration indicates the
// number of milliseconds elapsed since the ping was sent and the ack
// received. The payload is a Buffer containing the 8 bytes of payload
// data received on the PING acknowledgement.
function pingCallback(cb) {
  return function pingCallback(ack, duration, payload) {
    if (ack) {
      cb(null, duration, payload);
    } else {
      cb(new ERR_HTTP2_PING_CANCEL());
    }
  };
}

// Validates the values in a settings object. Specifically:
// 1. headerTableSize must be a number in the range 0 <= n <= kMaxInt
// 2. initialWindowSize must be a number in the range 0 <= n <= kMaxInt
// 3. maxFrameSize must be a number in the range 16384 <= n <= kMaxFrameSize
// 4. maxConcurrentStreams must be a number in the range 0 <= n <= kMaxStreams
// 5. maxHeaderListSize must be a number in the range 0 <= n <= kMaxInt
// 6. enablePush must be a boolean
// 7. enableConnectProtocol must be a boolean
// All settings are optional and may be left undefined
const validateSettings = hideStackFrames((settings) => {
  if (settings === undefined) return;
  assertIsObject.withoutStackTrace(settings.customSettings, 'customSettings', 'Number');
  if (settings.customSettings) {
    const entries = ObjectEntries(settings.customSettings);
    if (entries.length > MAX_ADDITIONAL_SETTINGS)
      throw new ERR_HTTP2_TOO_MANY_CUSTOM_SETTINGS();
    for (const { 0: key, 1: value } of entries) {
      assertWithinRange.withoutStackTrace('customSettings:id', Number(key), 0, 0xffff);
      assertWithinRange.withoutStackTrace('customSettings:value', Number(value), 0, kMaxInt);
    }
  }

  assertWithinRange.withoutStackTrace('headerTableSize',
                                      settings.headerTableSize,
                                      0, kMaxInt);
  assertWithinRange.withoutStackTrace('initialWindowSize',
                                      settings.initialWindowSize,
                                      0, kMaxInt);
  assertWithinRange.withoutStackTrace('maxFrameSize',
                                      settings.maxFrameSize,
                                      16384, kMaxFrameSize);
  assertWithinRange.withoutStackTrace('maxConcurrentStreams',
                                      settings.maxConcurrentStreams,
                                      0, kMaxStreams);
  assertWithinRange.withoutStackTrace('maxHeaderListSize',
                                      settings.maxHeaderListSize,
                                      0, kMaxInt);
  assertWithinRange.withoutStackTrace('maxHeaderSize',
                                      settings.maxHeaderSize,
                                      0, kMaxInt);
  if (settings.enablePush !== undefined &&
      typeof settings.enablePush !== 'boolean') {
    throw new ERR_HTTP2_INVALID_SETTING_VALUE.HideStackFramesError('enablePush',
                                                                   settings.enablePush);
  }
  if (settings.enableConnectProtocol !== undefined &&
      typeof settings.enableConnectProtocol !== 'boolean') {
    throw new ERR_HTTP2_INVALID_SETTING_VALUE.HideStackFramesError('enableConnectProtocol',
                                                                   settings.enableConnectProtocol);
  }
});

// Wrap a typed array in a proxy, and allow selectively copying the entries
// that have explicitly been set to another typed array.
function trackAssignmentsTypedArray(typedArray) {
  const typedArrayLength = typedArray.length;
  const modifiedEntries = new Uint8Array(typedArrayLength);

  function copyAssigned(target) {
    for (let i = 0; i < typedArrayLength; i++) {
      if (modifiedEntries[i]) {
        target[i] = typedArray[i];
      }
    }
  }

  return new Proxy(typedArray, {
    __proto__: null,
    get(obj, prop, receiver) {
      if (prop === 'copyAssigned') {
        return copyAssigned;
      }
      return ReflectGet(obj, prop, receiver);
    },
    set(obj, prop, value) {
      if (`${+prop}` === prop) {
        modifiedEntries[prop] = 1;
      }
      return ReflectSet(obj, prop, value);
    },
  });
}

// Creates the internal binding.Http2Session handle for an Http2Session
// instance. This occurs only after the socket connection has been
// established. Note: the binding.Http2Session will take over ownership
// of the socket. No other code should read from or write to the socket.
function setupHandle(socket, type, options) {
  // If the session has been destroyed, go ahead and emit 'connect',
  // but do nothing else. The various on('connect') handlers set by
  // core will check for session.destroyed before progressing, this
  // ensures that those at least get cleared out.
  if (this.destroyed) {
    process.nextTick(emit, this, 'connect', this, socket);
    return;
  }

  assert(socket._handle !== undefined,
         'Internal HTTP/2 Failure. The socket is not connected. Please ' +
         'report this as a bug in Node.js');

  debugSession(type, 'setting up session handle');
  this[kState].flags |= SESSION_FLAGS_READY;

  updateOptionsBuffer(options);
  if (options.remoteCustomSettings) {
    remoteCustomSettingsToBuffer(options.remoteCustomSettings);
  }
  const handle = new binding.Http2Session(type);
  handle[kOwner] = this;

  if (typeof options.selectPadding === 'function')
    this[kSelectPadding] = options.selectPadding;
  handle.consume(socket._handle);

  this[kHandle] = handle;
  if (this[kNativeFields]) {
    // If some options have already been set before the handle existed, copy
    // those (and only those) that have manually been set over.
    this[kNativeFields].copyAssigned(handle.fields);
  }

  this[kNativeFields] = handle.fields;

  if (socket.encrypted) {
    this[kAlpnProtocol] = socket.alpnProtocol;
    this[kEncrypted] = true;
  } else {
    // 'h2c' is the protocol identifier for HTTP/2 over plain-text. We use
    // it here to identify any session that is not explicitly using an
    // encrypted socket.
    this[kAlpnProtocol] = 'h2c';
    this[kEncrypted] = false;
  }

  if (isUint32(options.maxSessionInvalidFrames)) {
    const uint32 = new Uint32Array(
      this[kNativeFields].buffer, kSessionMaxInvalidFrames, 1);
    uint32[0] = options.maxSessionInvalidFrames;
  }

  if (isUint32(options.maxSessionRejectedStreams)) {
    const uint32 = new Uint32Array(
      this[kNativeFields].buffer, kSessionMaxRejectedStreams, 1);
    uint32[0] = options.maxSessionRejectedStreams;
  }

  const settings = typeof options.settings === 'object' ?
    options.settings : {};

  this.settings(settings);

  if (type === NGHTTP2_SESSION_SERVER &&
      ArrayIsArray(options.origins)) {
    ReflectApply(this.origin, this, options.origins);
  }

  process.nextTick(emit, this, 'connect', this, socket);
}

// Emits a close event followed by an error event if err is truthy. Used
// by Http2Session.prototype.destroy()
function emitClose(self, error) {
  if (error)
    self.emit('error', error);
  self.emit('close');
}

function cleanupSession(session) {
  const socket = session[kSocket];
  const handle = session[kHandle];
  session[kProxySocket] = undefined;
  session[kSocket] = undefined;
  session[kHandle] = undefined;
  session[kNativeFields] = trackAssignmentsTypedArray(
    new Uint8Array(kSessionUint8FieldCount));
  if (handle)
    handle.ondone = null;
  if (socket) {
    socket[kBoundSession] = undefined;
    socket[kServer] = undefined;
  }
}

function finishSessionClose(session, error) {
  debugSessionObj(session, 'finishSessionClose');

  const socket = session[kSocket];
  cleanupSession(session);

  if (socket && !socket.destroyed) {
    socket.on('close', () => {
      emitClose(session, error);
    });
    if (session.closed) {
      // If we're gracefully closing the socket, call resume() so we can detect
      // the peer closing in case binding.Http2Session is already gone.
      socket.resume();
    }

    // Always wait for writable side to finish.
    socket.end((err) => {
      debugSessionObj(session, 'finishSessionClose socket end', err, error);
      // If session.destroy() was called, destroy the underlying socket. Delay
      // it a bit to try to avoid ECONNRESET on Windows.
      if (!session.closed) {
        setImmediate(() => {
          socket.destroy(error);
        });
      }
    });
  } else {
    process.nextTick(emitClose, session, error);
  }
}

function closeSession(session, code, error) {
  debugSessionObj(session, 'start closing/destroying', error);

  const state = session[kState];
  state.flags |= SESSION_FLAGS_DESTROYED;
  state.destroyCode = code;

  // Clear timeout and remove timeout listeners.
  session.setTimeout(0);
  session.removeAllListeners('timeout');

  // Destroy any pending and open streams
  if (state.pendingStreams.size > 0 || state.streams.size > 0) {
    const cancel = new ERR_HTTP2_STREAM_CANCEL(error);
    state.pendingStreams.forEach((stream) => stream.destroy(cancel));
    state.streams.forEach((stream) => stream.destroy(error));
  }

  // Disassociate from the socket and server.
  const socket = session[kSocket];
  const handle = session[kHandle];

  // Destroy the handle if it exists at this point.
  if (handle !== undefined) {
    handle.ondone = finishSessionClose.bind(null, session, error);
    handle.destroy(code, socket.destroyed);
  } else {
    finishSessionClose(session, error);
  }
}

// Upon creation, the Http2Session takes ownership of the socket. The session
// may not be ready to use immediately if the socket is not yet fully connected.
// In that case, the Http2Session will wait for the socket to connect. Once
// the Http2Session is ready, it will emit its own 'connect' event.
//
// The Http2Session.goaway() method will send a GOAWAY frame, signalling
// to the connected peer that a shutdown is in progress. Sending a goaway
// frame has no other effect, however.
//
// Receiving a GOAWAY frame will cause the Http2Session to first emit a 'goaway'
// event notifying the user that a shutdown is in progress. If the goaway
// error code equals 0 (NGHTTP2_NO_ERROR), session.close() will be called,
// causing the Http2Session to send its own GOAWAY frame and switch itself
// into a graceful closing state. In this state, new inbound or outbound
// Http2Streams will be rejected. Existing *pending* streams (those created
// but without an assigned stream ID or handle) will be destroyed with a
// cancel error. Existing open streams will be permitted to complete on their
// own. Once all existing streams close, session.destroy() will be called
// automatically.
//
// Calling session.destroy() will tear down the Http2Session immediately,
// making it no longer usable. Pending and existing streams will be destroyed.
// The bound socket will be destroyed. Once all resources have been freed up,
// the 'close' event will be emitted. Note that pending streams will be
// destroyed using a specific "ERR_HTTP2_STREAM_CANCEL" error. Existing open
// streams will be destroyed using the same error passed to session.destroy()
//
// If destroy is called with an error, an 'error' event will be emitted
// immediately following the 'close' event.
//
// The socket and Http2Session lifecycles are tightly bound. Once one is
// destroyed, the other should also be destroyed. When the socket is destroyed
// with an error, session.destroy() will be called with that same error.
// Likewise, when session.destroy() is called with an error, the same error
// will be sent to the socket.
class Http2Session extends EventEmitter {
  constructor(type, options, socket) {
    super();

    // No validation is performed on the input parameters because this
    // constructor is not exported directly for users.

    // If the session property already exists on the socket,
    // then it has already been bound to an Http2Session instance
    // and cannot be attached again.
    if (socket[kBoundSession] !== undefined)
      throw new ERR_HTTP2_SOCKET_BOUND();

    socket[kBoundSession] = this;

    if (!socket._handle || !socket._handle.isStreamBase) {
      socket = new JSStreamSocket(socket);
    }
    socket.on('error', socketOnError);
    socket.on('close', socketOnClose);

    this[kState] = {
      destroyCode: NGHTTP2_NO_ERROR,
      flags: SESSION_FLAGS_PENDING,
      goawayCode: null,
      goawayLastStreamID: null,
      streams: new SafeMap(),
      pendingStreams: new SafeSet(),
      pendingAck: 0,
      shutdownWritableCalled: false,
      writeQueueSize: 0,
      originSet: undefined,
    };

    this[kEncrypted] = undefined;
    this[kAlpnProtocol] = undefined;
    this[kType] = type;
    this[kProxySocket] = null;
    this[kSocket] = socket;
    this[kTimeout] = null;
    this[kHandle] = undefined;

    // Do not use nagle's algorithm
    if (typeof socket.setNoDelay === 'function')
      socket.setNoDelay();

    // Disable TLS renegotiation on the socket
    if (typeof socket.disableRenegotiation === 'function')
      socket.disableRenegotiation();

    const setupFn = setupHandle.bind(this, socket, type, options);
    if (socket.connecting || socket.secureConnecting) {
      const connectEvent =
        socket instanceof tls.TLSSocket ? 'secureConnect' : 'connect';
      socket.once(connectEvent, () => {
        try {
          setupFn();
        } catch (error) {
          socket.destroy(error);
        }
      });
    } else {
      setupFn();
    }

    this[kNativeFields] ||= trackAssignmentsTypedArray(new Uint8Array(kSessionUint8FieldCount));
    this.on('newListener', sessionListenerAdded);
    this.on('removeListener', sessionListenerRemoved);

    // Process data on the next tick - a remoteSettings handler may be attached.
    // https://github.com/nodejs/node/issues/35981
    process.nextTick(() => {
      // Socket already has some buffered data - emulate receiving it
      // https://github.com/nodejs/node/issues/35475
      // https://github.com/nodejs/node/issues/34532
      if (socket.readableLength) {
        let buf;
        while ((buf = socket.read()) !== null) {
          debugSession(type, `${buf.length} bytes already in buffer`);
          this[kHandle].receive(buf);
        }
      }
    });

    debugSession(type, 'created');
  }

  // Returns undefined if the socket is not yet connected, true if the
  // socket is a TLSSocket, and false if it is not.
  get encrypted() {
    return this[kEncrypted];
  }

  // Returns undefined if the socket is not yet connected, `h2` if the
  // socket is a TLSSocket and the alpnProtocol is `h2`, or `h2c` if the
  // socket is not a TLSSocket.
  get alpnProtocol() {
    return this[kAlpnProtocol];
  }

  // TODO(jasnell): originSet is being added in preparation for ORIGIN frame
  // support. At the current time, the ORIGIN frame specification is awaiting
  // publication as an RFC and is awaiting implementation in nghttp2. Once
  // added, an ORIGIN frame will add to the origins included in the origin
  // set. 421 responses will remove origins from the set.
  get originSet() {
    if (!this.encrypted || this.destroyed)
      return undefined;
    return ArrayFrom(initOriginSet(this));
  }

  // True if the Http2Session is still waiting for the socket to connect
  get connecting() {
    return (this[kState].flags & SESSION_FLAGS_READY) === 0;
  }

  // True if Http2Session.prototype.close() has been called
  get closed() {
    return !!(this[kState].flags & SESSION_FLAGS_CLOSED);
  }

  // True if Http2Session.prototype.destroy() has been called
  get destroyed() {
    return !!(this[kState].flags & SESSION_FLAGS_DESTROYED);
  }

  // Resets the timeout counter
  [kUpdateTimer]() {
    if (this.destroyed)
      return;
    if (this[kTimeout]) this[kTimeout].refresh();
  }

  // Sets the id of the next stream to be created by this Http2Session.
  // The value must be a number in the range 0 <= n <= kMaxStreams. The
  // value also needs to be larger than the current next stream ID.
  setNextStreamID(id) {
    if (this.destroyed)
      throw new ERR_HTTP2_INVALID_SESSION();

    validateNumber(id, 'id');
    if (id <= 0 || id > kMaxStreams)
      throw new ERR_OUT_OF_RANGE('id', `> 0 and <= ${kMaxStreams}`, id);
    this[kHandle].setNextStreamID(id);
  }

  // Sets the local window size (local endpoints's window size)
  // Returns 0 if success or throw an exception if NGHTTP2_ERR_NOMEM
  // if the window allocation fails
  setLocalWindowSize(windowSize) {
    if (this.destroyed)
      throw new ERR_HTTP2_INVALID_SESSION();

    validateInt32(windowSize, 'windowSize', 0);
    const ret = this[kHandle].setLocalWindowSize(windowSize);

    if (ret === NGHTTP2_ERR_NOMEM) {
      this.destroy(new ERR_HTTP2_NO_MEM());
    }
  }

  // If ping is called while we are still connecting, or after close() has
  // been called, the ping callback will be invoked immediately with a ping
  // cancelled error and a duration of 0.0.
  ping(payload, callback) {
    if (this.destroyed)
      throw new ERR_HTTP2_INVALID_SESSION();

    if (typeof payload === 'function') {
      callback = payload;
      payload = undefined;
    }
    if (payload) {
      validateBuffer(payload, 'payload');
    }
    if (payload && payload.length !== 8) {
      throw new ERR_HTTP2_PING_LENGTH();
    }
    validateFunction(callback, 'callback');

    const cb = pingCallback(callback);
    if (this.connecting || this.closed) {
      process.nextTick(cb, false, 0.0, payload);
      return;
    }

    return this[kHandle].ping(payload, cb);
  }

  [kInspect](depth, opts) {
    if (typeof depth === 'number' && depth < 0)
      return this;

    const obj = {
      type: this[kType],
      closed: this.closed,
      destroyed: this.destroyed,
      state: this.state,
      localSettings: this.localSettings,
      remoteSettings: this.remoteSettings,
    };
    return `Http2Session ${format(obj)}`;
  }

  // The socket owned by this session
  get socket() {
    const proxySocket = this[kProxySocket];
    if (proxySocket === null)
      return this[kProxySocket] = new Proxy(this, proxySocketHandler);
    return proxySocket;
  }

  // The session type
  get type() {
    return this[kType];
  }

  // If a GOAWAY frame has been received, gives the error code specified
  get goawayCode() {
    return this[kState].goawayCode || NGHTTP2_NO_ERROR;
  }

  // If a GOAWAY frame has been received, gives the last stream ID reported
  get goawayLastStreamID() {
    return this[kState].goawayLastStreamID || 0;
  }

  // True if the Http2Session is waiting for a settings acknowledgement
  get pendingSettingsAck() {
    return this[kState].pendingAck > 0;
  }

  // Retrieves state information for the Http2Session
  get state() {
    return this.connecting || this.destroyed ?
      {} : getSessionState(this[kHandle]);
  }

  // The settings currently in effect for the local peer. These will
  // be updated only when a settings acknowledgement has been received.
  get localSettings() {
    const settings = this[kLocalSettings];
    if (settings !== undefined)
      return settings;

    if (this.destroyed || this.connecting)
      return {};

    return this[kLocalSettings] = getSettings(this[kHandle], false); // Local
  }

  // The settings currently in effect for the remote peer.
  get remoteSettings() {
    if (this[kNativeFields][kBitfield] &
        (1 << kSessionRemoteSettingsIsUpToDate)) {
      const settings = this[kRemoteSettings];
      if (settings !== undefined) {
        return settings;
      }
    }

    if (this.destroyed || this.connecting)
      return {};

    this[kNativeFields][kBitfield] |= (1 << kSessionRemoteSettingsIsUpToDate);
    return this[kRemoteSettings] = getSettings(this[kHandle], true); // Remote
  }

  // Submits a SETTINGS frame to be sent to the remote peer.
  settings(settings, callback) {
    if (this.destroyed)
      throw new ERR_HTTP2_INVALID_SESSION();
    assertIsObject(settings, 'settings');
    validateSettings(settings);

    if (callback) {
      validateFunction(callback, 'callback');
    }
    debugSessionObj(this, 'sending settings');

    this[kState].pendingAck++;

    const settingsFn = submitSettings.bind(this, { ...settings }, callback);
    if (this.connecting) {
      this.once('connect', settingsFn);
      return;
    }
    settingsFn();
  }

  // Submits a GOAWAY frame to be sent to the remote peer. Note that this
  // is only a notification, and does not affect the usable state of the
  // session with the notable exception that new incoming streams will
  // be rejected automatically.
  goaway(code = NGHTTP2_NO_ERROR, lastStreamID = 0, opaqueData) {
    if (this.destroyed)
      throw new ERR_HTTP2_INVALID_SESSION();

    if (opaqueData !== undefined) {
      validateBuffer(opaqueData, 'opaqueData');
    }
    validateNumber(code, 'code');
    validateNumber(lastStreamID, 'lastStreamID');

    const goawayFn = submitGoaway.bind(this, code, lastStreamID, opaqueData);
    if (this.connecting) {
      this.once('connect', goawayFn);
      return;
    }
    goawayFn();
  }

  // Destroy the Http2Session, making it no longer usable and cancelling
  // any pending activity.
  destroy(error = NGHTTP2_NO_ERROR, code) {
    if (this.destroyed)
      return;

    debugSessionObj(this, 'destroying');

    if (typeof error === 'number') {
      code = error;
      error =
        code !== NGHTTP2_NO_ERROR ?
          new ERR_HTTP2_SESSION_ERROR(code) : undefined;
    }
    if (code === undefined && error != null)
      code = NGHTTP2_INTERNAL_ERROR;

    closeSession(this, code, error);
  }

  // Closing the session will:
  // 1. Send a goaway frame
  // 2. Mark the session as closed
  // 3. Prevent new inbound or outbound streams from being opened
  // 4. Optionally register a 'close' event handler
  // 5. Will cause the session to automatically destroy after the
  //    last currently open Http2Stream closes.
  //
  // Close always assumes a good, non-error shutdown (NGHTTP_NO_ERROR)
  //
  // If the session has not connected yet, the closed flag will still be
  // set but the goaway will not be sent until after the connect event
  // is emitted.
  close(callback) {
    if (this.closed || this.destroyed)
      return;
    debugSessionObj(this, 'marking session closed');
    this[kState].flags |= SESSION_FLAGS_CLOSED;
    if (typeof callback === 'function')
      this.once('close', callback);
    this.goaway();
    this[kMaybeDestroy]();
  }

  [EventEmitter.captureRejectionSymbol](err, event, ...args) {
    switch (event) {
      case 'stream': {
        const stream = args[0];
        stream.destroy(err);
        break;
      }
      default:
        this.destroy(err);
    }
  }

  // Destroy the session if:
  // * error is not undefined/null
  // * session is closed and there are no more pending or open streams
  [kMaybeDestroy](error) {
    if (error == null) {
      const state = this[kState];
      // Do not destroy if we're not closed and there are pending/open streams
      if (!this.closed ||
          state.streams.size > 0 ||
          state.pendingStreams.size > 0) {
        return;
      }
    }
    this.destroy(error);
  }

  _onTimeout() {
    callTimeout(this, this);
  }

  ref() {
    if (this[kSocket]) {
      this[kSocket].ref();
    }
  }

  unref() {
    if (this[kSocket]) {
      this[kSocket].unref();
    }
  }
}

// ServerHttp2Session instances should never have to wait for the socket
// to connect as they are always created after the socket has already been
// established.
class ServerHttp2Session extends Http2Session {
  constructor(options, socket, server) {
    super(NGHTTP2_SESSION_SERVER, options, socket);
    this[kServer] = server;
    // This is a bit inaccurate because it does not reflect changes to
    // number of listeners made after the session was created. This should
    // not be an issue in practice. Additionally, the 'priority' event on
    // server instances (or any other object) is fully undocumented.
    this[kNativeFields][kSessionPriorityListenerCount] =
      server ? server.listenerCount('priority') : 0;
  }

  get server() {
    return this[kServer];
  }

  // Submits an altsvc frame to be sent to the client. `stream` is a
  // numeric Stream ID. origin is a URL string that will be used to get
  // the origin. alt is a string containing the altsvc details. No fancy
  // API is provided for that.
  altsvc(alt, originOrStream) {
    if (this.destroyed)
      throw new ERR_HTTP2_INVALID_SESSION();

    let stream = 0;
    let origin;

    if (typeof originOrStream === 'string') {
      origin = getURLOrigin(originOrStream);
      if (origin === 'null')
        throw new ERR_HTTP2_ALTSVC_INVALID_ORIGIN();
    } else if (typeof originOrStream === 'number') {
      if (originOrStream >>> 0 !== originOrStream || originOrStream === 0) {
        throw new ERR_OUT_OF_RANGE('originOrStream',
                                   `> 0 && < ${2 ** 32}`, originOrStream);
      }
      stream = originOrStream;
    } else if (originOrStream !== undefined) {
      // Allow origin to be passed a URL or object with origin property
      if (originOrStream !== null && typeof originOrStream === 'object')
        origin = originOrStream.origin;
      // Note: if originOrStream is an object with an origin property other
      // than a URL, then it is possible that origin will be malformed.
      // We do not verify that here. Users who go that route need to
      // ensure they are doing the right thing or the payload data will
      // be invalid.
      if (typeof origin !== 'string') {
        throw new ERR_INVALID_ARG_TYPE('originOrStream',
                                       ['string', 'number', 'URL', 'object'],
                                       originOrStream);
      } else if (origin === 'null' || origin.length === 0) {
        throw new ERR_HTTP2_ALTSVC_INVALID_ORIGIN();
      }
    }

    validateString(alt, 'alt');
    if (!kQuotedString.test(alt))
      throw new ERR_INVALID_CHAR('alt');

    // Max length permitted for ALTSVC
    if ((alt.length + (origin !== undefined ? origin.length : 0)) > kMaxALTSVC)
      throw new ERR_HTTP2_ALTSVC_LENGTH();

    this[kHandle].altsvc(stream, origin || '', alt);
  }

  // Submits an origin frame to be sent.
  origin(...origins) {
    if (this.destroyed)
      throw new ERR_HTTP2_INVALID_SESSION();

    if (origins.length === 0)
      return;

    let arr = '';
    let len = 0;
    const count = origins.length;
    for (let i = 0; i < count; i++) {
      let origin = origins[i];
      if (typeof origin === 'string') {
        origin = getURLOrigin(origin);
      } else if (origin != null && typeof origin === 'object') {
        origin = origin.origin;
      }
      validateString(origin, 'origin');
      if (origin === 'null')
        throw new ERR_HTTP2_INVALID_ORIGIN();

      arr += `${origin}\0`;
      len += origin.length;
    }

    if (len > kMaxALTSVC)
      throw new ERR_HTTP2_ORIGIN_LENGTH();

    this[kHandle].origin(arr, count);
  }

}

// ClientHttp2Session instances have to wait for the socket to connect after
// they have been created. Various operations such as request() may be used,
// but the actual protocol communication will only occur after the socket
// has been connected.
class ClientHttp2Session extends Http2Session {
  constructor(options, socket) {
    super(NGHTTP2_SESSION_CLIENT, options, socket);
    this[kPendingRequestCalls] = null;
  }

  // Submits a new HTTP2 request to the connected peer. Returns the
  // associated Http2Stream instance.
  request(headers, options) {
    debugSessionObj(this, 'initiating request');

    if (this.destroyed)
      throw new ERR_HTTP2_INVALID_SESSION();

    if (this.closed)
      throw new ERR_HTTP2_GOAWAY_SESSION();

    this[kUpdateTimer]();

    if (headers !== null && headers !== undefined) {
      const keys = ObjectKeys(headers);
      for (let i = 0; i < keys.length; i++) {
        const header = keys[i];
        if (header[0] === ':') {
          assertValidPseudoHeader(header);
        } else if (header && !checkIsHttpToken(header))
          this.destroy(new ERR_INVALID_HTTP_TOKEN('Header name', header));
      }
    }

    assertIsObject(headers, 'headers');
    assertIsObject(options, 'options');

    headers = ObjectAssign({ __proto__: null }, headers);
    options = { ...options };

    if (headers[HTTP2_HEADER_METHOD] === undefined)
      headers[HTTP2_HEADER_METHOD] = HTTP2_METHOD_GET;

    const connect = headers[HTTP2_HEADER_METHOD] === HTTP2_METHOD_CONNECT;

    if (!connect || headers[HTTP2_HEADER_PROTOCOL] !== undefined) {
      if (getAuthority(headers) === undefined)
        headers[HTTP2_HEADER_AUTHORITY] = this[kAuthority];
      if (headers[HTTP2_HEADER_SCHEME] === undefined)
        headers[HTTP2_HEADER_SCHEME] = this[kProtocol].slice(0, -1);
      if (headers[HTTP2_HEADER_PATH] === undefined)
        headers[HTTP2_HEADER_PATH] = '/';
    } else {
      if (headers[HTTP2_HEADER_AUTHORITY] === undefined)
        throw new ERR_HTTP2_CONNECT_AUTHORITY();
      if (headers[HTTP2_HEADER_SCHEME] !== undefined)
        throw new ERR_HTTP2_CONNECT_SCHEME();
      if (headers[HTTP2_HEADER_PATH] !== undefined)
        throw new ERR_HTTP2_CONNECT_PATH();
    }

    setAndValidatePriorityOptions(options);

    if (options.endStream === undefined) {
      // For some methods, we know that a payload is meaningless, so end the
      // stream by default if the user has not specifically indicated a
      // preference.
      options.endStream = isPayloadMeaningless(headers[HTTP2_HEADER_METHOD]);
    } else {
      validateBoolean(options.endStream, 'options.endStream');
    }

    const headersList = mapToHeaders(headers);

    // eslint-disable-next-line no-use-before-define
    const stream = new ClientHttp2Stream(this, undefined, undefined, {});
    stream[kSentHeaders] = headers;
    stream[kOrigin] = `${headers[HTTP2_HEADER_SCHEME]}://` +
                      `${getAuthority(headers)}`;
    const reqAsync = new AsyncResource('PendingRequest');
    stream[kRequestAsyncResource] = reqAsync;

    // Close the writable side of the stream if options.endStream is set.
    if (options.endStream)
      stream.end();

    if (options.waitForTrailers)
      stream[kState].flags |= STREAM_FLAGS_HAS_TRAILERS;

    const { signal } = options;
    if (signal) {
      validateAbortSignal(signal, 'options.signal');
      const aborter = () => {
        stream.destroy(new AbortError(undefined, { cause: signal.reason }));
      };
      if (signal.aborted) {
        aborter();
      } else {
        const disposable = addAbortListener(signal, aborter);
        stream.once('close', disposable[SymbolDispose]);
      }
    }

    const onConnect = reqAsync.bind(requestOnConnect.bind(stream, headersList, options));
    if (this.connecting) {
      if (this[kPendingRequestCalls] !== null) {
        this[kPendingRequestCalls].push(onConnect);
      } else {
        this[kPendingRequestCalls] = [onConnect];
        this.once('connect', () => {
          this[kPendingRequestCalls].forEach((f) => f());
          this[kPendingRequestCalls] = null;
        });
      }
    } else {
      onConnect();
    }
    return stream;
  }
}

function trackWriteState(stream, bytes) {
  const session = stream[kSession];
  stream[kState].writeQueueSize += bytes;
  session[kState].writeQueueSize += bytes;
  session[kHandle].chunksSentSinceLastWrite = 0;
}

function streamOnResume() {
  if (!this.destroyed)
    this[kHandle].readStart();
}

function streamOnPause() {
  if (!this.destroyed && !this.pending)
    this[kHandle].readStop();
}

function afterShutdown(status) {
  const stream = this.handle[kOwner];
  if (stream) {
    stream.on('finish', () => {
      stream[kMaybeDestroy]();
    });
  }
  // Currently this status value is unused
  this.callback();
}

function shutdownWritable(callback) {
  const handle = this[kHandle];
  if (!handle) return callback();
  const state = this[kState];
  if (state.shutdownWritableCalled) {
    debugStreamObj(this, 'shutdownWritable() already called');
    return callback();
  }
  state.shutdownWritableCalled = true;

  const req = new ShutdownWrap();
  req.oncomplete = afterShutdown;
  req.callback = callback;
  req.handle = handle;
  const err = handle.shutdown(req);
  if (err === 1)  // synchronous finish
    return ReflectApply(afterShutdown, req, [0]);
}

function finishSendTrailers(stream, headersList) {
  // The stream might be destroyed and in that case
  // there is nothing to do.
  // This can happen because finishSendTrailers is
  // scheduled via setImmediate.
  if (stream.destroyed) {
    return;
  }

  stream[kState].flags &= ~STREAM_FLAGS_HAS_TRAILERS;

  const ret = stream[kHandle].trailers(headersList);
  if (ret < 0)
    stream.destroy(new NghttpError(ret));
  else
    stream[kMaybeDestroy]();
}

const kNoRstStream = 0;
const kSubmitRstStream = 1;
const kForceRstStream = 2;

function closeStream(stream, code, rstStreamStatus = kSubmitRstStream) {
  const state = stream[kState];
  state.flags |= STREAM_FLAGS_CLOSED;
  state.rstCode = code;

  // Clear timeout and remove timeout listeners
  stream.setTimeout(0);
  stream.removeAllListeners('timeout');

  const { ending } = stream._writableState;

  if (!ending) {
    // If the writable side of the Http2Stream is still open, emit the
    // 'aborted' event and set the aborted flag.
    if (!stream.aborted) {
      state.flags |= STREAM_FLAGS_ABORTED;
      stream.emit('aborted');
    }

    // Close the writable side.
    stream.end();
  }

  if (rstStreamStatus !== kNoRstStream) {
    const finishFn = finishCloseStream.bind(stream, code);
    if (!ending || stream.writableFinished || code !== NGHTTP2_NO_ERROR ||
        rstStreamStatus === kForceRstStream)
      finishFn();
    else
      stream.once('finish', finishFn);
  }
}

function finishCloseStream(code) {
  const rstStreamFn = submitRstStream.bind(this, code);
  // If the handle has not yet been assigned, queue up the request to
  // ensure that the RST_STREAM frame is sent after the stream ID has
  // been determined.
  if (this.pending) {
    this.push(null);
    this.once('ready', rstStreamFn);
    return;
  }
  rstStreamFn();
}

// An Http2Stream is a Duplex stream that is backed by a
// node::http2::Http2Stream handle implementing StreamBase.
class Http2Stream extends Duplex {
  constructor(session, options) {
    options.allowHalfOpen = true;
    options.decodeStrings = false;
    options.autoDestroy = false;
    super(options);
    this[async_id_symbol] = -1;

    // Corking the stream automatically allows writes to happen
    // but ensures that those are buffered until the handle has
    // been assigned.
    this.cork();
    this[kSession] = session;
    session[kState].pendingStreams.add(this);

    // Allow our logic for determining whether any reads have happened to
    // work in all situations. This is similar to what we do in _http_incoming.
    this._readableState.readingMore = true;

    this[kTimeout] = null;

    this[kState] = {
      didRead: false,
      flags: STREAM_FLAGS_PENDING,
      rstCode: NGHTTP2_NO_ERROR,
      writeQueueSize: 0,
      trailersReady: false,
      endAfterHeaders: false,
    };

    // Fields used by the compat API to avoid megamorphisms.
    this[kRequest] = null;
    this[kProxySocket] = null;

    this.on('pause', streamOnPause);

    this.on('newListener', streamListenerAdded);
    this.on('removeListener', streamListenerRemoved);
  }

  [kUpdateTimer]() {
    if (this.destroyed)
      return;
    if (this[kTimeout])
      this[kTimeout].refresh();
    if (this[kSession])
      this[kSession][kUpdateTimer]();
  }

  [kInit](id, handle) {
    const state = this[kState];
    state.flags |= STREAM_FLAGS_READY;

    const session = this[kSession];
    session[kState].pendingStreams.delete(this);
    session[kState].streams.set(id, this);

    this[kID] = id;
    this[async_id_symbol] = handle.getAsyncId();
    handle[kOwner] = this;
    this[kHandle] = handle;
    handle.onread = onStreamRead;
    this.uncork();
    this.emit('ready');
  }

  [kInspect](depth, opts) {
    if (typeof depth === 'number' && depth < 0)
      return this;

    const obj = {
      id: this[kID] || '<pending>',
      closed: this.closed,
      destroyed: this.destroyed,
      state: this.state,
      readableState: this._readableState,
      writableState: this._writableState,
    };
    return `Http2Stream ${format(obj)}`;
  }

  get bufferSize() {
    // `bufferSize` properties of `net.Socket` are `undefined` when
    // their `_handle` are falsy. Here we avoid the behavior.
    return this[kState].writeQueueSize + this.writableLength;
  }

  get endAfterHeaders() {
    return this[kState].endAfterHeaders;
  }

  get sentHeaders() {
    return this[kSentHeaders];
  }

  get sentTrailers() {
    return this[kSentTrailers];
  }

  get sentInfoHeaders() {
    return this[kInfoHeaders];
  }

  get pending() {
    return this[kID] === undefined;
  }

  // The id of the Http2Stream, will be undefined if the socket is not
  // yet connected.
  get id() {
    return this[kID];
  }

  // The Http2Session that owns this Http2Stream.
  get session() {
    return this[kSession];
  }

  _onTimeout() {
    callTimeout(this, this[kSession]);
  }

  // True if the HEADERS frame has been sent
  get headersSent() {
    return !!(this[kState].flags & STREAM_FLAGS_HEADERS_SENT);
  }

  // True if the Http2Stream was aborted abnormally.
  get aborted() {
    return !!(this[kState].flags & STREAM_FLAGS_ABORTED);
  }

  // True if dealing with a HEAD request
  get headRequest() {
    return !!(this[kState].flags & STREAM_FLAGS_HEAD_REQUEST);
  }

  // The error code reported when this Http2Stream was closed.
  get rstCode() {
    return this[kState].rstCode;
  }

  // State information for the Http2Stream
  get state() {
    const id = this[kID];
    if (this.destroyed || id === undefined)
      return {};
    return getStreamState(this[kHandle], id);
  }

  [kProceed]() {
    assert.fail('Implementers MUST implement this. Please report this as a ' +
                'bug in Node.js');
  }

  [kAfterAsyncWrite]({ bytes }) {
    this[kState].writeQueueSize -= bytes;

    if (this.session !== undefined)
      this.session[kState].writeQueueSize -= bytes;
  }

  [kWriteGeneric](writev, data, encoding, cb) {
    // When the Http2Stream is first created, it is corked until the
    // handle and the stream ID is assigned. However, if the user calls
    // uncork() before that happens, the Duplex will attempt to pass
    // writes through. Those need to be queued up here.
    if (this.pending) {
      this.once(
        'ready',
        this[kWriteGeneric].bind(this, writev, data, encoding, cb),
      );
      return;
    }

    // If the stream has been destroyed, there's nothing else we can do
    // because the handle has been destroyed. This should only be an
    // issue if a write occurs before the 'ready' event in the case where
    // the duplex is uncorked before the stream is ready to go. In that
    // case, drop the data on the floor. An error should have already been
    // emitted.
    if (this.destroyed)
      return;

    this[kUpdateTimer]();
    if (!this.headersSent)
      this[kProceed]();

    let req;

    let waitingForWriteCallback = true;
    let waitingForEndCheck = true;
    let writeCallbackErr;
    let endCheckCallbackErr;
    const done = () => {
      if (waitingForEndCheck || waitingForWriteCallback) return;
      const err = aggregateTwoErrors(endCheckCallbackErr, writeCallbackErr);
      // writeGeneric does not destroy on error and
      // we cannot enable autoDestroy,
      // so make sure to destroy on error.
      if (err) {
        this.destroy(err);
      }
      cb(err);
    };
    const writeCallback = (err) => {
      waitingForWriteCallback = false;
      writeCallbackErr = err;
      done();
    };
    const endCheckCallback = (err) => {
      waitingForEndCheck = false;
      endCheckCallbackErr = err;
      done();
    };
    // Shutdown write stream right after last chunk is sent
    // so final DATA frame can include END_STREAM flag
    process.nextTick(() => {
      if (writeCallbackErr ||
        !this._writableState.ending ||
        this._writableState.buffered.length ||
        (this[kState].flags & STREAM_FLAGS_HAS_TRAILERS))
        return endCheckCallback();
      debugStreamObj(this, 'shutting down writable on last write');
      shutdownWritable.call(this, endCheckCallback);
    });

    if (writev)
      req = writevGeneric(this, data, writeCallback);
    else
      req = writeGeneric(this, data, encoding, writeCallback);

    trackWriteState(this, req.bytes);
  }

  _write(data, encoding, cb) {
    this[kWriteGeneric](false, data, encoding, cb);
  }

  _writev(data, cb) {
    this[kWriteGeneric](true, data, '', cb);
  }

  _final(cb) {
    if (this.pending) {
      this.once('ready', () => this._final(cb));
      return;
    }
    debugStreamObj(this, 'shutting down writable on _final');
    ReflectApply(shutdownWritable, this, [cb]);
  }

  _read(nread) {
    if (this.destroyed) {
      this.push(null);
      return;
    }
    if (!this[kState].didRead) {
      this._readableState.readingMore = false;
      this[kState].didRead = true;
    }
    if (!this.pending) {
      streamOnResume.call(this);
    } else {
      this.once('ready', streamOnResume);
    }
  }

  priority(options) {
    if (this.destroyed)
      throw new ERR_HTTP2_INVALID_STREAM();

    assertIsObject(options, 'options');
    options = { ...options };
    setAndValidatePriorityOptions(options);

    const priorityFn = submitPriority.bind(this, options);

    // If the handle has not yet been assigned, queue up the priority
    // frame to be sent as soon as the ready event is emitted.
    if (this.pending) {
      this.once('ready', priorityFn);
      return;
    }
    priorityFn();
  }

  sendTrailers(headers) {
    if (this.destroyed || this.closed)
      throw new ERR_HTTP2_INVALID_STREAM();
    if (this[kSentTrailers])
      throw new ERR_HTTP2_TRAILERS_ALREADY_SENT();
    if (!this[kState].trailersReady)
      throw new ERR_HTTP2_TRAILERS_NOT_READY();

    assertIsObject(headers, 'headers');
    headers = ObjectAssign({ __proto__: null }, headers);

    debugStreamObj(this, 'sending trailers');

    this[kUpdateTimer]();

    const headersList = mapToHeaders(headers, assertValidPseudoHeaderTrailer);
    this[kSentTrailers] = headers;

    // Send the trailers in setImmediate so we don't do it on nghttp2 stack.
    setImmediate(finishSendTrailers, this, headersList);
  }

  get closed() {
    return !!(this[kState].flags & STREAM_FLAGS_CLOSED);
  }

  // Close initiates closing the Http2Stream instance by sending an RST_STREAM
  // frame to the connected peer. The readable and writable sides of the
  // Http2Stream duplex are closed and the timeout timer is cleared. If
  // a callback is passed, it is registered to listen for the 'close' event.
  //
  // If the handle and stream ID have not been assigned yet, the close
  // will be queued up to wait for the ready event. As soon as the stream ID
  // is determined, the close will proceed.
  //
  // Submitting the RST_STREAM frame to the underlying handle will cause
  // the Http2Stream to be closed and ultimately destroyed. After calling
  // close, it is still possible to queue up PRIORITY and RST_STREAM frames,
  // but no DATA and HEADERS frames may be sent.
  close(code = NGHTTP2_NO_ERROR, callback) {
    validateInteger(code, 'code', 0, kMaxInt);

    if (callback !== undefined) {
      validateFunction(callback, 'callback');
    }

    if (this.closed)
      return;

    if (callback !== undefined)
      this.once('close', callback);

    closeStream(this, code);
  }

  // Called by this.destroy().
  // * Will submit an RST stream to shutdown the stream if necessary.
  //   This will cause the internal resources to be released.
  // * Then cleans up the resources on the js side
  _destroy(err, callback) {
    const session = this[kSession];
    const handle = this[kHandle];
    const id = this[kID];

    debugStream(this[kID] || 'pending', session[kType], 'destroying stream');

    const state = this[kState];
    const sessionState = session[kState];
    const sessionCode = sessionState.goawayCode || sessionState.destroyCode;

    // If a stream has already closed successfully, there is no error
    // to report from this stream, even if the session has errored.
    // This can happen if the stream was already in process of destroying
    // after a successful close, but the session had a error between
    // this stream's close and destroy operations.
    // Previously, this always overrode a successful close operation code
    // NGHTTP2_NO_ERROR (0) with sessionCode because the use of the || operator.
    let code = this.closed ? this.rstCode : sessionCode;
    if (err != null) {
      if (sessionCode) {
        code = sessionCode;
      } else if (err instanceof AbortError) {
        // Enables using AbortController to cancel requests with RST code 8.
        code = NGHTTP2_CANCEL;
      } else {
        code = NGHTTP2_INTERNAL_ERROR;
      }
    }
    const hasHandle = handle !== undefined;

    if (!this.closed)
      closeStream(this, code, hasHandle ? kForceRstStream : kNoRstStream);
    this.push(null);

    if (hasHandle) {
      handle.destroy();
      sessionState.streams.delete(id);
    } else {
      sessionState.pendingStreams.delete(this);
    }

    // Adjust the write queue size for accounting
    sessionState.writeQueueSize -= state.writeQueueSize;
    state.writeQueueSize = 0;

    // RST code 8 not emitted as an error as its used by clients to signify
    // abort and is already covered by aborted event, also allows more
    // seamless compatibility with http1
    if (err == null && code !== NGHTTP2_NO_ERROR && code !== NGHTTP2_CANCEL)
      err = new ERR_HTTP2_STREAM_ERROR(nameForErrorCode[code] || code);

    this[kSession] = undefined;
    this[kHandle] = undefined;

    // This notifies the session that this stream has been destroyed and
    // gives the session the opportunity to clean itself up. The session
    // will destroy if it has been closed and there are no other open or
    // pending streams. Delay with setImmediate so we don't do it on the
    // nghttp2 stack.
    setImmediate(() => {
      session[kMaybeDestroy]();
    });
    callback(err);
  }
  // The Http2Stream can be destroyed if it has closed and if the readable
  // side has received the final chunk.
  [kMaybeDestroy](code = NGHTTP2_NO_ERROR) {
    if (code !== NGHTTP2_NO_ERROR) {
      this.destroy();
      return;
    }

    if (this.writableFinished) {
      if (!this.readable && this.closed) {
        this.destroy();
        return;
      }

      // We've submitted a response from our server session, have not attempted
      // to process any incoming data, and have no trailers. This means we can
      // attempt to gracefully close the session.
      const state = this[kState];
      if (this.headersSent &&
          this[kSession] &&
          this[kSession][kType] === NGHTTP2_SESSION_SERVER &&
          !(state.flags & STREAM_FLAGS_HAS_TRAILERS) &&
          !state.didRead &&
          this.readableFlowing === null) {
        // By using setImmediate we allow pushStreams to make it through
        // before the stream is officially closed. This prevents a bug
        // in most browsers where those pushStreams would be rejected.
        setImmediate(callStreamClose, this);
      }
    }
  }
}

function callTimeout(self, session) {
  // If the session is destroyed, this should never actually be invoked,
  // but just in case...
  if (self.destroyed)
    return;
  // This checks whether a write is currently in progress and also whether
  // that write is actually sending data across the write. The kHandle
  // stored `chunksSentSinceLastWrite` is only updated when a timeout event
  // happens, meaning that if a write is ongoing it should never equal the
  // newly fetched, updated value.
  if (self[kState].writeQueueSize > 0) {
    const handle = session[kHandle];
    const chunksSentSinceLastWrite = handle !== undefined ?
      handle.chunksSentSinceLastWrite : null;
    if (chunksSentSinceLastWrite !== null &&
      chunksSentSinceLastWrite !== handle.updateChunksSent()) {
      self[kUpdateTimer]();
      return;
    }
  }

  self.emit('timeout');
}

function callStreamClose(stream) {
  stream.close();
}

function processHeaders(oldHeaders, options) {
  assertIsObject(oldHeaders, 'headers');
  const headers = { __proto__: null };

  if (oldHeaders !== null && oldHeaders !== undefined) {
    // This loop is here for performance reason. Do not change.
    for (const key in oldHeaders) {
      if (ObjectHasOwn(oldHeaders, key)) {
        headers[key] = oldHeaders[key];
      }
    }
    headers[kSensitiveHeaders] = oldHeaders[kSensitiveHeaders];
  }

  const statusCode =
    headers[HTTP2_HEADER_STATUS] =
      headers[HTTP2_HEADER_STATUS] | 0 || HTTP_STATUS_OK;

  if (options.sendDate == null || options.sendDate) {
    headers[HTTP2_HEADER_DATE] ??= utcDate();
  }

  // This is intentionally stricter than the HTTP/1 implementation, which
  // allows values between 100 and 999 (inclusive) in order to allow for
  // backwards compatibility with non-spec compliant code. With HTTP/2,
  // we have the opportunity to start fresh with stricter spec compliance.
  // This will have an impact on the compatibility layer for anyone using
  // non-standard, non-compliant status codes.
  if (statusCode < 200 || statusCode > 599)
    throw new ERR_HTTP2_STATUS_INVALID(headers[HTTP2_HEADER_STATUS]);

  const neverIndex = headers[kSensitiveHeaders];
  if (neverIndex !== undefined && !ArrayIsArray(neverIndex))
    throw new ERR_INVALID_ARG_VALUE('headers[http2.neverIndex]', neverIndex);

  return headers;
}


function onFileUnpipe() {
  const stream = this.sink[kOwner];
  if (stream.ownsFd)
    this.source.close().catch(stream.destroy.bind(stream));
  else
    this.source.releaseFD();
}

// This is only called once the pipe has returned back control, so
// it only has to handle errors and End-of-File.
function onPipedFileHandleRead() {
  const err = streamBaseState[kReadBytesOrError];
  if (err < 0 && err !== UV_EOF) {
    this.stream.close(NGHTTP2_INTERNAL_ERROR);
  }
}

function processRespondWithFD(self, fd, headers, offset = 0, length = -1,
                              streamOptions = 0) {
  const state = self[kState];
  state.flags |= STREAM_FLAGS_HEADERS_SENT;

  let headersList;
  try {
    headersList = mapToHeaders(headers, assertValidPseudoHeaderResponse);
  } catch (err) {
    self.destroy(err);
    return;
  }
  self[kSentHeaders] = headers;

  // Close the writable side of the stream, but only as far as the writable
  // stream implementation is concerned.
  self._final = null;
  self.end();

  const ret = self[kHandle].respond(headersList, streamOptions);

  if (ret < 0) {
    self.destroy(new NghttpError(ret));
    return;
  }

  defaultTriggerAsyncIdScope(self[async_id_symbol], startFilePipe,
                             self, fd, offset, length);
}

function startFilePipe(self, fd, offset, length) {
  const handle = new FileHandle(fd, offset, length);
  handle.onread = onPipedFileHandleRead;
  handle.stream = self;

  const pipe = new StreamPipe(handle, self[kHandle]);
  pipe.onunpipe = onFileUnpipe;
  pipe.start();

  // Exact length of the file doesn't matter here, since the
  // stream is closing anyway - just use 1 to signify that
  // a write does exist
  trackWriteState(self, 1);
}

function doSendFD(session, options, fd, headers, streamOptions, err, stat) {
  if (err) {
    this.destroy(err);
    return;
  }

  // This can happen if the stream is destroyed or closed while we are waiting
  // for the file descriptor to be opened or the stat call to be completed.
  // In either case, we do not want to continue because the we are shutting
  // down and should not attempt to send any data.
  if (this.destroyed || this.closed) {
    this.destroy(new ERR_HTTP2_INVALID_STREAM());
    return;
  }

  const statOptions = {
    offset: options.offset !== undefined ? options.offset : 0,
    length: options.length !== undefined ? options.length : -1,
  };

  // options.statCheck is a user-provided function that can be used to
  // verify stat values, override or set headers, or even cancel the
  // response operation. If statCheck explicitly returns false, the
  // response is canceled. The user code may also send a separate type
  // of response so check again for the HEADERS_SENT flag
  if ((typeof options.statCheck === 'function' &&
       ReflectApply(options.statCheck, this,
                    [stat, headers, statOptions]) === false) ||
       (this[kState].flags & STREAM_FLAGS_HEADERS_SENT)) {
    return;
  }

  processRespondWithFD(this, fd, headers,
                       statOptions.offset | 0,
                       statOptions.length | 0,
                       streamOptions);
}

function doSendFileFD(session, options, fd, headers, streamOptions, err, stat) {
  const onError = options.onError;

  if (err) {
    tryClose(fd);
    if (onError)
      onError(err);
    else
      this.destroy(err);
    return;
  }

  if (!stat.isFile()) {
    const isDirectory = stat.isDirectory();
    if (options.offset !== undefined || options.offset > 0 ||
        options.length !== undefined || options.length >= 0 ||
        isDirectory) {
      const err = isDirectory ?
        new ERR_HTTP2_SEND_FILE() : new ERR_HTTP2_SEND_FILE_NOSEEK();
      tryClose(fd);
      if (onError)
        onError(err);
      else
        this.destroy(err);
      return;
    }

    options.offset = -1;
    options.length = -1;
  }

  if (this.destroyed || this.closed) {
    tryClose(fd);
    this.destroy(new ERR_HTTP2_INVALID_STREAM());
    return;
  }

  const statOptions = {
    offset: options.offset !== undefined ? options.offset : 0,
    length: options.length !== undefined ? options.length : -1,
  };

  // options.statCheck is a user-provided function that can be used to
  // verify stat values, override or set headers, or even cancel the
  // response operation. If statCheck explicitly returns false, the
  // response is canceled. The user code may also send a separate type
  // of response so check again for the HEADERS_SENT flag
  if ((typeof options.statCheck === 'function' &&
       ReflectApply(options.statCheck, this, [stat, headers]) === false) ||
       (this[kState].flags & STREAM_FLAGS_HEADERS_SENT)) {
    tryClose(fd);
    return;
  }

  if (stat.isFile()) {
    statOptions.length =
      statOptions.length < 0 ? stat.size - (+statOptions.offset) :
        MathMin(stat.size - (+statOptions.offset),
                statOptions.length);

    headers[HTTP2_HEADER_CONTENT_LENGTH] = statOptions.length;
  }

  processRespondWithFD(this, fd, headers,
                       options.offset | 0,
                       statOptions.length | 0,
                       streamOptions);
}

function afterOpen(session, options, headers, streamOptions, err, fd) {
  const state = this[kState];
  const onError = options.onError;
  if (err) {
    if (onError)
      onError(err);
    else
      this.destroy(err);
    return;
  }
  if (this.destroyed || this.closed) {
    tryClose(fd);
    return;
  }
  state.fd = fd;

  fs.fstat(fd,
           doSendFileFD.bind(this, session, options, fd, headers, streamOptions));
}

class ServerHttp2Stream extends Http2Stream {
  constructor(session, handle, id, options, headers) {
    super(session, options);
    handle.owner = this;
    this[kInit](id, handle);
    this[kProtocol] = headers[HTTP2_HEADER_SCHEME];
    this[kAuthority] = getAuthority(headers);
  }

  // True if the remote peer accepts push streams
  get pushAllowed() {
    return !this.destroyed &&
           !this.closed &&
           !this.session.closed &&
           !this.session.destroyed &&
           this[kSession].remoteSettings.enablePush;
  }

  // Create a push stream, call the given callback with the created
  // Http2Stream for the push stream.
  pushStream(headers, options, callback) {
    if (!this.pushAllowed)
      throw new ERR_HTTP2_PUSH_DISABLED();
    if (this[kID] % 2 === 0)
      throw new ERR_HTTP2_NESTED_PUSH();

    const session = this[kSession];

    debugStreamObj(this, 'initiating push stream');

    this[kUpdateTimer]();

    if (typeof options === 'function') {
      callback = options;
      options = undefined;
    }

    validateFunction(callback, 'callback');

    assertIsObject(options, 'options');
    options = { ...options };
    options.endStream = !!options.endStream;

    assertIsObject(headers, 'headers');
    headers = ObjectAssign({ __proto__: null }, headers);

    if (headers[HTTP2_HEADER_METHOD] === undefined)
      headers[HTTP2_HEADER_METHOD] = HTTP2_METHOD_GET;
    if (getAuthority(headers) === undefined)
      headers[HTTP2_HEADER_AUTHORITY] = this[kAuthority];
    if (headers[HTTP2_HEADER_SCHEME] === undefined)
      headers[HTTP2_HEADER_SCHEME] = this[kProtocol];
    if (headers[HTTP2_HEADER_PATH] === undefined)
      headers[HTTP2_HEADER_PATH] = '/';

    let headRequest = false;
    if (headers[HTTP2_HEADER_METHOD] === HTTP2_METHOD_HEAD)
      headRequest = options.endStream = true;

    const headersList = mapToHeaders(headers);

    const streamOptions = options.endStream ? STREAM_OPTION_EMPTY_PAYLOAD : 0;

    const ret = this[kHandle].pushPromise(headersList, streamOptions);
    let err;
    if (typeof ret === 'number') {
      switch (ret) {
        case NGHTTP2_ERR_STREAM_ID_NOT_AVAILABLE:
          err = new ERR_HTTP2_OUT_OF_STREAMS();
          break;
        case NGHTTP2_ERR_STREAM_CLOSED:
          err = new ERR_HTTP2_INVALID_STREAM();
          break;
        default:
          err = new NghttpError(ret);
          break;
      }
      process.nextTick(callback, err);
      return;
    }

    const id = ret.id();
    const stream = new ServerHttp2Stream(session, ret, id, options, headers);
    stream[kSentHeaders] = headers;

    stream.push(null);

    if (options.endStream)
      stream.end();

    if (headRequest)
      stream[kState].flags |= STREAM_FLAGS_HEAD_REQUEST;

    process.nextTick(callback, null, stream, headers, 0);
  }

  // Initiate a response on this Http2Stream
  respond(headers, options) {
    if (this.destroyed || this.closed)
      throw new ERR_HTTP2_INVALID_STREAM();
    if (this.headersSent)
      throw new ERR_HTTP2_HEADERS_SENT();

    const state = this[kState];

    assertIsObject(options, 'options');
    options = { ...options };

    debugStreamObj(this, 'initiating response');
    this[kUpdateTimer]();

    options.endStream = !!options.endStream;

    let streamOptions = 0;
    if (options.endStream)
      streamOptions |= STREAM_OPTION_EMPTY_PAYLOAD;

    if (options.waitForTrailers) {
      streamOptions |= STREAM_OPTION_GET_TRAILERS;
      state.flags |= STREAM_FLAGS_HAS_TRAILERS;
    }

    headers = processHeaders(headers, options);
    const headersList = mapToHeaders(headers, assertValidPseudoHeaderResponse);
    this[kSentHeaders] = headers;

    state.flags |= STREAM_FLAGS_HEADERS_SENT;

    // Close the writable side if the endStream option is set or status
    // is one of known codes with no payload, or it's a head request
    const statusCode = headers[HTTP2_HEADER_STATUS] | 0;
    if (!!options.endStream ||
        statusCode === HTTP_STATUS_NO_CONTENT ||
        statusCode === HTTP_STATUS_RESET_CONTENT ||
        statusCode === HTTP_STATUS_NOT_MODIFIED ||
        this.headRequest === true) {
      options.endStream = true;
      this.end();
    }

    const ret = this[kHandle].respond(headersList, streamOptions);
    if (ret < 0)
      this.destroy(new NghttpError(ret));
  }

  // Initiate a response using an open FD. Note that there are fewer
  // protections with this approach. For one, the fd is not validated by
  // default. In respondWithFile, the file is checked to make sure it is a
  // regular file, here the fd is passed directly. If the underlying
  // mechanism is not able to read from the fd, then the stream will be
  // reset with an error code.
  respondWithFD(fd, headers, options) {
    if (this.destroyed || this.closed)
      throw new ERR_HTTP2_INVALID_STREAM();
    if (this.headersSent)
      throw new ERR_HTTP2_HEADERS_SENT();

    const session = this[kSession];

    assertIsObject(options, 'options');
    options = { ...options };

    if (options.offset !== undefined && typeof options.offset !== 'number')
      throw new ERR_INVALID_ARG_VALUE('options.offset', options.offset);

    if (options.length !== undefined && typeof options.length !== 'number')
      throw new ERR_INVALID_ARG_VALUE('options.length', options.length);

    if (options.statCheck !== undefined &&
        typeof options.statCheck !== 'function') {
      throw new ERR_INVALID_ARG_VALUE('options.statCheck', options.statCheck);
    }

    let streamOptions = 0;
    if (options.waitForTrailers) {
      streamOptions |= STREAM_OPTION_GET_TRAILERS;
      this[kState].flags |= STREAM_FLAGS_HAS_TRAILERS;
    }

    if (fd instanceof fsPromisesInternal.FileHandle)
      fd = fd.fd;
    else if (typeof fd !== 'number')
      throw new ERR_INVALID_ARG_TYPE('fd', ['number', 'FileHandle'], fd);

    debugStreamObj(this, 'initiating response from fd');
    this[kUpdateTimer]();
    this.ownsFd = false;

    headers = processHeaders(headers, options);
    const statusCode = headers[HTTP2_HEADER_STATUS] |= 0;
    // Payload/DATA frames are not permitted in these cases
    if (statusCode === HTTP_STATUS_NO_CONTENT ||
        statusCode === HTTP_STATUS_RESET_CONTENT ||
        statusCode === HTTP_STATUS_NOT_MODIFIED ||
        this.headRequest) {
      throw new ERR_HTTP2_PAYLOAD_FORBIDDEN(statusCode);
    }

    if (options.statCheck !== undefined) {
      fs.fstat(fd,
               doSendFD.bind(this, session, options, fd, headers, streamOptions));
      return;
    }

    processRespondWithFD(this, fd, headers,
                         options.offset,
                         options.length,
                         streamOptions);
  }

  // Initiate a file response on this Http2Stream. The path is passed to
  // fs.open() to acquire the fd with mode 'r', then the fd is passed to
  // fs.fstat(). Assuming fstat is successful, a check is made to ensure
  // that the file is a regular file, then options.statCheck is called,
  // giving the user an opportunity to verify the details and set additional
  // headers. If statCheck returns false, the operation is aborted and no
  // file details are sent.
  respondWithFile(path, headers, options) {
    if (this.destroyed || this.closed)
      throw new ERR_HTTP2_INVALID_STREAM();
    if (this.headersSent)
      throw new ERR_HTTP2_HEADERS_SENT();

    assertIsObject(options, 'options');
    options = { ...options };

    if (options.offset !== undefined && typeof options.offset !== 'number')
      throw new ERR_INVALID_ARG_VALUE('options.offset', options.offset);

    if (options.length !== undefined && typeof options.length !== 'number')
      throw new ERR_INVALID_ARG_VALUE('options.length', options.length);

    if (options.statCheck !== undefined &&
        typeof options.statCheck !== 'function') {
      throw new ERR_INVALID_ARG_VALUE('options.statCheck', options.statCheck);
    }

    let streamOptions = 0;
    if (options.waitForTrailers) {
      streamOptions |= STREAM_OPTION_GET_TRAILERS;
      this[kState].flags |= STREAM_FLAGS_HAS_TRAILERS;
    }

    const session = this[kSession];
    debugStreamObj(this, 'initiating response from file');
    this[kUpdateTimer]();
    this.ownsFd = true;

    headers = processHeaders(headers, options);
    const statusCode = headers[HTTP2_HEADER_STATUS] |= 0;
    // Payload/DATA frames are not permitted in these cases
    if (statusCode === HTTP_STATUS_NO_CONTENT ||
        statusCode === HTTP_STATUS_RESET_CONTENT ||
        statusCode === HTTP_STATUS_NOT_MODIFIED ||
        this.headRequest) {
      throw new ERR_HTTP2_PAYLOAD_FORBIDDEN(statusCode);
    }

    fs.open(path, 'r',
            afterOpen.bind(this, session, options, headers, streamOptions));
  }

  // Sends a block of informational headers. In theory, the HTTP/2 spec
  // allows sending a HEADER block at any time during a streams lifecycle,
  // but the HTTP request/response semantics defined in HTTP/2 places limits
  // such that HEADERS may only be sent *before* or *after* DATA frames.
  // If the block of headers being sent includes a status code, it MUST be
  // a 1xx informational code and it MUST be sent before the request/response
  // headers are sent, or an error will be thrown.
  additionalHeaders(headers) {
    if (this.destroyed || this.closed)
      throw new ERR_HTTP2_INVALID_STREAM();
    if (this.headersSent)
      throw new ERR_HTTP2_HEADERS_AFTER_RESPOND();

    assertIsObject(headers, 'headers');
    headers = ObjectAssign({ __proto__: null }, headers);

    debugStreamObj(this, 'sending additional headers');

    if (headers[HTTP2_HEADER_STATUS] != null) {
      const statusCode = headers[HTTP2_HEADER_STATUS] |= 0;
      if (statusCode === HTTP_STATUS_SWITCHING_PROTOCOLS)
        throw new ERR_HTTP2_STATUS_101();
      if (statusCode < 100 || statusCode >= 200) {
        throw new ERR_HTTP2_INVALID_INFO_STATUS(headers[HTTP2_HEADER_STATUS]);
      }
    }

    this[kUpdateTimer]();

    const headersList = mapToHeaders(headers, assertValidPseudoHeaderResponse);
    if (!this[kInfoHeaders])
      this[kInfoHeaders] = [headers];
    else
      this[kInfoHeaders].push(headers);

    const ret = this[kHandle].info(headersList);
    if (ret < 0)
      this.destroy(new NghttpError(ret));
  }
}

ServerHttp2Stream.prototype[kProceed] = ServerHttp2Stream.prototype.respond;

class ClientHttp2Stream extends Http2Stream {
  constructor(session, handle, id, options) {
    super(session, options);
    this[kState].flags |= STREAM_FLAGS_HEADERS_SENT;
    if (id !== undefined)
      this[kInit](id, handle);
    this.on('headers', handleHeaderContinue);
  }
}

function handleHeaderContinue(headers) {
  if (headers[HTTP2_HEADER_STATUS] === HTTP_STATUS_CONTINUE)
    this.emit('continue');
}

const setTimeoutValue = {
  configurable: true,
  enumerable: true,
  writable: true,
  value: setStreamTimeout,
};
ObjectDefineProperty(Http2Stream.prototype, 'setTimeout', setTimeoutValue);
ObjectDefineProperty(Http2Session.prototype, 'setTimeout', setTimeoutValue);


// When the socket emits an error, destroy the associated Http2Session and
// forward it the same error.
function socketOnError(error) {
  const session = this[kBoundSession];
  if (session !== undefined) {
    // We can ignore ECONNRESET after GOAWAY was received as there's nothing
    // we can do and the other side is fully within its rights to do so.
    if (error.code === 'ECONNRESET' && session[kState].goawayCode !== null)
      return session.destroy();
    debugSessionObj(this, 'socket error [%s]', error.message);
    session.destroy(error);
  }
}

// Handles the on('stream') event for a session and forwards
// it on to the server object.
function sessionOnStream(stream, headers, flags, rawHeaders) {
  if (this[kServer] !== undefined)
    this[kServer].emit('stream', stream, headers, flags, rawHeaders);
}

function sessionOnPriority(stream, parent, weight, exclusive) {
  if (this[kServer] !== undefined)
    this[kServer].emit('priority', stream, parent, weight, exclusive);
}

function sessionOnError(error) {
  if (this[kServer] !== undefined)
    this[kServer].emit('sessionError', error, this);
}

// When the session times out on the server, try emitting a timeout event.
// If no handler is registered, destroy the session.
function sessionOnTimeout() {
  // If destroyed or closed already, do nothing
  if (this.destroyed || this.closed)
    return;
  const server = this[kServer];
  if (!server.emit('timeout', this))
    this.destroy();  // No error code, just things down.
}

function connectionListener(socket) {
  debug('Http2Session server: received a connection');
  const options = this[kOptions] || {};

  if (socket.alpnProtocol === false || socket.alpnProtocol === 'http/1.1') {
    // Fallback to HTTP/1.1
    if (options.allowHTTP1 === true) {
      socket.server[kIncomingMessage] = options.Http1IncomingMessage;
      socket.server[kServerResponse] = options.Http1ServerResponse;
      return httpConnectionListener.call(this, socket);
    }
    // Let event handler deal with the socket
    debug('Unknown protocol from %s:%s',
          socket.remoteAddress, socket.remotePort);
    if (!this.emit('unknownProtocol', socket)) {
      debug('Unknown protocol timeout:  %s', options.unknownProtocolTimeout);
      // Install a timeout if the socket was not successfully closed, then
      // destroy the socket to ensure that the underlying resources are
      // released.
      const timer = setTimeout(() => {
        if (!socket.destroyed) {
          debug('UnknownProtocol socket timeout, destroy socket');
          socket.destroy();
        }
      }, options.unknownProtocolTimeout);
      // Un-reference the timer to avoid blocking of application shutdown and
      // clear the timeout if the socket was successfully closed.
      timer.unref();

      socket.once('close', () => clearTimeout(timer));

      // We don't know what to do, so let's just tell the other side what's
      // going on in a format that they *might* understand.
      socket.end('HTTP/1.0 403 Forbidden\r\n' +
                 'Content-Type: text/plain\r\n\r\n' +
                 'Missing ALPN Protocol, expected `h2` to be available.\n' +
                 'If this is a HTTP request: The server was not ' +
                 'configured with the `allowHTTP1` option or a ' +
                 'listener for the `unknownProtocol` event.\n');
    }
    return;
  }

  // Set up the Session
  const session = new ServerHttp2Session(options, socket, this);

  session.on('stream', sessionOnStream);
  session.on('error', sessionOnError);
  // Don't count our own internal listener.
  session.on('priority', sessionOnPriority);
  session[kNativeFields][kSessionPriorityListenerCount]--;

  if (this.timeout)
    session.setTimeout(this.timeout, sessionOnTimeout);

  socket[kServer] = this;

  this.emit('session', session);
}

function initializeOptions(options) {
  assertIsObject(options, 'options');
  options = { ...options };
  assertIsObject(options.settings, 'options.settings');
  options.settings = { ...options.settings };

  assertIsArray(options.remoteCustomSettings, 'options.remoteCustomSettings');
  if (options.remoteCustomSettings) {
    options.remoteCustomSettings = [ ...options.remoteCustomSettings ];
    if (options.remoteCustomSettings.length > MAX_ADDITIONAL_SETTINGS)
      throw new ERR_HTTP2_TOO_MANY_CUSTOM_SETTINGS();
  }

  if (options.maxSessionInvalidFrames !== undefined)
    validateUint32(options.maxSessionInvalidFrames, 'maxSessionInvalidFrames');

  if (options.maxSessionRejectedStreams !== undefined) {
    validateUint32(
      options.maxSessionRejectedStreams,
      'maxSessionRejectedStreams',
    );
  }

  if (options.unknownProtocolTimeout !== undefined)
    validateUint32(options.unknownProtocolTimeout, 'unknownProtocolTimeout');
  else
    // TODO(danbev): is this a good default value?
    options.unknownProtocolTimeout = 10000;


  // Used only with allowHTTP1
  options.Http1IncomingMessage ||= http.IncomingMessage;
  options.Http1ServerResponse ||= http.ServerResponse;

  options.Http2ServerRequest ||= Http2ServerRequest;
  options.Http2ServerResponse ||= Http2ServerResponse;
  return options;
}

function initializeTLSOptions(options, servername) {
  options = initializeOptions(options);

  if (!options.ALPNCallback) {
    options.ALPNProtocols = ['h2'];
    if (options.allowHTTP1 === true)
      options.ALPNProtocols.push('http/1.1');
  }

  if (servername !== undefined && !options.servername)
    options.servername = servername;
  return options;
}

function onErrorSecureServerSession(err, socket) {
  if (!this.emit('clientError', err, socket))
    socket.destroy(err);
}

class Http2SecureServer extends TLSServer {
  constructor(options, requestListener) {
    options = initializeTLSOptions(options);
    super(options, connectionListener);
    this[kOptions] = options;
    this.timeout = 0;
    this.on('newListener', setupCompat);
    if (options.allowHTTP1 === true) {
      this.headersTimeout = 60_000; // Minimum between 60 seconds or requestTimeout
      this.requestTimeout = 300_000; // 5 minutes
      this.connectionsCheckingInterval = 30_000; // 30 seconds
      this.on('listening', setupConnectionsTracking);
    }
    if (typeof requestListener === 'function')
      this.on('request', requestListener);
    this.on('tlsClientError', onErrorSecureServerSession);
  }

  setTimeout(msecs, callback) {
    this.timeout = msecs;
    if (callback !== undefined) {
      validateFunction(callback, 'callback');
      this.on('timeout', callback);
    }
    return this;
  }

  updateSettings(settings) {
    assertIsObject(settings, 'settings');
    validateSettings(settings);
    this[kOptions].settings = { ...this[kOptions].settings, ...settings };
  }

  close() {
    if (this[kOptions].allowHTTP1 === true) {
      httpServerPreClose(this);
    }
    ReflectApply(TLSServer.prototype.close, this, arguments);
  }

  closeIdleConnections() {
    if (this[kOptions].allowHTTP1 === true) {
      ReflectApply(HttpServer.prototype.closeIdleConnections, this, arguments);
    }
  }
}

class Http2Server extends NETServer {
  constructor(options, requestListener) {
    options = initializeOptions(options);
    super(options, connectionListener);
    this[kOptions] = options;
    this.timeout = 0;
    this.on('newListener', setupCompat);
    if (typeof requestListener === 'function')
      this.on('request', requestListener);
  }

  setTimeout(msecs, callback) {
    this.timeout = msecs;
    if (callback !== undefined) {
      validateFunction(callback, 'callback');
      this.on('timeout', callback);
    }
    return this;
  }

  updateSettings(settings) {
    assertIsObject(settings, 'settings');
    validateSettings(settings);
    this[kOptions].settings = { ...this[kOptions].settings, ...settings };
  }

  async [SymbolAsyncDispose]() {
    return promisify(super.close).call(this);
  }
}

Http2Server.prototype[EventEmitter.captureRejectionSymbol] = function(
  err, event, ...args) {

  switch (event) {
    case 'stream': {
      // TODO(mcollina): we might want to match this with what we do on
      // the compat side.
      const { 0: stream } = args;
      if (stream.sentHeaders) {
        stream.destroy(err);
      } else {
        stream.respond({ [HTTP2_HEADER_STATUS]: 500 });
        stream.end();
      }
      break;
    }
    case 'request': {
      const { 1: res } = args;
      if (!res.headersSent && !res.finished) {
        // Don't leak headers.
        for (const name of res.getHeaderNames()) {
          res.removeHeader(name);
        }
        res.statusCode = 500;
        res.end(http.STATUS_CODES[500]);
      } else {
        res.destroy();
      }
      break;
    }
    default:
      args.unshift(err, event);
      ReflectApply(net.Server.prototype[EventEmitter.captureRejectionSymbol],
                   this, args);
  }
};

function setupCompat(ev) {
  if (ev === 'request') {
    this.removeListener('newListener', setupCompat);
    this.on('stream', onServerStream.bind(this, this[kOptions].Http2ServerRequest,
                                          this[kOptions].Http2ServerResponse));
  }
}

function socketOnClose() {
  const session = this[kBoundSession];
  if (session !== undefined) {
    debugSessionObj(session, 'socket closed');
    const err = session.connecting ? new ERR_SOCKET_CLOSED() : null;
    const state = session[kState];
    state.streams.forEach((stream) => stream.close(NGHTTP2_CANCEL));
    state.pendingStreams.forEach((stream) => stream.close(NGHTTP2_CANCEL));
    session.close();
    session[kMaybeDestroy](err);
  }
}

function connect(authority, options, listener) {
  if (typeof options === 'function') {
    listener = options;
    options = undefined;
  }

  assertIsObject(options, 'options');
  options = { ...options };

  assertIsArray(options.remoteCustomSettings, 'options.remoteCustomSettings');
  if (options.remoteCustomSettings) {
    options.remoteCustomSettings = [ ...options.remoteCustomSettings ];
    if (options.remoteCustomSettings.length > MAX_ADDITIONAL_SETTINGS)
      throw new ERR_HTTP2_TOO_MANY_CUSTOM_SETTINGS();
  }

  if (typeof authority === 'string')
    authority = new URL(authority);

  assertIsObject(authority, 'authority', ['string', 'Object', 'URL']);

  const protocol = authority.protocol || options.protocol || 'https:';
  const port = '' + (authority.port !== '' ?
    authority.port : (authority.protocol === 'http:' ? 80 : 443));
  let host = 'localhost';

  if (authority.hostname) {
    host = authority.hostname;

    if (host[0] === '[')
      host = host.slice(1, -1);
  } else if (authority.host) {
    host = authority.host;
  }

  let socket;
  if (typeof options.createConnection === 'function') {
    socket = options.createConnection(authority, options);
  } else {
    switch (protocol) {
      case 'http:':
        socket = net.connect({ port, host, ...options });
        break;
      case 'https:':
        socket = tls.connect(port, host, initializeTLSOptions(options, net.isIP(host) ? undefined : host));
        break;
      default:
        throw new ERR_HTTP2_UNSUPPORTED_PROTOCOL(protocol);
    }
  }

  const session = new ClientHttp2Session(options, socket);

  session[kAuthority] = `${options.servername || host}:${port}`;
  session[kProtocol] = protocol;

  if (typeof listener === 'function')
    session.once('connect', listener);

  return session;
}

// Support util.promisify
ObjectDefineProperty(connect, promisify.custom, {
  __proto__: null,
  value: (authority, options) => {
    return new Promise((resolve, reject) => {
      const server = connect(authority, options, () => {
        server.removeListener('error', reject);
        return resolve(server);
      });

      server.once('error', reject);
    });
  },
});

function createSecureServer(options, handler) {
  return new Http2SecureServer(options, handler);
}

function createServer(options, handler) {
  if (typeof options === 'function') {
    handler = options;
    options = kEmptyObject;
  }
  return new Http2Server(options, handler);
}

// Returns a Base64 encoded settings frame payload from the given
// object. The value is suitable for passing as the value of the
// HTTP2-Settings header frame.
function getPackedSettings(settings) {
  assertIsObject(settings, 'settings');
  validateSettings(settings);
  updateSettingsBuffer({ ...settings });
  return binding.packSettings();
}

function getUnpackedSettings(buf, options = kEmptyObject) {
  if (!isArrayBufferView(buf) || buf.length === undefined) {
    throw new ERR_INVALID_ARG_TYPE('buf',
                                   ['Buffer', 'TypedArray'], buf);
  }
  if (buf.length % 6 !== 0)
    throw new ERR_HTTP2_INVALID_PACKED_SETTINGS_LENGTH();
  const settings = {};
  let offset = 0;
  while (offset < buf.length) {
    const id = ReflectApply(readUInt16BE, buf, [offset]);
    offset += 2;
    const value = ReflectApply(readUInt32BE, buf, [offset]);
    switch (id) {
      case NGHTTP2_SETTINGS_HEADER_TABLE_SIZE:
        settings.headerTableSize = value;
        break;
      case NGHTTP2_SETTINGS_ENABLE_PUSH:
        settings.enablePush = value !== 0;
        break;
      case NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS:
        settings.maxConcurrentStreams = value;
        break;
      case NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE:
        settings.initialWindowSize = value;
        break;
      case NGHTTP2_SETTINGS_MAX_FRAME_SIZE:
        settings.maxFrameSize = value;
        break;
      case NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE:
        settings.maxHeaderListSize = settings.maxHeaderSize = value;
        break;
      case NGHTTP2_SETTINGS_ENABLE_CONNECT_PROTOCOL:
        settings.enableConnectProtocol = value !== 0;
        break;
      default:
        settings.customSettings ||= {};
        settings.customSettings[id] = value;
    }
    offset += 4;
  }

  if (options != null && options.validate)
    validateSettings(settings);

  return settings;
}

function performServerHandshake(socket, options = {}) {
  options = initializeOptions(options);
  return new ServerHttp2Session(options, socket, undefined);
}

binding.setCallbackFunctions(
  onSessionInternalError,
  onPriority,
  onSettings,
  onPing,
  onSessionHeaders,
  onFrameError,
  onGoawayData,
  onAltSvc,
  onOrigin,
  onStreamTrailers,
  onStreamClose,
);

// Exports
module.exports = {
  connect,
  constants,
  createServer,
  createSecureServer,
  getDefaultSettings,
  getPackedSettings,
  getUnpackedSettings,
  performServerHandshake,
  sensitiveHeaders: kSensitiveHeaders,
  Http2Session,
  Http2Stream,
  ServerHttp2Session,
  Http2ServerRequest,
  Http2ServerResponse,
};
