'use strict';

const {
  Promise,
  RegExpPrototypeExec,
  StringPrototypeEndsWith,
  StringPrototypeIndexOf,
  StringPrototypeLastIndexOf,
  StringPrototypeSlice,
  StringPrototypeStartsWith,
  StringPrototypeToLocaleLowerCase,
} = primordials;

const { cwd } = process;
const console = require('internal/console/global');
const { createReadStream } = require('fs');
const { Server } = require('_http_server');
const { URL, isURL, pathToFileURL } = require('internal/url');
const { emitExperimentalWarning, kEmptyObject } = require('internal/util');
const { validateFunction, validatePort } = require('internal/validators');

const mimeDefault = {
  '__proto__': null,
  '.html': 'text/html; charset=UTF-8',
  '.js': 'text/javascript; charset=UTF-8',
  '.css': 'text/css; charset=UTF-8',
  '.avif': 'image/avif',
  '.svg': 'image/svg+xml',
  '.json': 'application/json',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.png': 'image/png',
  '.wasm': 'application/wasm',
  '.webp': 'image/webp',
  '.woff2': 'font/woff2',
};

const dot = /\/(\.|%2[eE])/;
function filterDotFiles(url, fileURL) {
  return RegExpPrototypeExec(dot, fileURL.pathname) === null;
}

function createStaticServer(options = kEmptyObject) {
  emitExperimentalWarning('http/static');

  const {
    directory = cwd(),
    port,
    host = 'localhost',
    mimeOverrides,
    filter = filterDotFiles,
    log = console.log,
    onStart = (host, port) => console.log(`Server started on http://${host}:${port}`),
  } = options;
  const mime = mimeOverrides ? {
    __proto__: null,
    ...mimeDefault,
    ...mimeOverrides,
  } : mimeDefault;

  const directoryURL = isURL(directory) ? directory : pathToFileURL(directory);
  // To be used as a base URL, it is necessary that the URL ends with a slash:
  const baseDirectoryURL = StringPrototypeEndsWith(directoryURL.pathname, '/') ?
    directoryURL : new URL(`${directoryURL}/`);

  if (port != null) validatePort(port);
  if (filter != null) validateFunction(filter, 'options.filter');

  const server = new Server(async (req, res) => {
    const url = new URL(
      req.url === '/' || StringPrototypeStartsWith(req.url, '/?') ?
        './index.html' :
        '.' + StringPrototypeSlice(`${new URL(req.url, 'root://')}`, 6),
      baseDirectoryURL,
    );

    if (filter != null && !filter(req.url, url)) {
      log?.(403, req.url);
      res.statusCode = 403;
      res.end('Forbidden\n');
      return;
    }

    const ext = StringPrototypeToLocaleLowerCase(
      StringPrototypeSlice(url.pathname, StringPrototypeLastIndexOf(url.pathname, '.')),
    );
    if (ext in mime) res.setHeader('Content-Type', mime[ext]);

    try {
      try {
        const stream = createReadStream(url, { emitClose: false });
        await new Promise((resolve, reject) => {
          stream.on('error', reject);
          stream.on('end', resolve);
          stream.pipe(res);
        });
        log?.(200, req.url);
      } catch (err) {
        if (err?.code === 'EISDIR') {
          if (StringPrototypeEndsWith(req.url, '/') || RegExpPrototypeExec(/^[^?]+\/\?/, req.url) !== null) {
            const stream = createReadStream(new URL('./index.html', url), {
              emitClose: false,
            });
            res.setHeader('Content-Type', mime['.html']);
            await new Promise((resolve, reject) => {
              stream.on('error', reject);
              stream.on('end', resolve);
              stream.pipe(res);
            });
            log?.(200, req.url);
          } else {
            log?.(307, req.url);
            res.statusCode = 307;
            const index = StringPrototypeIndexOf(req.url, '?');
            res.setHeader(
              'Location',
              index === -1 ?
                `${req.url}/` :
                `${StringPrototypeSlice(req.url, 0, index)}/${StringPrototypeSlice(req.url, index)}`,
            );
            res.end('Temporary Redirect\n');
          }
        } else {
          throw err;
        }
      }
    } catch (err) {
      if (err?.code === 'ENOENT') {
        log?.(404, req.url);
        res.statusCode = 404;
        res.end('Not Found\n');
      } else {
        log?.(500, req.url, err);
        res.statusCode = 500;
        res.end('Internal Error\n');
      }
    }
  });
  const callback = () => {
    const { address, family, port } = server.address();
    const host = family === 'IPv6' ? `[${address}]` : address;
    onStart?.(host, port);
  };
  if (host != null) {
    server.listen(port, host, callback);
  } else if (port != null) {
    server.listen(port, callback);
  } else {
    server.listen(callback);
  }

  return server;
}

module.exports = createStaticServer;
