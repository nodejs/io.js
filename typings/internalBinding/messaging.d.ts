declare namespace InternalMessagingBinding {
  class MessageChannel {
    port1: MessagePort;
    port2: MessagePort;
  }

  class MessagePort {
    private constructor();
    postMessage(message: any, transfer?: any[] | null): void;
    start(): void;
    close(): void;
    ref(): void;
    unref(): void;
  }

  class JSTransferable {}
}


export interface MessagingBinding {
  DOMException: typeof import('internal/per_context/domexception').DOMException;
  MessageChannel: typeof InternalMessagingBinding.MessageChannel;
  MessagePort: typeof InternalMessagingBinding.MessagePort;
  JSTransferable: typeof InternalMessagingBinding.JSTransferable;
  stopMessagePort(port: typeof InternalMessagingBinding.MessagePort): void;
  checkMessagePort(port: unknown): boolean;
  drainMessagePort(port: typeof InternalMessagingBinding.MessagePort): void;
  receiveMessageOnPort(port: typeof InternalMessagingBinding.MessagePort): any;
  moveMessagePortToContext(port: typeof InternalMessagingBinding.MessagePort,
                           context: any): typeof InternalMessagingBinding.MessagePort;
  setDeserializerCreateObjectFunction(func: (deserializeInfo: string) => any): void;
  broadcastChannel(name: string): typeof InternalMessagingBinding.MessagePort;
}
