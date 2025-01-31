'use strict';

const ts = require('../snapshot/typescript');
const extensions = {
  '.cts': 'commonjs-typescript',
  '.mts': 'module-typescript',
  '.ts': 'typescript',
};

const output = {
  'commonjs-typescript': {
    options: { module: ts.ModuleKind.CommonJS },
    format: 'commonjs',
  },
  'module-typescript': {
    options: { module: ts.ModuleKind.ESNext },
    format: 'module',
  },
  'typescript': {
    options: { module: ts.ModuleKind.NodeNext },
    format: 'commonjs',
  },
};

function resolve(specifier, context, nextResolve) {
  const resolved = nextResolve(specifier, context);
  const index = resolved.url.lastIndexOf('.');
  if (index === -1) {
    return resolved;
  }
  const ext = resolved.url.slice(index);
  const supportedFormat = extensions[ext];
  if (!supportedFormat) {
    return resolved;
  }
  const result = {
    ...resolved,
    format: supportedFormat,
  };
  return result;
}

let decoder;
function load(url, context, nextLoad) {
  const loadResult = nextLoad(url, context);
  const { source, format } = loadResult;

  if (!format || !format.includes('typescript')) {
    return { format, source };
  }

  let str = source;
  if (typeof str !== 'string') {
    decoder ??= new TextDecoder();
    str = decoder.decode(source);
  }
  const transpiled = ts.transpileModule(str, {
    compilerOptions: output[format].options
  });

  const result = {
    ...loadResult,
    format: output[format].format,
    source: transpiled.outputText,
  };

  return result;
}

exports.load = load;
exports.resolve = resolve;
