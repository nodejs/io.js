let wasm;

const isLE = new Uint8Array(new Uint16Array([1]).buffer)[0] === 1;

export function parse (source, name = '@') {
  if (!wasm)
    throw new Error('Not initialized');

  const len = source.length + 1;

  // need 2 bytes per code point plus analysis space so we double again
  const extraMem = (wasm.__heap_base.value || wasm.__heap_base) + len * 4 - wasm.memory.buffer.byteLength;
  if (extraMem > 0)
    wasm.memory.grow(Math.ceil(extraMem / 65536));
    
  const addr = wasm.sa(len);
  (isLE ? copyLE : copyBE)(source, new Uint16Array(wasm.memory.buffer, addr, len));

  const err_code = wasm.parseCJS(addr, source.length, 0, 0, 0);

  if (err_code) {
    const err = new Error(`Parse error ${name}${wasm.e()}:${source.slice(0, wasm.e()).split('\n').length}:${wasm.e() - source.lastIndexOf('\n', wasm.e() - 1)}`);
    Object.assign(err, { idx: wasm.e() });
    if (err_code === 5 || err_code === 6 || err_code === 7)
      Object.assign(err, { code: 'ERR_LEXER_ESM_SYNTAX' });
    throw err;
  }

  let exports = new Set(), reexports = new Set(), unsafeGetters = new Set();
  
  while (wasm.rre()) {
    const reexptStr = decode(source.slice(wasm.res(), wasm.ree()));
    if (reexptStr)
      reexports.add(reexptStr);
  }
  while (wasm.ru())
    unsafeGetters.add(decode(source.slice(wasm.us(), wasm.ue())));
  while (wasm.re()) {
    let exptStr = decode(source.slice(wasm.es(), wasm.ee()));
    if (exptStr !== undefined && !unsafeGetters.has(exptStr))
      exports.add(exptStr);
  }

  return { exports: [...exports], reexports: [...reexports] };
}

function decode (str) {
  if (str[0] === '"' || str[0] === '\'') {
    try {
      const decoded = (0, eval)(str);
      // Filter to exclude non-matching UTF-16 surrogate strings
      for (let i = 0; i < decoded.length; i++) {
        const surrogatePrefix = decoded.charCodeAt(i) & 0xFC00;
        if (surrogatePrefix < 0xD800) {
          // Not a surrogate
          continue;
        }
        else if (surrogatePrefix === 0xD800) {
          // Validate surrogate pair
          if ((decoded.charCodeAt(++i) & 0xFC00) !== 0xDC00)
            return;
        }
        else {
          // Out-of-range surrogate code (above 0xD800)
          return;
        }
      }
      return decoded;
    }
    catch {}
  }
  else {
    return str;
  }
}

function copyBE (src, outBuf16) {
  const len = src.length;
  let i = 0;
  while (i < len) {
    const ch = src.charCodeAt(i);
    outBuf16[i++] = (ch & 0xff) << 8 | ch >>> 8;
  }
}

function copyLE (src, outBuf16) {
  const len = src.length;
  let i = 0;
  while (i < len)
    outBuf16[i] = src.charCodeAt(i++);
}

function getWasmBytes() {
  const binary = 'WASM_BINARY';  // This string will be replaced by build.js.
  if (typeof Buffer !== 'undefined')
    return Buffer.from(binary, 'base64');
  return Uint8Array.from(atob(binary), x => x.charCodeAt(0));
}

let initPromise;
export function init () {
  if (initPromise)
    return initPromise;
  return initPromise = (async () => {
    const compiled = await WebAssembly.compile(getWasmBytes());
    const { exports } = await WebAssembly.instantiate(compiled);
    wasm = exports;
  })();
}

export function initSync () {
  if (wasm) {
    return;
  }
  const compiled = new WebAssembly.Module(getWasmBytes());
  const { exports } = new WebAssembly.Instance(compiled);
  wasm = exports;
  return;
}
