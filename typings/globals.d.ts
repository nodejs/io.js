import { AsyncWrapBinding } from './internalBinding/async_wrap';
import { BlobBinding } from './internalBinding/blob';
import { ConfigBinding } from './internalBinding/config';
import { ConstantsBinding } from './internalBinding/constants';
import { HttpParserBinding } from './internalBinding/http_parser';
import { FsBinding } from './internalBinding/fs';
import { FsDirBinding } from './internalBinding/fs_dir';
import { MessagingBinding } from './internalBinding/messaging';
import { OptionsBinding } from './internalBinding/options';
import { OSBinding } from './internalBinding/os';
import { SerdesBinding } from './internalBinding/serdes';
import { SymbolsBinding } from './internalBinding/symbols';
import { TimersBinding } from './internalBinding/timers';
import { TypesBinding } from './internalBinding/types';
import { URLBinding } from './internalBinding/url';
import { UtilBinding } from './internalBinding/util';
import { WorkerBinding } from './internalBinding/worker';
import { ModulesBinding } from './internalBinding/modules';

declare type TypedArray =
  | Uint8Array
  | Uint8ClampedArray
  | Uint16Array
  | Uint32Array
  | Int8Array
  | Int16Array
  | Int32Array
  | Float32Array
  | Float64Array
  | BigUint64Array
  | BigInt64Array;

interface InternalBindingMap {
  async_wrap: AsyncWrapBinding;
  blob: BlobBinding;
  config: ConfigBinding;
  constants: ConstantsBinding;
  fs: FsBinding;
  fs_dir: FsDirBinding;
  http_parser: HttpParserBinding;
  messaging: MessagingBinding;
  modules: ModulesBinding;
  options: OptionsBinding;
  os: OSBinding;
  serdes: SerdesBinding;
  symbols: SymbolsBinding;
  timers: TimersBinding;
  types: TypesBinding;
  url: URLBinding;
  util: UtilBinding;
  worker: WorkerBinding;
}

type InternalBindingKeys = keyof InternalBindingMap;

declare function internalBinding<T extends InternalBindingKeys>(binding: T): InternalBindingMap[T]

declare global {
  namespace NodeJS {
    interface Global {
      internalBinding<T extends InternalBindingKeys>(binding: T): InternalBindingMap[T]
    }
  }
}
