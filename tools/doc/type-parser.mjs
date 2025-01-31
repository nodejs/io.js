const jsDocPrefix = 'https://developer.mozilla.org/en-US/docs/Web/JavaScript/';

const jsDataStructuresUrl = `${jsDocPrefix}Data_structures`;
const jsPrimitives = {
  boolean: 'Boolean',
  integer: 'Number', // Not a primitive, used for clarification.
  null: 'Null',
  number: 'Number',
  string: 'String',
  symbol: 'Symbol',
  undefined: 'Undefined',
};

const jsGlobalObjectsUrl = `${jsDocPrefix}Reference/Global_Objects/`;
const jsGlobalTypes = [
  'AggregateError', 'Array', 'ArrayBuffer', 'DataView', 'Date', 'Error',
  'EvalError', 'Function', 'Map', 'Object', 'Promise', 'Proxy', 'RangeError',
  'ReferenceError', 'RegExp', 'Set', 'SharedArrayBuffer', 'SyntaxError',
  'TypeError', 'TypedArray', 'URIError', 'Uint8Array',
];

const customTypesMap = {
  'any': `${jsDataStructuresUrl}#Data_types`,

  'this': `${jsDocPrefix}Reference/Operators/this`,

  'AbortController': 'globals.html#class-abortcontroller',
  'AbortSignal': 'globals.html#class-abortsignal',

  'ArrayBufferView':
    'https://developer.mozilla.org/en-US/docs/Web/API/ArrayBufferView',

  'AsyncIterator': 'https://tc39.github.io/ecma262/#sec-asynciterator-interface',

  'AsyncIterable': 'https://tc39.github.io/ecma262/#sec-asynciterable-interface',

  'AsyncFunction': 'https://tc39.es/ecma262/#sec-async-function-constructor',

  'AsyncGeneratorFunction': 'https://tc39.es/proposal-async-iteration/#sec-asyncgeneratorfunction-constructor',

  'bigint': `${jsDocPrefix}Reference/Global_Objects/BigInt`,
  'WebAssembly.Instance':
    `${jsDocPrefix}Reference/Global_Objects/WebAssembly/Instance`,

  'Blob': 'buffer.html#class-blob',
  'File': 'buffer.html#class-file',

  'BroadcastChannel':
    'worker_threads.html#class-broadcastchannel-' +
    'extends-eventtarget',

  'Iterable':
    `${jsDocPrefix}Reference/Iteration_protocols#The_iterable_protocol`,
  'Iterator':
    `${jsDocPrefix}Reference/Iteration_protocols#The_iterator_protocol`,

  'Module Namespace Object':
    'https://tc39.github.io/ecma262/#sec-module-namespace-exotic-objects',

  'AsyncLocalStorage': 'async_context.html#class-asynclocalstorage',

  'AsyncHook': 'async_hooks.html#async_hookscreatehookcallbacks',
  'AsyncResource': 'async_hooks.html#class-asyncresource',

  'brotli options': 'zlib.html#class-brotlioptions',

  'Buffer': 'buffer.html#class-buffer',

  'ChildProcess': 'child_process.html#class-childprocess',

  'cluster.Worker': 'cluster.html#class-worker',

  'Cipher': 'crypto.html#class-cipher',
  'Decipher': 'crypto.html#class-decipher',
  'DiffieHellman': 'crypto.html#class-diffiehellman',
  'DiffieHellmanGroup': 'crypto.html#class-diffiehellmangroup',
  'ECDH': 'crypto.html#class-ecdh',
  'Hash': 'crypto.html#class-hash',
  'Hmac': 'crypto.html#class-hmac',
  'KeyObject': 'crypto.html#class-keyobject',
  'Sign': 'crypto.html#class-sign',
  'Verify': 'crypto.html#class-verify',
  'crypto.constants': 'crypto.html#cryptoconstants',

  'CryptoKey': 'webcrypto.html#class-cryptokey',
  'CryptoKeyPair': 'webcrypto.html#class-cryptokeypair',
  'Crypto': 'webcrypto.html#class-crypto',
  'SubtleCrypto': 'webcrypto.html#class-subtlecrypto',
  'RsaOaepParams': 'webcrypto.html#class-rsaoaepparams',
  'AlgorithmIdentifier': 'webcrypto.html#class-algorithmidentifier',
  'AesCtrParams': 'webcrypto.html#class-aesctrparams',
  'AesCbcParams': 'webcrypto.html#class-aescbcparams',
  'AesGcmParams': 'webcrypto.html#class-aesgcmparams',
  'EcdhKeyDeriveParams': 'webcrypto.html#class-ecdhkeyderiveparams',
  'HkdfParams': 'webcrypto.html#class-hkdfparams',
  'Pbkdf2Params': 'webcrypto.html#class-pbkdf2params',
  'HmacKeyGenParams': 'webcrypto.html#class-hmackeygenparams',
  'AesKeyGenParams': 'webcrypto.html#class-aeskeygenparams',
  'RsaHashedKeyGenParams':
    'webcrypto.html#class-rsahashedkeygenparams',
  'EcKeyGenParams': 'webcrypto.html#class-eckeygenparams',
  'RsaHashedImportParams':
    'webcrypto.html#class-rsahashedimportparams',
  'EcKeyImportParams': 'webcrypto.html#class-eckeyimportparams',
  'HmacImportParams': 'webcrypto.html#class-hmacimportparams',
  'EcdsaParams': 'webcrypto.html#class-ecdsaparams',
  'RsaPssParams': 'webcrypto.html#class-rsapssparams',
  'Ed448Params': 'webcrypto.html#class-ed448params',

  'dgram.Socket': 'dgram.html#class-dgramsocket',

  'Channel': 'diagnostics_channel.html#class-channel',
  'TracingChannel': 'diagnostics_channel.html#class-tracingchannel',

  'Domain': 'domain.html#class-domain',

  'errors.Error': 'errors.html#class-error',

  'import.meta': 'esm.html#importmeta',

  'EventEmitter': 'events.html#class-eventemitter',
  'EventTarget': 'events.html#class-eventtarget',
  'Event': 'events.html#class-event',
  'CustomEvent': 'events.html#class-customevent',
  'EventListener': 'events.html#event-listener',

  'FileHandle': 'fs.html#class-filehandle',
  'fs.Dir': 'fs.html#class-fsdir',
  'fs.Dirent': 'fs.html#class-fsdirent',
  'fs.FSWatcher': 'fs.html#class-fsfswatcher',
  'fs.ReadStream': 'fs.html#class-fsreadstream',
  'fs.Stats': 'fs.html#class-fsstats',
  'fs.StatFs': 'fs.html#class-fsstatfs',
  'fs.StatWatcher': 'fs.html#class-fsstatwatcher',
  'fs.WriteStream': 'fs.html#class-fswritestream',

  'http.Agent': 'http.html#class-httpagent',
  'http.ClientRequest': 'http.html#class-httpclientrequest',
  'http.IncomingMessage': 'http.html#class-httpincomingmessage',
  'http.OutgoingMessage': 'http.html#class-httpoutgoingmessage',
  'http.Server': 'http.html#class-httpserver',
  'http.ServerResponse': 'http.html#class-httpserverresponse',

  'ClientHttp2Session': 'http2.html#class-clienthttp2session',
  'ClientHttp2Stream': 'http2.html#class-clienthttp2stream',
  'HTTP/2 Headers Object': 'http2.html#headers-object',
  'HTTP/2 Settings Object': 'http2.html#settings-object',
  'http2.Http2ServerRequest': 'http2.html#class-http2http2serverrequest',
  'http2.Http2ServerResponse':
    'http2.html#class-http2http2serverresponse',
  'Http2SecureServer': 'http2.html#class-http2secureserver',
  'Http2Server': 'http2.html#class-http2server',
  'Http2Session': 'http2.html#class-http2session',
  'Http2Stream': 'http2.html#class-http2stream',
  'ServerHttp2Stream': 'http2.html#class-serverhttp2stream',
  'ServerHttp2Session': 'http2.html#class-serverhttp2session',

  'https.Server': 'https.html#class-httpsserver',

  'module': 'modules.html#the-module-object',

  'module.SourceMap':
    'module.html#class-modulesourcemap',

  'MockModuleContext': 'test.html#class-mockmodulecontext',

  'require': 'modules.html#requireid',

  'Handle': 'net.html#serverlistenhandle-backlog-callback',
  'net.BlockList': 'net.html#class-netblocklist',
  'net.Server': 'net.html#class-netserver',
  'net.Socket': 'net.html#class-netsocket',
  'net.SocketAddress': 'net.html#class-netsocketaddress',

  'NodeEventTarget':
    'events.html#class-nodeeventtarget',

  'os.constants.dlopen': 'os.html#dlopen-constants',

  'Histogram': 'perf_hooks.html#class-histogram',
  'IntervalHistogram':
     'perf_hooks.html#class-intervalhistogram-extends-histogram',
  'RecordableHistogram':
     'perf_hooks.html#class-recordablehistogram-extends-histogram',
  'PerformanceEntry': 'perf_hooks.html#class-performanceentry',
  'PerformanceNodeTiming':
    'perf_hooks.html#class-performancenodetiming',
  'PerformanceObserver':
    'perf_hooks.html#class-performanceobserver',
  'PerformanceObserverEntryList':
    'perf_hooks.html#class-performanceobserverentrylist',

  'readline.Interface':
    'readline.html#class-readlineinterface',
  'readline.InterfaceConstructor':
    'readline.html#class-interfaceconstructor',
  'readlinePromises.Interface':
    'readline.html#class-readlinepromisesinterface',

  'repl.REPLServer': 'repl.html#class-replserver',

  'Session': 'sqlite.html#class-session',
  'StatementSync': 'sqlite.html#class-statementsync',

  'Stream': 'stream.html#stream',
  'stream.Duplex': 'stream.html#class-streamduplex',
  'Duplex': 'stream.html#class-streamduplex',
  'stream.Readable': 'stream.html#class-streamreadable',
  'Readable': 'stream.html#class-streamreadable',
  'stream.Transform': 'stream.html#class-streamtransform',
  'Transform': 'stream.html#class-streamtransform',
  'stream.Writable': 'stream.html#class-streamwritable',
  'Writable': 'stream.html#class-streamwritable',

  'Immediate': 'timers.html#class-immediate',
  'Timeout': 'timers.html#class-timeout',
  'Timer': 'timers.html#timers',

  'TestsStream': 'test.html#class-testsstream',

  'tls.SecureContext': 'tls.html#tlscreatesecurecontextoptions',
  'tls.Server': 'tls.html#class-tlsserver',
  'tls.TLSSocket': 'tls.html#class-tlstlssocket',

  'Tracing': 'tracing.html#tracing-object',

  'tty.ReadStream': 'tty.html#class-ttyreadstream',
  'tty.WriteStream': 'tty.html#class-ttywritestream',

  'URL': 'url.html#the-whatwg-url-api',
  'URLSearchParams': 'url.html#class-urlsearchparams',

  'MIMEParams': 'util.html#class-utilmimeparams',

  'vm.Module': 'vm.html#class-vmmodule',
  'vm.Script': 'vm.html#class-vmscript',
  'vm.SourceTextModule': 'vm.html#class-vmsourcetextmodule',
  'vm.constants.USE_MAIN_CONTEXT_DEFAULT_LOADER':
      'vm.html#vmconstantsuse_main_context_default_loader',
  'vm.constants.DONT_CONTEXTIFY':
      'vm.html#vmconstantsdont_contextify',

  'MessagePort': 'worker_threads.html#class-messageport',
  'Worker': 'worker_threads.html#class-worker',

  'X509Certificate': 'crypto.html#class-x509certificate',

  'zlib options': 'zlib.html#class-options',

  'ReadableStream':
    'webstreams.html#class-readablestream',
  'ReadableStreamDefaultReader':
    'webstreams.html#class-readablestreamdefaultreader',
  'ReadableStreamBYOBReader':
    'webstreams.html#class-readablestreambyobreader',
  'ReadableStreamDefaultController':
    'webstreams.html#class-readablestreamdefaultcontroller',
  'ReadableByteStreamController':
    'webstreams.html#class-readablebytestreamcontroller',
  'ReadableStreamBYOBRequest':
    'webstreams.html#class-readablestreambyobrequest',
  'WritableStream':
    'webstreams.html#class-writablestream',
  'WritableStreamDefaultWriter':
    'webstreams.html#class-writablestreamdefaultwriter',
  'WritableStreamDefaultController':
    'webstreams.html#class-writablestreamdefaultcontroller',
  'TransformStream':
    'webstreams.html#class-transformstream',
  'TransformStreamDefaultController':
    'webstreams.html#class-transformstreamdefaultcontroller',
  'ByteLengthQueuingStrategy':
    'webstreams.html#class-bytelengthqueuingstrategy',
  'CountQueuingStrategy':
    'webstreams.html#class-countqueuingstrategy',
  'TextEncoderStream':
    'webstreams.html#class-textencoderstream',
  'TextDecoderStream':
    'webstreams.html#class-textdecoderstream',

  'FormData': 'https://developer.mozilla.org/en-US/docs/Web/API/FormData',
  'Headers': 'https://developer.mozilla.org/en-US/docs/Web/API/Headers',
  'Response': 'https://developer.mozilla.org/en-US/docs/Web/API/Response',
  'Request': 'https://developer.mozilla.org/en-US/docs/Web/API/Request',
  'Disposable': 'https://tc39.es/proposal-explicit-resource-management/#sec-disposable-interface',

  'quic.QuicEndpoint': 'quic.html#class-quicendpoint',
  'quic.QuicEndpoint.Stats': 'quic.html#class-quicendpointstats',
  'quic.QuicSession': 'quic.html#class-quicsession',
  'quic.QuicSession.Stats': 'quic.html#class-quicsessionstats',
  'quic.QuicStream': 'quic.html#class-quicstream',
  'quic.QuicStream.Stats': 'quic.html#class-quicstreamstats',
  'quic.EndpointOptions': 'quic.html#type-endpointoptions',
  'quic.SessionOptions': 'quic.html#type-sessionoptions',
  'quic.ApplicationOptions': 'quic.html#type-applicationoptions',
  'quic.TlsOptions': 'quic.html#type-tlsoptions',
  'quic.TransportParams': 'quic.html#type-transportparams',
  'quic.OnSessionCallback': 'quic.html#callback-onsessioncallback',
  'quic.OnStreamCallback': 'quic.html#callback-onstreamcallback',
  'quic.OnDatagramCallback': 'quic.html#callback-ondatagramcallback',
  'quic.OnDatagramStatusCallback': 'quic.html#callback-ondatagramstatuscallback',
  'quic.OnPathValidationCallback': 'quic.html#callback-onpathvalidationcallback',
  'quic.OnSessionTicketCallback': 'quic.html#callback-onsessionticketcallback',
  'quic.OnVersionNegotiationCallback': 'quic.html#callback-onversionnegotiationcallback',
  'quic.OnHandshakeCallback': 'quic.html#callback-onhandshakecallback',
  'quic.OnBlockedCallback': 'quic.html#callback-onblockedcallback',
  'quic.OnStreamErrorCallback': 'quic.html#callback-onstreamerrorcallback',
  'quic.OnHeadersCallback': 'quic.html#callback-onheaderscallback',
  'quic.OnTrailersCallback': 'quic.html#callback-ontrailerscallback',
  'quic.OnPullCallback': 'quic.html#callback-onpullcallback',
};

const arrayPart = /(?:\[])+$/;

export function toLink(typeInput) {
  const typeLinks = [];
  typeInput = typeInput.replace('{', '').replace('}', '');
  const typeTexts = typeInput.split('|');

  typeTexts.forEach((typeText) => {
    typeText = typeText.trim();
    if (typeText) {
      let typeUrl;

      // To support type[], type[][] etc., we store the full string
      // and use the bracket-less version to lookup the type URL.
      const typeTextFull = typeText;
      typeText = typeText.replace(arrayPart, '');

      const primitive = jsPrimitives[typeText];

      if (primitive !== undefined) {
        typeUrl = `${jsDataStructuresUrl}#${primitive}_type`;
      } else if (jsGlobalTypes.includes(typeText)) {
        typeUrl = `${jsGlobalObjectsUrl}${typeText}`;
      } else {
        typeUrl = customTypesMap[typeText];
      }

      if (typeUrl) {
        typeLinks.push(
          `<a href="${typeUrl}" class="type">&lt;${typeTextFull}&gt;</a>`);
      } else {
        throw new Error(
          `Unrecognized type: '${typeTextFull}' in '${import.meta.url}'.\n` +
          'Valid types can be found at https://github.com/nodejs/node/blob/HEAD/tools/doc/type-parser.mjs',
        );
      }
    } else {
      throw new Error(`Empty type slot: ${typeInput}`);
    }
  });

  return typeLinks.length ? typeLinks.join(' | ') : typeInput;
}
