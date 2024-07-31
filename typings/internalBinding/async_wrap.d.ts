import { owner_symbol } from './symbols';

declare namespace InternalAsyncWrapBinding {
  interface Resource {
    [owner_symbol]?: PublicResource;
  }
  type PublicResource = object;

  type EmitHook = (asyncId: number) => void;

  type PromiseHook = (promise: Promise<unknown>, parent: Promise<unknown>) => void;

  interface Providers {
    NONE: 0;
    DIRHANDLE: 1;
    DNSCHANNEL: 2;
    ELDHISTOGRAM: 3;
    FILEHANDLE: 4;
    FILEHANDLECLOSEREQ: 5;
    FIXEDSIZEBLOBCOPY: 6;
    FSEVENTWRAP: 7;
    FSREQCALLBACK: 8;
    FSREQPROMISE: 9;
    GETADDRINFOREQWRAP: 10;
    GETNAMEINFOREQWRAP: 11;
    HEAPSNAPSHOT: 12;
    HTTP2SESSION: 13;
    HTTP2STREAM: 14;
    HTTP2PING: 15;
    HTTP2SETTINGS: 16;
    HTTPINCOMINGMESSAGE: 17;
    HTTPCLIENTREQUEST: 18;
    JSSTREAM: 19;
    JSUDPWRAP: 20;
    MESSAGEPORT: 21;
    PIPECONNECTWRAP: 22;
    PIPESERVERWRAP: 23;
    PIPEWRAP: 24;
    PROCESSWRAP: 25;
    PROMISE: 26;
    QUERYWRAP: 27;
    SHUTDOWNWRAP: 28;
    SIGNALWRAP: 29;
    STATWATCHER: 30;
    STREAMPIPE: 31;
    TCPCONNECTWRAP: 32;
    TCPSERVERWRAP: 33;
    TCPWRAP: 34;
    TTYWRAP: 35;
    UDPSENDWRAP: 36;
    UDPWRAP: 37;
    SIGINTWATCHDOG: 38;
    WORKER: 39;
    WORKERHEAPSNAPSHOT: 40;
    WRITEWRAP: 41;
    ZLIB: 42;
    CHECKPRIMEREQUEST: 43;
    PBKDF2REQUEST: 44;
    KEYPAIRGENREQUEST: 45;
    KEYGENREQUEST: 46;
    KEYEXPORTREQUEST: 47;
    CIPHERREQUEST: 48;
    DERIVEBITSREQUEST: 49;
    HASHREQUEST: 50;
    RANDOMBYTESREQUEST: 51;
    RANDOMPRIMEREQUEST: 52;
    SCRYPTREQUEST: 53;
    SIGNREQUEST: 54;
    TLSWRAP: 55;
    VERIFYREQUEST: 56;
  }
}

export interface AsyncWrapBinding {
  setupHooks(): {
    init: (
      asyncId: number,
      type: keyof InternalAsyncWrapBinding.Providers,
      triggerAsyncId: number,
      resource: InternalAsyncWrapBinding.Resource,
    ) => void;
    before: InternalAsyncWrapBinding.EmitHook;
    after: InternalAsyncWrapBinding.EmitHook;
    destroy: InternalAsyncWrapBinding.EmitHook;
    promise_resolve: InternalAsyncWrapBinding.EmitHook;
  };
  setCallbackTrampoline(
    callback: (
      asyncId: number,
      resource: InternalAsyncWrapBinding.Resource,
      cb: (
        cb: (...args: any[]) => boolean | undefined,
        ...args: any[]
      ) => boolean | undefined,
      ...args: any[]
    ) => boolean | undefined
  ): void;
  pushAsyncContext(asyncId: number, triggerAsyncId: number): void;
  popAsyncContext(asyncId: number): boolean;
  executionAsyncResource(index: number): InternalAsyncWrapBinding.PublicResource | null;
  clearAsyncIdStack(): void;
  queueDestroyAsyncId(asyncId: number): void;
  setPromiseHooks(
    initHook: ((promise: Promise<unknown>, parent?: Promise<unknown>) => void) | undefined,
    promiseBeforeHook: InternalAsyncWrapBinding.PromiseHook | undefined,
    promiseAfterHook: InternalAsyncWrapBinding.PromiseHook | undefined,
    promiseResolveHook: InternalAsyncWrapBinding.PromiseHook | undefined
  ): void;
  registerDestroyHook(resource: object, asyncId: number, destroyed?: { destroyed: boolean }): void;
  async_hook_fields: Uint32Array;
  async_id_fields: Float64Array;
  async_ids_stack: Float64Array;
  execution_async_resources: InternalAsyncWrapBinding.Resource[];
  constants: {
    kInit: 0;
    kBefore: 1;
    kAfter: 2;
    kDestroy: 3;
    kPromiseResolve: 4;
    kTotals: 5;
    kCheck: 6;
    kStackLength: 7;
    kUsesExecutionAsyncResource: 8;

    kExecutionAsyncId: 0;
    kTriggerAsyncId: 1;
    kAsyncIdCounter: 2;
    kDefaultTriggerAsyncId: 3;
  };
  Providers: InternalAsyncWrapBinding.Providers;
}
