// Flags: --expose-internals
'use strict';

const { hasQuic } = require('../common');

const {
  describe,
  it,
} = require('node:test');

describe('quic internal endpoint stats and state', { skip: !hasQuic }, () => {
  const {
    Endpoint,
    QuicStreamState,
    QuicStreamStats,
    SessionState,
    SessionStats,
    kFinishClose,
  } = require('internal/quic/quic');

  const {
    inspect,
  } = require('util');

  const {
    deepStrictEqual,
    strictEqual,
    throws,
  } = require('node:assert');

  it('endpoint state', () => {
    const endpoint = new Endpoint({
      onsession() {},
      session: {},
      stream: {},
    });

    strictEqual(endpoint.state.isBound, false);
    strictEqual(endpoint.state.isReceiving, false);
    strictEqual(endpoint.state.isListening, false);
    strictEqual(endpoint.state.isClosing, false);
    strictEqual(endpoint.state.isBusy, false);
    strictEqual(endpoint.state.pendingCallbacks, 0n);

    deepStrictEqual(JSON.parse(JSON.stringify(endpoint.state)), {
      isBound: false,
      isReceiving: false,
      isListening: false,
      isClosing: false,
      isBusy: false,
      pendingCallbacks: '0',
    });

    endpoint.busy = true;
    strictEqual(endpoint.state.isBusy, true);
    endpoint.busy = false;
    strictEqual(endpoint.state.isBusy, false);

    it('state can be inspected without errors', () => {
      strictEqual(typeof inspect(endpoint.state), 'string');
    });
  });

  it('state is not readable after close', () => {
    const endpoint = new Endpoint({
      onsession() {},
      session: {},
      stream: {},
    }, {});
    endpoint.state[kFinishClose]();
    throws(() => endpoint.state.isBound, {
      name: 'Error',
    });
  });

  it('state constructor argument is ArrayBuffer', () => {
    const endpoint = new Endpoint({
      onsession() {},
      session: {},
      stream: {},
    }, {});
    const Cons = endpoint.state.constructor;
    throws(() => new Cons(1), {
      code: 'ERR_INVALID_ARG_TYPE'
    });
  });

  it('endpoint stats', () => {
    const endpoint = new Endpoint({
      onsession() {},
      session: {},
      stream: {},
    });

    strictEqual(typeof endpoint.stats.createdAt, 'bigint');
    strictEqual(typeof endpoint.stats.destroyedAt, 'bigint');
    strictEqual(typeof endpoint.stats.bytesReceived, 'bigint');
    strictEqual(typeof endpoint.stats.bytesSent, 'bigint');
    strictEqual(typeof endpoint.stats.packetsReceived, 'bigint');
    strictEqual(typeof endpoint.stats.packetsSent, 'bigint');
    strictEqual(typeof endpoint.stats.serverSessions, 'bigint');
    strictEqual(typeof endpoint.stats.clientSessions, 'bigint');
    strictEqual(typeof endpoint.stats.serverBusyCount, 'bigint');
    strictEqual(typeof endpoint.stats.retryCount, 'bigint');
    strictEqual(typeof endpoint.stats.versionNegotiationCount, 'bigint');
    strictEqual(typeof endpoint.stats.statelessResetCount, 'bigint');
    strictEqual(typeof endpoint.stats.immediateCloseCount, 'bigint');

    deepStrictEqual(Object.keys(endpoint.stats.toJSON()), [
      'createdAt',
      'destroyedAt',
      'bytesReceived',
      'bytesSent',
      'packetsReceived',
      'packetsSent',
      'serverSessions',
      'clientSessions',
      'serverBusyCount',
      'retryCount',
      'versionNegotiationCount',
      'statelessResetCount',
      'immediateCloseCount',
    ]);

    it('stats can be inspected without errors', () => {
      strictEqual(typeof inspect(endpoint.stats), 'string');
    });
  });

  it('stats are still readble after close', () => {
    const endpoint = new Endpoint({
      onsession() {},
      session: {},
      stream: {},
    }, {});
    strictEqual(typeof endpoint.stats.toJSON(), 'object');
    endpoint.stats[kFinishClose]();
    strictEqual(typeof endpoint.stats.destroyedAt, 'bigint');
    strictEqual(typeof endpoint.stats.toJSON(), 'object');
  });

  it('stats constructor argument is ArrayBuffer', () => {
    const endpoint = new Endpoint({
      onsession() {},
      session: {},
      stream: {},
    }, {});
    const Cons = endpoint.stats.constructor;
    throws(() => new Cons(1), {
      code: 'ERR_INVALID_ARG_TYPE',
    });
  });

  // TODO(@jasnell): The following tests are largely incomplete.
  // This is largely here to boost the code coverage numbers
  // temporarily while the rest of the functionality is being
  // implemented.
  it('stream and session states', () => {
    const streamState = new QuicStreamState(new ArrayBuffer(1024));
    const sessionState = new SessionState(new ArrayBuffer(1024));

    strictEqual(streamState.finSent, false);
    strictEqual(streamState.finReceived, false);
    strictEqual(streamState.readEnded, false);
    strictEqual(streamState.writeEnded, false);
    strictEqual(streamState.destroyed, false);
    strictEqual(streamState.paused, false);
    strictEqual(streamState.reset, false);
    strictEqual(streamState.hasReader, false);
    strictEqual(streamState.wantsBlock, false);
    strictEqual(streamState.wantsHeaders, false);
    strictEqual(streamState.wantsReset, false);
    strictEqual(streamState.wantsTrailers, false);

    strictEqual(sessionState.hasPathValidationListener, false);
    strictEqual(sessionState.hasVersionNegotiationListener, false);
    strictEqual(sessionState.hasDatagramListener, false);
    strictEqual(sessionState.hasSessionTicketListener, false);
    strictEqual(sessionState.isClosing, false);
    strictEqual(sessionState.isGracefulClose, false);
    strictEqual(sessionState.isSilentClose, false);
    strictEqual(sessionState.isStatelessReset, false);
    strictEqual(sessionState.isDestroyed, false);
    strictEqual(sessionState.isHandshakeCompleted, false);
    strictEqual(sessionState.isHandshakeConfirmed, false);
    strictEqual(sessionState.isStreamOpenAllowed, false);
    strictEqual(sessionState.isPrioritySupported, false);
    strictEqual(sessionState.isWrapped, false);
    strictEqual(sessionState.lastDatagramId, 0n);

    strictEqual(typeof streamState.toJSON(), 'object');
    strictEqual(typeof sessionState.toJSON(), 'object');
    strictEqual(typeof inspect(streamState), 'string');
    strictEqual(typeof inspect(sessionState), 'string');
  });

  it('stream and session stats', () => {
    const streamStats = new QuicStreamStats(new ArrayBuffer(1024));
    const sessionStats = new SessionStats(new ArrayBuffer(1024));
    strictEqual(streamStats.createdAt, undefined);
    strictEqual(streamStats.receivedAt, undefined);
    strictEqual(streamStats.ackedAt, undefined);
    strictEqual(streamStats.closingAt, undefined);
    strictEqual(streamStats.destroyedAt, undefined);
    strictEqual(streamStats.bytesReceived, undefined);
    strictEqual(streamStats.bytesSent, undefined);
    strictEqual(streamStats.maxOffset, undefined);
    strictEqual(streamStats.maxOffsetAcknowledged, undefined);
    strictEqual(streamStats.maxOffsetReceived, undefined);
    strictEqual(streamStats.finalSize, undefined);
    strictEqual(typeof streamStats.toJSON(), 'object');
    strictEqual(typeof inspect(streamStats), 'string');
    streamStats[kFinishClose]();

    strictEqual(typeof sessionStats.createdAt, 'bigint');
    strictEqual(typeof sessionStats.closingAt, 'bigint');
    strictEqual(typeof sessionStats.destroyedAt, 'bigint');
    strictEqual(typeof sessionStats.handshakeCompletedAt, 'bigint');
    strictEqual(typeof sessionStats.handshakeConfirmedAt, 'bigint');
    strictEqual(typeof sessionStats.gracefulClosingAt, 'bigint');
    strictEqual(typeof sessionStats.bytesReceived, 'bigint');
    strictEqual(typeof sessionStats.bytesSent, 'bigint');
    strictEqual(typeof sessionStats.bidiInStreamCount, 'bigint');
    strictEqual(typeof sessionStats.bidiOutStreamCount, 'bigint');
    strictEqual(typeof sessionStats.uniInStreamCount, 'bigint');
    strictEqual(typeof sessionStats.uniOutStreamCount, 'bigint');
    strictEqual(typeof sessionStats.lossRetransmitCount, 'bigint');
    strictEqual(typeof sessionStats.maxBytesInFlights, 'bigint');
    strictEqual(typeof sessionStats.bytesInFlight, 'bigint');
    strictEqual(typeof sessionStats.blockCount, 'bigint');
    strictEqual(typeof sessionStats.cwnd, 'bigint');
    strictEqual(typeof sessionStats.latestRtt, 'bigint');
    strictEqual(typeof sessionStats.minRtt, 'bigint');
    strictEqual(typeof sessionStats.rttVar, 'bigint');
    strictEqual(typeof sessionStats.smoothedRtt, 'bigint');
    strictEqual(typeof sessionStats.ssthresh, 'bigint');
    strictEqual(typeof sessionStats.datagramsReceived, 'bigint');
    strictEqual(typeof sessionStats.datagramsSent, 'bigint');
    strictEqual(typeof sessionStats.datagramsAcknowledged, 'bigint');
    strictEqual(typeof sessionStats.datagramsLost, 'bigint');
    strictEqual(typeof sessionStats.toJSON(), 'object');
    strictEqual(typeof inspect(sessionStats), 'string');
    streamStats[kFinishClose]();
  });
});
