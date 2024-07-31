declare namespace InternalUtilBinding {
  class WeakReference<T> {
    constructor(value: T);
    get(): undefined | T;
    incRef(): void;
    decRef(): void;
  }
}

export interface UtilBinding {
  // PER_ISOLATE_PRIVATE_SYMBOL_PROPERTIES, defined in src/env_properties.h
  arrow_message_private_symbol: 1;
  contextify_context_private_symbol: 2;
  decorated_private_symbol: 3;
  napi_type_tag: 4;
  napi_wrapper: 5;
  untransferable_object_private_symbol: 6;
  exiting_aliased_Uint32Array: 7;

  kPending: 0;
  kFulfilled: 1;
  kRejected: 2;

  getHiddenValue(object: object, index: number): any;
  setHiddenValue(object: object, index: number, value: any): boolean;
  getPromiseDetails(promise: any): undefined | [state: 0] | [state: 1 | 2, result: any];
  getProxyDetails(proxy: any, fullProxy?: boolean): undefined | any | [target: any, handler: any];
  previewEntries(object: object, slowPath?: boolean): undefined | any[] | [entries: any[], isKeyValue: boolean];
  getOwnNonIndexProperties(object: object, filter: number): Array<string | symbol>;
  getConstructorName(object: object): string;
  getExternalValue(value: any): bigint;
  sleep(msec: number): void;
  isConstructor(fn: Function): boolean;
  arrayBufferViewHasBuffer(view: ArrayBufferView): boolean;
  propertyFilter: {
    ALL_PROPERTIES: 0;
    ONLY_WRITABLE: 1;
    ONLY_ENUMERABLE: 2;
    ONLY_CONFIGURABLE: 4;
    SKIP_STRINGS: 8;
    SKIP_SYMBOLS: 16;
  };
  shouldAbortOnUncaughtToggle: [shouldAbort: 0 | 1];
  WeakReference: typeof InternalUtilBinding.WeakReference;
  guessHandleType(fd: number): 'TCP' | 'TTY' | 'UDP' | 'FILE' | 'PIPE' | 'UNKNOWN';
  parseEnv(content: string): Record<string, string>;
}
