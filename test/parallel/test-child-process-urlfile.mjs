import { isWindows, mustCall, mustSucceed, mustNotCall } from '../common/index.mjs';
import { strictEqual, throws } from 'node:assert';
import cp from 'node:child_process';
import url from 'node:url';
import tmpdir from '../common/tmpdir.js';

tmpdir.refresh();

const cwd = tmpdir.path;
const cwdUrl = url.pathToFileURL(cwd);
const whichCommand = isWindows ? 'where.exe cmd.exe' : 'which pwd';
const pwdFullPath = `${cp.execSync(whichCommand)}`.trim();
const pwdWHATWGUrl = url.pathToFileURL(pwdFullPath);

// Test for WHATWG URL instance, legacy URL, and URL-like object
for (const pwdUrl of [
  pwdWHATWGUrl,
  new url.URL(pwdWHATWGUrl),
  {
    href: pwdWHATWGUrl.href,
    origin: pwdWHATWGUrl.origin,
    protocol: pwdWHATWGUrl.protocol,
    username: pwdWHATWGUrl.username,
    password: pwdWHATWGUrl.password,
    host: pwdWHATWGUrl.host,
    hostname: pwdWHATWGUrl.hostname,
    port: pwdWHATWGUrl.port,
    pathname: pwdWHATWGUrl.pathname,
    search: pwdWHATWGUrl.search,
    searchParams: new URLSearchParams(pwdWHATWGUrl.searchParams),
    hash: pwdWHATWGUrl.hash,
  },
]) {
  const pwdCommandAndOptions = isWindows ?
    [pwdUrl, ['/d', '/c', 'cd'], { cwd: cwdUrl }] :
    [pwdUrl, [], { cwd: cwdUrl }];

  {
    cp.execFile(...pwdCommandAndOptions, mustSucceed((stdout) => {
      strictEqual(`${stdout}`.trim(), cwd);
    }));
  }

  {
    const stdout = cp.execFileSync(...pwdCommandAndOptions);
    strictEqual(`${stdout}`.trim(), cwd);
  }

  {
    const pwd = cp.spawn(...pwdCommandAndOptions);
    pwd.stdout.on('data', mustCall((stdout) => {
      strictEqual(`${stdout}`.trim(), cwd);
    }));
    pwd.stderr.on('data', mustNotCall());
    pwd.on('close', mustCall((code) => {
      strictEqual(code, 0);
    }));
  }

  {
    const stdout = cp.spawnSync(...pwdCommandAndOptions).stdout;
    strictEqual(`${stdout}`.trim(), cwd);
  }

}

// Test for non-URL objects
for (const badFile of [
  Buffer.from(pwdFullPath),
  {},
  { href: pwdWHATWGUrl.href },
  { pathname: pwdWHATWGUrl.pathname },
]) {
  const pwdCommandAndOptions = isWindows ?
    [badFile, ['/d', '/c', 'cd'], { cwd: cwdUrl }] :
    [badFile, [], { cwd: cwdUrl }];

  // Passing an object that doesn't have shape of WHATWG URL object
  // results in TypeError

  throws(
    () => cp.execFile(...pwdCommandAndOptions, mustNotCall()),
    { code: 'ERR_INVALID_ARG_TYPE' },
  );

  throws(
    () => cp.execFileSync(...pwdCommandAndOptions),
    { code: 'ERR_INVALID_ARG_TYPE' },
  );

  throws(
    () => cp.spawn(...pwdCommandAndOptions),
    { code: 'ERR_INVALID_ARG_TYPE' },
  );

  throws(
    () => cp.spawnSync(...pwdCommandAndOptions),
    { code: 'ERR_INVALID_ARG_TYPE' },
  );
}

// Test for non-file: URL objects
for (const badFile of [
  new URL('https://nodejs.org/file:///'),
  new url.URL('https://nodejs.org/file:///'),
  {
    href: 'https://nodejs.org/file:///',
    origin: 'https://nodejs.org',
    protocol: 'https:',
    username: '',
    password: '',
    host: 'nodejs.org',
    hostname: 'nodejs.org',
    port: '',
    pathname: '/file:///',
    search: '',
    searchParams: new URLSearchParams(),
    hash: ''
  },
]) {
  const pwdCommandAndOptions = isWindows ?
    [badFile, ['/d', '/c', 'cd'], { cwd: cwdUrl }] :
    [badFile, [], { cwd: cwdUrl }];

  // Passing an URL object with protocol other than `file:`
  // results in TypeError

  throws(
    () => cp.execFile(...pwdCommandAndOptions, mustNotCall()),
    { code: 'ERR_INVALID_URL_SCHEME' },
  );

  throws(
    () => cp.execFileSync(...pwdCommandAndOptions),
    { code: 'ERR_INVALID_URL_SCHEME' },
  );

  throws(
    () => cp.spawn(...pwdCommandAndOptions),
    { code: 'ERR_INVALID_URL_SCHEME' },
  );

  throws(
    () => cp.spawnSync(...pwdCommandAndOptions),
    { code: 'ERR_INVALID_URL_SCHEME' },
  );
}

// Test for malformed file URL objects
// On Windows, non-empty host is allowed
if (!isWindows) {
  for (const badFile of [
    new URL('file://nodejs.org/file:///'),
    new url.URL('file://nodejs.org/file:///'),
    {
      href: 'file://nodejs.org/file:///',
      origin: 'null',
      protocol: 'file:',
      username: '',
      password: '',
      host: 'nodejs.org',
      hostname: 'nodejs.org',
      port: '',
      pathname: '/file:///',
      search: '',
      searchParams: new URLSearchParams(),
      hash: ''
    },
  ]) {
    const pwdCommandAndOptions = [badFile, [], { cwd: cwdUrl }];

    // Passing an URL object with non-empty host
    // results in TypeError

    throws(
      () => cp.execFile(...pwdCommandAndOptions, mustNotCall()),
      { code: 'ERR_INVALID_FILE_URL_HOST' },
    );

    throws(
      () => cp.execFileSync(...pwdCommandAndOptions),
      { code: 'ERR_INVALID_FILE_URL_HOST' },
    );

    throws(
      () => cp.spawn(...pwdCommandAndOptions),
      { code: 'ERR_INVALID_FILE_URL_HOST' },
    );

    throws(
      () => cp.spawnSync(...pwdCommandAndOptions),
      { code: 'ERR_INVALID_FILE_URL_HOST' },
    );
  }
}

// Test for file URL objects with %2F in path
const urlWithSlash = new URL(pwdWHATWGUrl);
urlWithSlash.pathname += '%2F';
for (const badFile of [
  urlWithSlash,
  new url.URL(urlWithSlash),
  {
    href: urlWithSlash.href,
    origin: urlWithSlash.origin,
    protocol: urlWithSlash.protocol,
    username: urlWithSlash.username,
    password: urlWithSlash.password,
    host: urlWithSlash.host,
    hostname: urlWithSlash.hostname,
    port: urlWithSlash.port,
    pathname: urlWithSlash.pathname,
    search: urlWithSlash.search,
    searchParams: new URLSearchParams(urlWithSlash.searchParams),
    hash: urlWithSlash.hash,
  },
]) {
  const pwdCommandAndOptions = isWindows ?
    [badFile, ['/d', '/c', 'cd'], { cwd: cwdUrl }] :
    [badFile, [], { cwd: cwdUrl }];

  // Passing an URL object with percent-encoded '/'
  // results in TypeError

  throws(
    () => cp.execFile(...pwdCommandAndOptions, mustNotCall()),
    { code: 'ERR_INVALID_FILE_URL_PATH' },
  );

  throws(
    () => cp.execFileSync(...pwdCommandAndOptions),
    { code: 'ERR_INVALID_FILE_URL_PATH' },
  );

  throws(
    () => cp.spawn(...pwdCommandAndOptions),
    { code: 'ERR_INVALID_FILE_URL_PATH' },
  );

  throws(
    () => cp.spawnSync(...pwdCommandAndOptions),
    { code: 'ERR_INVALID_FILE_URL_PATH' },
  );
}

// Test for file URL objects with %00 in path
const urlWithNul = new URL(pwdWHATWGUrl);
urlWithNul.pathname += '%00';
for (const badFile of [
  urlWithNul,
  new url.URL(urlWithNul),
  {
    href: urlWithNul.href,
    origin: urlWithNul.origin,
    protocol: urlWithNul.protocol,
    username: urlWithNul.username,
    password: urlWithNul.password,
    host: urlWithNul.host,
    hostname: urlWithNul.hostname,
    port: urlWithNul.port,
    pathname: urlWithNul.pathname,
    search: urlWithNul.search,
    searchParams: new URLSearchParams(urlWithNul.searchParams),
    hash: urlWithNul.hash,
  },
]) {
  const pwdCommandAndOptions = isWindows ?
    [badFile, ['/d', '/c', 'cd'], { cwd: cwdUrl }] :
    [badFile, [], { cwd: cwdUrl }];

  // Passing an URL object with percent-encoded '\0'
  // results in TypeError

  throws(
    () => cp.execFile(...pwdCommandAndOptions, mustNotCall()),
    { code: 'ERR_INVALID_ARG_VALUE' },
  );

  throws(
    () => cp.execFileSync(...pwdCommandAndOptions),
    { code: 'ERR_INVALID_ARG_VALUE' },
  );

  throws(
    () => cp.spawn(...pwdCommandAndOptions),
    { code: 'ERR_INVALID_ARG_VALUE' },
  );

  throws(
    () => cp.spawnSync(...pwdCommandAndOptions),
    { code: 'ERR_INVALID_ARG_VALUE' },
  );
}
