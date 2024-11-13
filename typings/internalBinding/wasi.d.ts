declare namespace InternalWASIBinding {
  type EnvStr = `${string}=${string}`

  class WASI {
    constructor(args: string[], env: EnvStr[], preopens: string[], stdio: [stdin: number, stdout: number, stderr: number])

    _setMemory(memory: WebAssembly.Memory): void;
  }
}

export interface WASIBinding {
  WASI: typeof InternalWASIBinding.WASI;
}

