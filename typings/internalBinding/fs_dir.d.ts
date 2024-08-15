import { InternalFSBinding } from './fs';

declare namespace InternalFSDirBinding {
  import FSReqCallback = InternalFSBinding.FSReqCallback;
  type Buffer = Uint8Array;
  type StringOrBuffer = string | Buffer;

  class DirHandle {
    read(encoding: string, bufferSize: number, callback: FSReqCallback): string[] | undefined;
    read(encoding: string, bufferSize: number): string[] | undefined;
    close(callback: FSReqCallback): void;
    close(): void;
  }

  function opendir(path: StringOrBuffer, encoding: string, req: FSReqCallback): DirHandle;
  function opendir(path: StringOrBuffer, encoding: string): DirHandle;
  function opendirSync(path: StringOrBuffer): DirHandle;
}

export interface FsDirBinding {
  opendir: typeof InternalFSDirBinding.opendir;
  opendirSync: typeof InternalFSDirBinding.opendirSync;
}
