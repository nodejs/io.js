import { ConstantsBinding } from './constants';

interface ReadFileContext {
  fd: number | undefined;
  isUserFd: boolean | undefined;
  size: number;
  callback: (err?: Error, data?: string | Uint8Array) => unknown;
  buffers: Uint8Array[];
  buffer: Uint8Array;
  pos: number;
  encoding: string;
  err: Error | null;
  signal: unknown /* AbortSignal | undefined */;
}

declare namespace InternalFSBinding {
  class FSReqCallback<ResultType = unknown> {
    constructor(bigint?: boolean);
    oncomplete: ((error: Error) => void) | ((error: null, result: ResultType) => void);
    context: ReadFileContext;
  }

  interface FSSyncContext {
    fd?: number;
    path?: string;
    dest?: string;
    errno?: string;
    message?: string;
    syscall?: string;
    error?: Error;
  }

  type Buffer = Uint8Array;
  type Stream = object;
  type StringOrBuffer = string | Buffer;

  const kUsePromises: unique symbol;

  class FileHandle {
    constructor(fd: number, offset: number, length: number);
    fd: number;
    getAsyncId(): number;
    close(): Promise<void>;
    onread: () => void;
    stream: Stream;
  }

  class StatWatcher {
    constructor(useBigint: boolean);
    initialized: boolean;
    start(path: string, interval: number): number;
    getAsyncId(): number;
    close(): void;
    ref(): void;
    unref(): void;
    onchange: (status: number, eventType: string, filename: string | Buffer) => void;
  }

  function access(path: StringOrBuffer, mode: number, req: FSReqCallback): void;
  function access(path: StringOrBuffer, mode: number): void;
  function access(path: StringOrBuffer, mode: number, usePromises: typeof kUsePromises): Promise<void>;

  function chmod(path: string, mode: number, req: FSReqCallback): void;
  function chmod(path: string, mode: number): void;
  function chmod(path: string, mode: number, usePromises: typeof kUsePromises): Promise<void>;

  function chown(path: string, uid: number, gid: number, req: FSReqCallback): void;
  function chown(path: string, uid: number, gid: number, req: undefined, ctx: FSSyncContext): void;
  function chown(path: string, uid: number, gid: number, usePromises: typeof kUsePromises): Promise<void>;
  function chown(path: string, uid: number, gid: number): void;

  function close(fd: number, req: FSReqCallback): void;
  function close(fd: number): void;

  function copyFile(src: StringOrBuffer, dest: StringOrBuffer, mode: number, req: FSReqCallback): void;
  function copyFile(src: StringOrBuffer, dest: StringOrBuffer, mode: number, req: undefined, ctx: FSSyncContext): void;
  function copyFile(src: StringOrBuffer, dest: StringOrBuffer, mode: number, usePromises: typeof kUsePromises): Promise<void>;

  function cpSyncCheckPaths(src: StringOrBuffer, dest: StringOrBuffer, dereference: boolean, recursive: boolean): void;

  function fchmod(fd: number, mode: number, req: FSReqCallback): void;
  function fchmod(fd: number, mode: number): void;
  function fchmod(fd: number, mode: number, usePromises: typeof kUsePromises): Promise<void>;

  function fchown(fd: number, uid: number, gid: number, req: FSReqCallback): void;
  function fchown(fd: number, uid: number, gid: number): void;
  function fchown(fd: number, uid: number, gid: number, usePromises: typeof kUsePromises): Promise<void>;

  function fdatasync(fd: number, req: FSReqCallback): void;
  function fdatasync(fd: number, req: undefined, ctx: FSSyncContext): void;
  function fdatasync(fd: number, usePromises: typeof kUsePromises): Promise<void>;
  function fdatasync(fd: number): void;

  function fstat(fd: number, useBigint: boolean, req: FSReqCallback<Float64Array | BigUint64Array>): void;
  function fstat(fd: number, useBigint: true, req: FSReqCallback<BigUint64Array>): void;
  function fstat(fd: number, useBigint: false, req: FSReqCallback<Float64Array>): void;
  function fstat(fd: number, useBigint: boolean, req: undefined, shouldNotThrow: boolean): Float64Array | BigUint64Array;
  function fstat(fd: number, useBigint: true, req: undefined, shouldNotThrow: boolean): BigUint64Array;
  function fstat(fd: number, useBigint: false, req: undefined, shouldNotThrow: boolean): Float64Array;
  function fstat(fd: number, useBigint: boolean, usePromises: typeof kUsePromises): Promise<Float64Array | BigUint64Array>;
  function fstat(fd: number, useBigint: true, usePromises: typeof kUsePromises): Promise<BigUint64Array>;
  function fstat(fd: number, useBigint: false, usePromises: typeof kUsePromises): Promise<Float64Array>;

  function fsync(fd: number, req: FSReqCallback): void;
  function fsync(fd: number, usePromises: typeof kUsePromises): Promise<void>;
  function fsync(fd: number): void;

  function ftruncate(fd: number, len: number, req: FSReqCallback): void;
  function ftruncate(fd: number, len: number, req: undefined, ctx: FSSyncContext): void;
  function ftruncate(fd: number, len: number, usePromises: typeof kUsePromises): Promise<void>;

  function futimes(fd: number, atime: number, mtime: number, req: FSReqCallback): void;
  function futimes(fd: number, atime: number, mtime: number): void;
  function futimes(fd: number, atime: number, mtime: number, usePromises: typeof kUsePromises): Promise<void>;

  function internalModuleStat(receiver: unknown, path: string): number;

  function lchown(path: string, uid: number, gid: number, req: FSReqCallback): void;
  function lchown(path: string, uid: number, gid: number, req: undefined, ctx: FSSyncContext): void;
  function lchown(path: string, uid: number, gid: number, usePromises: typeof kUsePromises): Promise<void>;
  function lchown(path: string, uid: number, gid: number): void;

  function link(existingPath: string, newPath: string, req: FSReqCallback): void;
  function link(existingPath: string, newPath: string, req: undefined, ctx: FSSyncContext): void;
  function link(existingPath: string, newPath: string, usePromises: typeof kUsePromises): Promise<void>;
  function link(existingPath: string, newPath: string): void;

  function lstat(path: StringOrBuffer, useBigint: boolean, req: FSReqCallback<Float64Array | BigUint64Array>): void;
  function lstat(path: StringOrBuffer, useBigint: true, req: FSReqCallback<BigUint64Array>): void;
  function lstat(path: StringOrBuffer, useBigint: false, req: FSReqCallback<Float64Array>): void;
  function lstat(path: StringOrBuffer, useBigint: boolean, req: undefined, throwIfNoEntry: boolean): Float64Array | BigUint64Array;
  function lstat(path: StringOrBuffer, useBigint: true, req: undefined, throwIfNoEntry: boolean): BigUint64Array;
  function lstat(path: StringOrBuffer, useBigint: false, req: undefined, throwIfNoEntry: boolean): Float64Array;
  function lstat(path: StringOrBuffer, useBigint: boolean, usePromises: typeof kUsePromises): Promise<Float64Array | BigUint64Array>;
  function lstat(path: StringOrBuffer, useBigint: true, usePromises: typeof kUsePromises): Promise<BigUint64Array>;
  function lstat(path: StringOrBuffer, useBigint: false, usePromises: typeof kUsePromises): Promise<Float64Array>;

  function lutimes(path: string, atime: number, mtime: number, req: FSReqCallback): void;
  function lutimes(path: string, atime: number, mtime: number): void;
  function lutimes(path: string, atime: number, mtime: number, usePromises: typeof kUsePromises): Promise<void>;

  function mkdtemp(prefix: string, encoding: unknown, req: FSReqCallback<string>): void;
  function mkdtemp(prefix: string, encoding: unknown, req: undefined, ctx: FSSyncContext): string;
  function mkdtemp(prefix: string, encoding: unknown, usePromises: typeof kUsePromises): Promise<string>;
  function mkdtemp(prefix: string, encoding: unknown): string;

  function mkdir(path: string, mode: number, recursive: boolean, req: FSReqCallback<void | string>): void;
  function mkdir(path: string, mode: number, recursive: true, req: FSReqCallback<string>): void;
  function mkdir(path: string, mode: number, recursive: false, req: FSReqCallback<void>): void;
  function mkdir(path: string, mode: number, recursive: boolean): void | string;
  function mkdir(path: string, mode: number, recursive: true): string;
  function mkdir(path: string, mode: number, recursive: false): void;
  function mkdir(path: string, mode: number, recursive: boolean, usePromises: typeof kUsePromises): Promise<void | string>;
  function mkdir(path: string, mode: number, recursive: true, usePromises: typeof kUsePromises): Promise<string>;
  function mkdir(path: string, mode: number, recursive: false, usePromises: typeof kUsePromises): Promise<void>;

  function open(path: StringOrBuffer, flags: number, mode: number, req: FSReqCallback<number>): void;
  function open(path: StringOrBuffer, flags: number, mode: number): number;

  function openFileHandle(path: StringOrBuffer, flags: number, mode: number, usePromises: typeof kUsePromises): Promise<FileHandle>;

  function read(fd: number, buffer: ArrayBufferView, offset: number, length: number, position: number, req: FSReqCallback<number>): void;
  function read(fd: number, buffer: ArrayBufferView, offset: number, length: number, position: number, usePromises: typeof kUsePromises): Promise<number>;
  function read(fd: number, buffer: ArrayBufferView, offset: number, length: number, position: number): number;

  function readBuffers(fd: number, buffers: ArrayBufferView[], position: number, req: FSReqCallback<number>): void;
  function readBuffers(fd: number, buffers: ArrayBufferView[], position: number, req: undefined, ctx: FSSyncContext): number;
  function readBuffers(fd: number, buffers: ArrayBufferView[], position: number, usePromises: typeof kUsePromises): Promise<number>;

  function readdir(path: StringOrBuffer, encoding: unknown, withFileTypes: boolean, req: FSReqCallback<string[] | [string[], number[]]>): void;
  function readdir(path: StringOrBuffer, encoding: unknown, withFileTypes: true, req: FSReqCallback<[string[], number[]]>): void;
  function readdir(path: StringOrBuffer, encoding: unknown, withFileTypes: false, req: FSReqCallback<string[]>): void;
  function readdir(path: StringOrBuffer, encoding: unknown, withFileTypes: boolean, req: undefined, ctx: FSSyncContext): string[] | [string[], number[]];
  function readdir(path: StringOrBuffer, encoding: unknown, withFileTypes: true): [string[], number[]];
  function readdir(path: StringOrBuffer, encoding: unknown, withFileTypes: false): string[];
  function readdir(path: StringOrBuffer, encoding: unknown, withFileTypes: boolean, usePromises: typeof kUsePromises): Promise<string[] | [string[], number[]]>;
  function readdir(path: StringOrBuffer, encoding: unknown, withFileTypes: true, usePromises: typeof kUsePromises): Promise<[string[], number[]]>;
  function readdir(path: StringOrBuffer, encoding: unknown, withFileTypes: false, usePromises: typeof kUsePromises): Promise<string[]>;

  function readFileUtf8(path: StringOrBuffer, flags: number): string;

  function readlink(path: StringOrBuffer, encoding: unknown, req: FSReqCallback<string | Buffer>): void;
  function readlink(path: StringOrBuffer, encoding: unknown, req: undefined, ctx: FSSyncContext): string | Buffer;
  function readlink(path: StringOrBuffer, encoding: unknown, usePromises: typeof kUsePromises): Promise<string | Buffer>;
  function readlink(path: StringOrBuffer, encoding: unknown): StringOrBuffer;

  function realpath(path: StringOrBuffer, encoding: unknown, req: FSReqCallback<string | Buffer>): void;
  function realpath(path: StringOrBuffer, encoding: unknown, req: undefined, ctx: FSSyncContext): string | Buffer;
  function realpath(path: StringOrBuffer, encoding: unknown, usePromises: typeof kUsePromises): Promise<string | Buffer>;
  function realpath(path: StringOrBuffer, encoding: unknown): StringOrBuffer;

  function rename(oldPath: string, newPath: string, req: FSReqCallback): void;
  function rename(oldPath: string, newPath: string, req: undefined, ctx: FSSyncContext): void;
  function rename(oldPath: string, newPath: string, usePromises: typeof kUsePromises): Promise<void>;
  function rename(oldPath: string, newPath: string): void;

  function rmdir(path: string, req: FSReqCallback): void;
  function rmdir(path: string): void;
  function rmdir(path: string, usePromises: typeof kUsePromises): Promise<void>;

  function rmSync(path: StringOrBuffer, maxRetries: number, recursive: boolean, retryDelay: number): void;

  function stat(path: StringOrBuffer, useBigint: boolean, req: FSReqCallback<Float64Array | BigUint64Array>): void;
  function stat(path: StringOrBuffer, useBigint: true, req: FSReqCallback<BigUint64Array>): void;
  function stat(path: StringOrBuffer, useBigint: false, req: FSReqCallback<Float64Array>): void;
  function stat(path: StringOrBuffer, useBigint: boolean, req: undefined, ctx: FSSyncContext): Float64Array | BigUint64Array;
  function stat(path: StringOrBuffer, useBigint: true, req: undefined, ctx: FSSyncContext): BigUint64Array;
  function stat(path: StringOrBuffer, useBigint: false, req: undefined, ctx: FSSyncContext): Float64Array;
  function stat(path: StringOrBuffer, useBigint: boolean, usePromises: typeof kUsePromises): Promise<Float64Array | BigUint64Array>;
  function stat(path: StringOrBuffer, useBigint: true, usePromises: typeof kUsePromises): Promise<BigUint64Array>;
  function stat(path: StringOrBuffer, useBigint: false, usePromises: typeof kUsePromises): Promise<Float64Array>;

  function symlink(target: StringOrBuffer, path: StringOrBuffer, type: number, req: FSReqCallback): void;
  function symlink(target: StringOrBuffer, path: StringOrBuffer, type: number, req: undefined, ctx: FSSyncContext): void;
  function symlink(target: StringOrBuffer, path: StringOrBuffer, type: number, usePromises: typeof kUsePromises): Promise<void>;
  function symlink(target: StringOrBuffer, path: StringOrBuffer, type: number): void;

  function unlink(path: string, req: FSReqCallback): void;
  function unlink(path: string): void;
  function unlink(path: string, usePromises: typeof kUsePromises): Promise<void>;

  function utimes(path: string, atime: number, mtime: number, req: FSReqCallback): void;
  function utimes(path: string, atime: number, mtime: number): void;
  function utimes(path: string, atime: number, mtime: number, usePromises: typeof kUsePromises): Promise<void>;

  function writeBuffer(fd: number, buffer: ArrayBufferView, offset: number, length: number, position: number | null, req: FSReqCallback<number>): void;
  function writeBuffer(fd: number, buffer: ArrayBufferView, offset: number, length: number, position: number | null, req: undefined, ctx: FSSyncContext): number;
  function writeBuffer(fd: number, buffer: ArrayBufferView, offset: number, length: number, position: number | null, usePromises: typeof kUsePromises): Promise<number>;

  function writeBuffers(fd: number, buffers: ArrayBufferView[], position: number, req: FSReqCallback<number>): void;
  function writeBuffers(fd: number, buffers: ArrayBufferView[], position: number, req: undefined, ctx: FSSyncContext): number;
  function writeBuffers(fd: number, buffers: ArrayBufferView[], position: number, usePromises: typeof kUsePromises): Promise<number>;

  function writeString(fd: number, value: string, pos: unknown, encoding: unknown, req: FSReqCallback<number>): void;
  function writeString(fd: number, value: string, pos: unknown, encoding: unknown, req: undefined, ctx: FSSyncContext): number;
  function writeString(fd: number, value: string, pos: unknown, encoding: unknown, usePromises: typeof kUsePromises): Promise<number>;

  function getFormatOfExtensionlessFile(url: string): ConstantsBinding['fs'];

  function writeFileUtf8(path: string, data: string, flag: number, mode: number): void;
  function writeFileUtf8(fd: number, data: string, flag: number, mode: number): void;
}

export interface FsBinding {
  FSReqCallback: typeof InternalFSBinding.FSReqCallback;

  FileHandle: typeof InternalFSBinding.FileHandle;

  kUsePromises: typeof InternalFSBinding.kUsePromises;

  statValues: Float64Array;
  bigintStatValues: BigUint64Array;

  kFsStatsFieldsNumber: number;
  StatWatcher: typeof InternalFSBinding.StatWatcher;

  access: typeof InternalFSBinding.access;
  chmod: typeof InternalFSBinding.chmod;
  chown: typeof InternalFSBinding.chown;
  close: typeof InternalFSBinding.close;
  copyFile: typeof InternalFSBinding.copyFile;
  cpSyncCheckPaths: typeof InternalFSBinding.cpSyncCheckPaths;
  fchmod: typeof InternalFSBinding.fchmod;
  fchown: typeof InternalFSBinding.fchown;
  fdatasync: typeof InternalFSBinding.fdatasync;
  fstat: typeof InternalFSBinding.fstat;
  fsync: typeof InternalFSBinding.fsync;
  ftruncate: typeof InternalFSBinding.ftruncate;
  futimes: typeof InternalFSBinding.futimes;
  internalModuleStat: typeof InternalFSBinding.internalModuleStat;
  lchown: typeof InternalFSBinding.lchown;
  link: typeof InternalFSBinding.link;
  lstat: typeof InternalFSBinding.lstat;
  lutimes: typeof InternalFSBinding.lutimes;
  mkdtemp: typeof InternalFSBinding.mkdtemp;
  mkdir: typeof InternalFSBinding.mkdir;
  open: typeof InternalFSBinding.open;
  openFileHandle: typeof InternalFSBinding.openFileHandle;
  read: typeof InternalFSBinding.read;
  readBuffers: typeof InternalFSBinding.readBuffers;
  readdir: typeof InternalFSBinding.readdir;
  readFileUtf8: typeof InternalFSBinding.readFileUtf8;
  readlink: typeof InternalFSBinding.readlink;
  realpath: typeof InternalFSBinding.realpath;
  rename: typeof InternalFSBinding.rename;
  rmdir: typeof InternalFSBinding.rmdir;
  rmSync: typeof InternalFSBinding.rmSync;
  stat: typeof InternalFSBinding.stat;
  symlink: typeof InternalFSBinding.symlink;
  unlink: typeof InternalFSBinding.unlink;
  utimes: typeof InternalFSBinding.utimes;
  writeBuffer: typeof InternalFSBinding.writeBuffer;
  writeBuffers: typeof InternalFSBinding.writeBuffers;
  writeFileUtf8: typeof InternalFSBinding.writeFileUtf8;
  writeString: typeof InternalFSBinding.writeString;

  getFormatOfExtensionlessFile: typeof InternalFSBinding.getFormatOfExtensionlessFile;
}
