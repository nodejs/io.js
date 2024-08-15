'use strict';
const {
  JSONStringify,
  SafeMap,
  globalThis: {
    Atomics: {
      notify: AtomicsNotify,
      store: AtomicsStore,
    },
  },
} = primordials;
const {
  ensureNodeScheme,
  kBadExportsMessage,
  kMockSearchParam,
  kMockSuccess,
  kMockExists,
  kMockUnknownMessage,
} = require('internal/test_runner/mock/mock');
const { pathToFileURL, URL } = require('internal/url');
const { normalizeReferrerURL } = require('internal/modules/helpers');
let debug = require('internal/util/debuglog').debuglog('test_runner', (fn) => {
  debug = fn;
});
const { createRequire, isBuiltin } = require('module');
const { defaultGetFormatWithoutErrors } = require('internal/modules/esm/get_format');
const { defaultResolve } = require('internal/modules/esm/resolve');

// TODO(cjihrig): The mocks need to be thread aware because the exports are
// evaluated on the thread that creates the mock. Before marking this API as
// stable, one of the following issues needs to be implemented:
// https://github.com/nodejs/node/issues/49472
// or https://github.com/nodejs/node/issues/52219

const mocks = new SafeMap();

async function initialize(data) {
  data?.port.on('message', ({ type, payload }) => {
    debug('mock loader received message type "%s" with payload %o', type, payload);

    if (type === 'node:test:register') {
      const { baseURL } = payload;
      const mock = mocks.get(baseURL);

      if (mock?.active) {
        debug('already mocking "%s"', baseURL);
        sendAck(payload.ack, kMockExists);
        return;
      }

      const localVersion = mock?.localVersion ?? 0;

      debug('new mock version %d for "%s"', localVersion, baseURL);
      mocks.set(baseURL, {
        __proto__: null,
        active: true,
        cache: payload.cache,
        exportNames: payload.exportNames,
        format: payload.format,
        hasDefaultExport: payload.hasDefaultExport,
        localVersion,
        url: baseURL,
      });
      sendAck(payload.ack);
    } else if (type === 'node:test:unregister') {
      const mock = mocks.get(payload.baseURL);

      if (mock !== undefined) {
        mock.active = false;
        mock.localVersion++;
      }

      sendAck(payload.ack);
    } else {
      sendAck(payload.ack, kMockUnknownMessage);
    }
  });
}

async function resolve(specifier, context, nextResolve) {
  debug('resolve hook entry, specifier = "%s", context = %o', specifier, context);
  let mockSpecifier;

  if (isBuiltin(specifier)) {
    mockSpecifier = ensureNodeScheme(specifier);
  } else {
    let format;

    if (context.parentURL) {
      format = defaultGetFormatWithoutErrors(pathToFileURL(context.parentURL));
    }

    try {
      if (format === 'module') {
        specifier = defaultResolve(specifier, context).url;
      } else {
        specifier = pathToFileURL(
          createRequire(context.parentURL).resolve(specifier),
        ).href;
      }
    } catch {
      const parentURL = normalizeReferrerURL(context.parentURL);
      const parsedURL = URL.parse(specifier, parentURL)?.href;

      if (parsedURL) {
        specifier = parsedURL;
      }
    }

    mockSpecifier = specifier;
  }

  const mock = mocks.get(mockSpecifier);
  debug('resolve hook, specifier = "%s", mock = %o', specifier, mock);

  if (mock?.active !== true) {
    return nextResolve(specifier, context);
  }

  const url = new URL(mockSpecifier);

  url.searchParams.set(kMockSearchParam, mock.localVersion);

  if (!mock.cache) {
    // With ESM, we can't remove modules from the cache. Bump the module's
    // version instead so that the next import will be uncached.
    mock.localVersion++;
  }

  debug('resolve hook finished, url = "%s"', url.href);
  return nextResolve(url.href, context);
}

async function load(url, context, nextLoad) {
  debug('load hook entry, url = "%s", context = %o', url, context);
  const parsedURL = URL.parse(url);
  if (parsedURL) {
    parsedURL.searchParams.delete(kMockSearchParam);
  }

  const baseURL = parsedURL ? parsedURL.href : url;
  const mock = mocks.get(baseURL);

  const original = await nextLoad(url, context);
  debug('load hook, mock = %o', mock);
  if (mock?.active !== true) {
    return original;
  }

  // Treat builtins as commonjs because customization hooks do not allow a
  // core module to be replaced.
  // Also collapse 'commonjs-sync' and 'require-commonjs' to 'commonjs'.
  const format = (
    original.format === 'builtin' ||
    original.format === 'commonjs-sync' ||
    original.format === 'require-commonjs') ? 'commonjs' : original.format;

  const result = {
    __proto__: null,
    format,
    shortCircuit: true,
    source: await createSourceFromMock(mock, format),
  };

  debug('load hook finished, result = %o', result);
  return result;
}

async function createSourceFromMock(mock, format) {
  // Create mock implementation from provided exports.
  const { exportNames, hasDefaultExport, url } = mock;
  const useESM = format === 'module';
  const source = `${testImportSource(useESM)}
if (!$__test.mock._mockExports.has('${url}')) {
  throw new Error(${JSONStringify(`mock exports not found for "${url}"`)});
}

const $__exports = $__test.mock._mockExports.get(${JSONStringify(url)});
${defaultExportSource(useESM, hasDefaultExport)}
${namedExportsSource(useESM, exportNames)}
`;

  return source;
}

function testImportSource(useESM) {
  if (useESM) {
    return "import $__test from 'node:test';";
  }

  return "const $__test = require('node:test');";
}

function defaultExportSource(useESM, hasDefaultExport) {
  if (!hasDefaultExport) {
    return '';
  } else if (useESM) {
    return 'export default $__exports.defaultExport;';
  }

  return 'module.exports = $__exports.defaultExport;';
}

function namedExportsSource(useESM, exportNames) {
  let source = '';

  if (!useESM && exportNames.length > 0) {
    source += `
if (module.exports === null || typeof module.exports !== 'object') {
  throw new Error('${JSONStringify(kBadExportsMessage)}');
}
`;
  }

  for (let i = 0; i < exportNames.length; ++i) {
    const name = exportNames[i];

    if (useESM) {
      source += `export let ${name} = $__exports.namedExports[${JSONStringify(name)}];\n`;
    } else {
      source += `module.exports[${JSONStringify(name)}] = $__exports.namedExports[${JSONStringify(name)}];\n`;
    }
  }

  return source;
}

function sendAck(buf, status = kMockSuccess) {
  AtomicsStore(buf, 0, status);
  AtomicsNotify(buf, 0);
}

module.exports = { initialize, load, resolve };
