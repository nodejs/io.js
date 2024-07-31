interface QuicCallbacks {
  onEndpointClose: (context: number, status: number) => void;
  onSessionNew: (session: Session) => void;
  onSessionClose: (type: number, code: bigint, reason?: string) => void;
  onSessionDatagram: (datagram: Uint8Array, early: boolean) => void;
  onSessionDatagramStatus: (id: bigint, status: string) => void;
  onSessionHandshake: (sni: string,
                       alpn: string,
                       cipher: string,
                       cipherVersion: string,
                       validationReason?: string,
                       validationCode?: string) => void;
  onSessionPathValidation: (result: string,
                            local: SocketAddress,
                            remote: SocketAddress,
                            preferred: boolean) => void;
  onSessionTicket: (ticket: ArrayBuffer) => void;
  onSessionVersionNegotiation: (version: number,
                                versions: number[],
                                supports: number[]) => void;
  onStreamCreated: (stream: Stream) => void;
  onStreamBlocked: () => void;
  onStreamClose: (error: [number,bigint,string]) => void;
  onStreamReset: (error: [number,bigint,string]) => void;
  onStreamHeaders: (headers: string[], kind: number) => void;
  onStreamTrailers: () => void;
}

interface EndpointOptions {
  address?: SocketAddress;
  retryTokenExpiration?: number|bigint;
  tokenExpiration?: number|bigint;
  maxConnectionsPerHost?: number|bigint;
  maxConnectionsTotal?: number|bigint;
  maxStatelessResetsPerHost?: number|bigint;
  addressLRUSize?: number|bigint;
  maxRetries?: number|bigint;
  maxPayloadSize?: number|bigint;
  unacknowledgedPacketThreshold?: number|bigint;
  validateAddress?: boolean;
  disableStatelessReset?: boolean;
  ipv6Only?: boolean;
  udpReceiveBufferSize?: number;
  udpSendBufferSize?: number;
  udpTTL?: number;
  resetTokenSecret?: ArrayBufferView;
  tokenSecret?: ArrayBufferView;
  cc?: 'reno'|'cubic'|'pcc'|'bbr'| 0 | 2 | 3 | 4;
}

interface SessionOptions {}
interface SocketAddress {}

interface Session {}
interface Stream {}

interface Endpoint {
  listen(options: SessionOptions): void;
  connect(address: SocketAddress, options: SessionOptions): Session;
  closeGracefully(): void;
  markBusy(on?: boolean): void;
  ref(on?: boolean): void;
  address(): SocketAddress|void;
  readonly state: ArrayBuffer;
  readonly stats: ArrayBuffer;
}

export interface QuicBinding {
  setCallbacks(callbacks: QuicCallbacks): void;
  flushPacketFreeList(): void;

  readonly IDX_STATS_ENDPOINT_CREATED_AT: number;
  readonly IDX_STATS_ENDPOINT_DESTROYED_AT: number;
  readonly IDX_STATS_ENDPOINT_BYTES_RECEIVED: number;
  readonly IDX_STATS_ENDPOINT_BYTES_SENT: number;
  readonly IDX_STATS_ENDPOINT_PACKETS_RECEIVED: number;
  readonly IDX_STATS_ENDPOINT_PACKETS_SENT: number;
  readonly IDX_STATS_ENDPOINT_SERVER_SESSIONS: number;
  readonly IDX_STATS_ENDPOINT_CLIENT_SESSIONS: number;
  readonly IDX_STATS_ENDPOINT_SERVER_BUSY_COUNT: number;
  readonly IDX_STATS_ENDPOINT_RETRY_COUNT: number;
  readonly IDX_STATS_ENDPOINT_VERSION_NEGOTIATION_COUNT: number;
  readonly IDX_STATS_ENDPOINT_STATELESS_RESET_COUNT: number;
  readonly IDX_STATS_ENDPOINT_IMMEDIATE_CLOSE_COUNT: number;
  readonly IDX_STATS_ENDPOINT_COUNT: number;
  readonly IDX_STATE_ENDPOINT_BOUND: number;
  readonly IDX_STATE_ENDPOINT_BOUND_SIZE: number;
  readonly IDX_STATE_ENDPOINT_RECEIVING: number;
  readonly IDX_STATE_ENDPOINT_RECEIVING_SIZE: number;
  readonly IDX_STATE_ENDPOINT_LISTENING: number;
  readonly IDX_STATE_ENDPOINT_LISTENING_SIZE: number;
  readonly IDX_STATE_ENDPOINT_CLOSING: number;
  readonly IDX_STATE_ENDPOINT_CLOSING_SIZE: number;
  readonly IDX_STATE_ENDPOINT_WAITING_FOR_CALLBACKS: number;
  readonly IDX_STATE_ENDPOINT_WAITING_FOR_CALLBACKS_SIZE: number;
  readonly IDX_STATE_ENDPOINT_BUSY: number;
  readonly IDX_STATE_ENDPOINT_BUSY_SIZE: number;
  readonly IDX_STATE_ENDPOINT_PENDING_CALLBACKS: number;
  readonly IDX_STATE_ENDPOINT_PENDING_CALLBACKS_SIZE: number;
}
