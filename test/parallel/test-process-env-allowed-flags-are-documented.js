'use strict';

const common = require('../common');

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const { hasOpenSSL3 } = require('../common/crypto');

const rootDir = path.resolve(__dirname, '..', '..');
const cliMd = path.join(rootDir, 'doc', 'api', 'cli.md');
const cliText = fs.readFileSync(cliMd, { encoding: 'utf8' });

const parseSection = (text, startMarker, endMarker) => {
  const regExp = new RegExp(`${startMarker}\r?\n([^]*)\r?\n${endMarker}`);
  const match = text.match(regExp);
  assert(match,
         `Unable to locate text between '${startMarker}' and '${endMarker}'.`);
  return match[1]
         .split(/\r?\n/)
         .filter((val) => val.trim() !== '');
};

const nodeOptionsLines = parseSection(cliText,
                                      '<!-- node-options-node start -->',
                                      '<!-- node-options-node end -->');
const v8OptionsLines = parseSection(cliText,
                                    '<!-- node-options-v8 start -->',
                                    '<!-- node-options-v8 end -->');

// Check the options are documented in alphabetical order.
assert.deepStrictEqual(nodeOptionsLines, [...nodeOptionsLines].sort());
assert.deepStrictEqual(v8OptionsLines, [...v8OptionsLines].sort());

const documented = new Set();
for (const line of [...nodeOptionsLines, ...v8OptionsLines]) {
  for (const match of line.matchAll(/`(-[^`]+)`/g)) {
    // Remove negation from the option's name.
    const option = match[1].replace('--no-', '--');
    assert(!documented.has(option),
           `Option '${option}' was documented more than once as an ` +
           `allowed option for NODE_OPTIONS in ${cliMd}.`);
    documented.add(option);
  }
}

if (!hasOpenSSL3) {
  documented.delete('--openssl-legacy-provider');
  documented.delete('--openssl-shared-config');
}

// Filter out options that are conditionally present.
const conditionalOpts = [
  {
    include: common.hasCrypto,
    filter: (opt) => {
      return [
        '--openssl-config',
        hasOpenSSL3 ? '--openssl-legacy-provider' : '',
        hasOpenSSL3 ? '--openssl-shared-config' : '',
        '--tls-cipher-list',
        '--use-bundled-ca',
        '--use-openssl-ca',
        common.isMacOS ? '--use-system-ca' : '',
        '--secure-heap',
        '--secure-heap-min',
        '--enable-fips',
        '--force-fips',
      ].includes(opt);
    }
  }, {
    include: common.hasIntl,
    filter: (opt) => opt === '--icu-data-dir'
  }, {
    include: process.features.inspector,
    filter: (opt) => opt.startsWith('--inspect') || opt === '--debug-port'
  },
];
documented.forEach((opt) => {
  conditionalOpts.forEach(({ include, filter }) => {
    if (!include && filter(opt)) {
      documented.delete(opt);
    }
  });
});

const difference = (setA, setB) => {
  return new Set([...setA].filter((x) => !setB.has(x)));
};

// Remove heap prof options if the inspector is not enabled.
// NOTE: this is for ubuntuXXXX_sharedlibs_withoutssl_x64, no SSL, no inspector
// Refs: https://github.com/nodejs/node/pull/54259#issuecomment-2308256647
if (!process.features.inspector) {
  [
    '--cpu-prof-dir',
    '--cpu-prof-interval',
    '--cpu-prof-name',
    '--cpu-prof',
    '--heap-prof-dir',
    '--heap-prof-interval',
    '--heap-prof-name',
    '--heap-prof',
  ].forEach((opt) => documented.delete(opt));
}

const overdocumented = difference(documented,
                                  process.allowedNodeEnvironmentFlags);
assert.strictEqual(overdocumented.size, 0,
                   'The following options are documented as allowed in ' +
                   `NODE_OPTIONS in ${cliMd}: ` +
                   `${[...overdocumented].join(' ')} ` +
                   'but are not in process.allowedNodeEnvironmentFlags');
const undocumented = difference(process.allowedNodeEnvironmentFlags,
                                documented);
// Remove intentionally undocumented options.
assert(undocumented.delete('--debug-arraybuffer-allocations'));
assert(undocumented.delete('--no-debug-arraybuffer-allocations'));
assert(undocumented.delete('--es-module-specifier-resolution'));
assert(undocumented.delete('--experimental-fetch'));
assert(undocumented.delete('--experimental-wasm-modules'));
assert(undocumented.delete('--experimental-global-customevent'));
assert(undocumented.delete('--experimental-global-webcrypto'));
assert(undocumented.delete('--experimental-report'));
assert(undocumented.delete('--experimental-worker'));
assert(undocumented.delete('--node-snapshot'));
assert(undocumented.delete('--no-node-snapshot'));
assert(undocumented.delete('--loader'));
assert(undocumented.delete('--verify-base-objects'));
assert(undocumented.delete('--no-verify-base-objects'));
assert(undocumented.delete('--trace-promises'));
assert(undocumented.delete('--no-trace-promises'));
assert(undocumented.delete('--experimental-quic'));
if (common.hasQuic) {
  assert(undocumented.delete('--no-experimental-quic'));
}

// Remove negated versions of the flags.
for (const flag of undocumented) {
  if (flag.startsWith('--no-')) {
    assert(documented.has(`--${flag.slice(5)}`), flag);
    undocumented.delete(flag);
  }
}

assert.strictEqual(undocumented.size, 0,
                   'The following options are not documented as allowed in ' +
                   `NODE_OPTIONS in ${cliMd}: ${[...undocumented].join(' ')}`);
