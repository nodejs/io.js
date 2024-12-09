declare namespace InternalBlobBinding {
  interface BlobHandle {
    slice(start: number, end: number): BlobHandle;
  }

  class FixedSizeBlobCopyJob {
    constructor(handle: BlobHandle);
    run(): ArrayBuffer | undefined;
    ondone: (err: unknown, res?: ArrayBuffer) => void;
  }
}

export interface BlobBinding {
  createBlob(
    sources: Array<Uint8Array | InternalBlobBinding.BlobHandle>, length: number
  ): InternalBlobBinding.BlobHandle;
  FixedSizeBlobCopyJob: typeof InternalBlobBinding.FixedSizeBlobCopyJob;
  getDataObject(
    id: string
  ): [handle: InternalBlobBinding.BlobHandle | undefined, length: number, type: string] | undefined;
  storeDataObject(id: string, handle: InternalBlobBinding.BlobHandle, size: number, type: string): void;
  revokeDataObject(id: string): void;
}
