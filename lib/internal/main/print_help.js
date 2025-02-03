'use strict';

const {
  ArrayPrototypeConcat,
  ArrayPrototypeSort,
  Boolean,
  MathFloor,
  MathMax,
  ObjectKeys,
  RegExp,
  RegExpPrototypeSymbolReplace,
  SafeMap,
  StringPrototypeLocaleCompare,
  StringPrototypeRepeat,
  StringPrototypeSlice,
  StringPrototypeTrimStart,
} = primordials;

const { types } = internalBinding('options');
const hasCrypto = Boolean(process.versions.openssl);

const {
  prepareMainThreadExecution,
  markBootstrapComplete,
} = require('internal/process/pre_execution');

const { getCLIOptionsInfo, getOptionValue } = require('internal/options');

const typeLookup = [];
for (const key of ObjectKeys(types))
  typeLookup[types[key]] = key;

// Environment variables are parsed ad-hoc throughout the code base,
// so we gather the documentation here.
const { hasIntl, hasSmallICU, hasNodeOptions } = internalBinding('config');
// eslint-disable-next-line node-core/avoid-prototype-pollution
const envVars = new SafeMap(ArrayPrototypeConcat([
  ['FORCE_COLOR', { helpText: "when set to 'true', 1, 2, 3, or an empty " +
   'string causes NO_COLOR and NODE_DISABLE_COLORS to be ignored.' }],
  ['NO_COLOR', { helpText: 'Alias for NODE_DISABLE_COLORS' }],
  ['NODE_DEBUG', { helpText: "','-separated list of core modules that " +
    'should print debug information' }],
  ['NODE_DEBUG_NATIVE', { helpText: "','-separated list of C++ core debug " +
    'categories that should print debug output' }],
  ['NODE_DISABLE_COLORS', { helpText: 'set to 1 to disable colors in ' +
    'the REPL' }],
  ['NODE_EXTRA_CA_CERTS', { helpText: 'path to additional CA certificates ' +
    'file. Only read once during process startup.' }],
  ['NODE_NO_WARNINGS', { helpText: 'set to 1 to silence process warnings' }],
  ['NODE_PATH', { helpText: `'${require('path').delimiter}'-separated list ` +
    'of directories prefixed to the module search path' }],
  ['NODE_PENDING_DEPRECATION', { helpText: 'set to 1 to emit pending ' +
    'deprecation warnings' }],
  ['NODE_PENDING_PIPE_INSTANCES', { helpText: 'set the number of pending ' +
    'pipe instance handles on Windows' }],
  ['NODE_PRESERVE_SYMLINKS', { helpText: 'set to 1 to preserve symbolic ' +
    'links when resolving and caching modules' }],
  ['NODE_REDIRECT_WARNINGS', { helpText: 'write warnings to path instead ' +
    'of stderr' }],
  ['NODE_REPL_HISTORY', { helpText: 'path to the persistent REPL ' +
    'history file' }],
  ['NODE_REPL_EXTERNAL_MODULE', { helpText: 'path to a Node.js module ' +
    'which will be loaded in place of the built-in REPL' }],
  ['NODE_SKIP_PLATFORM_CHECK', { helpText: 'set to 1 to skip ' +
    'the check for a supported platform during Node.js startup' }],
  ['NODE_TLS_REJECT_UNAUTHORIZED', { helpText: 'set to 0 to disable TLS ' +
    'certificate validation' }],
  ['NODE_V8_COVERAGE', { helpText: 'directory to output v8 coverage JSON ' +
    'to' }],
  ['TZ', { helpText: 'specify the timezone configuration' }],
  ['UV_THREADPOOL_SIZE', { helpText: 'sets the number of threads used in ' +
    'libuv\'s threadpool' }],
], hasIntl ? [
  ['NODE_ICU_DATA', { helpText: 'data path for ICU (Intl object) data' +
    hasSmallICU ? '' : ' (will extend linked-in data)' }],
] : []), (hasNodeOptions ? [
  ['NODE_OPTIONS', { helpText: 'set CLI options in the environment via a ' +
    'space-separated list' }],
] : []), hasCrypto ? [
  ['OPENSSL_CONF', { helpText: 'load OpenSSL configuration from file' }],
  ['SSL_CERT_DIR', { helpText: 'sets OpenSSL\'s directory of trusted ' +
    'certificates when used in conjunction with --use-openssl-ca' }],
  ['SSL_CERT_FILE', { helpText: 'sets OpenSSL\'s trusted certificate file ' +
    'when used in conjunction with --use-openssl-ca' }],
] : []);


function indent(text, depth) {
  return RegExpPrototypeSymbolReplace(/^/gm, text, StringPrototypeRepeat(' ', depth));
}

function fold(text, width) {
  return RegExpPrototypeSymbolReplace(
    new RegExp(`([^\n]{0,${width}})( |$)`, 'g'),
    text,
    (_, newLine, end) => newLine + (end === ' ' ? '\n' : ''),
  );
}

function getArgDescription(type) {
  switch (typeLookup[type]) {
    case 'kNoOp':
    case 'kV8Option':
    case 'kBoolean':
    case undefined:
      break;
    case 'kHostPort':
      return '[host:]port';
    case 'kInteger':
    case 'kUInteger':
    case 'kString':
    case 'kStringList':
      return '...';
    default:
      require('assert').fail(`unknown option type ${type}`);
  }
}

function format(
  { options, aliases = new SafeMap(), firstColumn, secondColumn },
) {
  let text = '';
  let maxFirstColumnUsed = 0;

  const sortedOptions = ArrayPrototypeSort(
    [...options.entries()],
    ({ 0: name1, 1: option1 }, { 0: name2, 1: option2 }) => {
      if (option1.defaultIsTrue) {
        name1 = `--no-${StringPrototypeSlice(name1, 2)}`;
      }
      if (option2.defaultIsTrue) {
        name2 = `--no-${StringPrototypeSlice(name2, 2)}`;
      }
      return StringPrototypeLocaleCompare(name1, name2);
    },
  );

  for (const {
    0: name, 1: { helpText, type, defaultIsTrue },
  } of sortedOptions) {
    if (!helpText) continue;
    const value = getOptionValue(name);

    let displayName = name;

    if (defaultIsTrue) {
      displayName = `--no-${StringPrototypeSlice(displayName, 2)}`;
    }

    const argDescription = getArgDescription(type);
    if (argDescription)
      displayName += `=${argDescription}`;

    for (const { 0: from, 1: to } of aliases) {
      // For cases like e.g. `-e, --eval`.
      if (to[0] === name && to.length === 1) {
        displayName = `${from}, ${displayName}`;
      }

      // For cases like `--inspect-brk[=[host:]port]`.
      const targetInfo = options.get(to[0]);
      const targetArgDescription =
        targetInfo ? getArgDescription(targetInfo.type) : '...';
      if (from === `${name}=`) {
        displayName += `[=${targetArgDescription}]`;
      } else if (from === `${name} <arg>`) {
        displayName += ` [${targetArgDescription}]`;
      }
    }

    let displayHelpText = helpText;
    if (value === !defaultIsTrue) {
      // Mark boolean options we currently have enabled.
      // In particular, it indicates whether --use-openssl-ca
      // or --use-bundled-ca is the (current) default.
      displayHelpText += ' (currently set)';
    }

    text += displayName;
    maxFirstColumnUsed = MathMax(maxFirstColumnUsed, displayName.length);
    if (displayName.length >= firstColumn)
      text += '\n' + StringPrototypeRepeat(' ', firstColumn);
    else
      text += StringPrototypeRepeat(' ', firstColumn - displayName.length);

    text += StringPrototypeTrimStart(
      indent(fold(displayHelpText, secondColumn), firstColumn)) + '\n';
  }

  if (maxFirstColumnUsed < firstColumn - 4) {
    // If we have more than 4 blank gap spaces, reduce first column width.
    return format({
      options,
      aliases,
      firstColumn: maxFirstColumnUsed + 2,
      secondColumn,
    });
  }

  return text;
}

function print(stream) {
  const { options, aliases } = getCLIOptionsInfo();
  const console = require('internal/console/global');

  // Use 75 % of the available width, and at least 70 characters.
  const width = MathMax(70, (stream.columns || 0) * 0.75);
  const firstColumn = MathFloor(width * 0.4);
  const secondColumn = MathFloor(width * 0.57);

  options.set('-', { helpText: 'script read from stdin ' +
                               '(default if no file name is provided, ' +
                               'interactive mode if a tty)' });
  options.set('--', { helpText: 'indicate the end of node options' });
  let helpText = (
    'Usage: node [options] [ script.js ] [arguments]\n' +
    '       node inspect [options] [ script.js | host:port ] [arguments]\n\n' +
    'Options:\n');
  helpText += (indent(format({
    options, aliases, firstColumn, secondColumn,
  }), 2));

  helpText += ('\nEnvironment variables:\n');

  helpText += (format({
    options: envVars, firstColumn, secondColumn,
  }));

  helpText += ('\nDocumentation can be found at https://nodejs.org/');
  console.log(helpText);
}

prepareMainThreadExecution();

markBootstrapComplete();

print(process.stdout);
