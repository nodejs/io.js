const {
  ArrayPrototypeShift,
  ArrayPrototypeSplice,
  ArrayPrototypeIncludes,
  StringPrototypeSplit,
  RegExpPrototypeExec,
} = primordials;

function parseBoolean(value) {
  return value === 'true' || value === '1' || value === 'yes';
}

function validatePauseOnExceptionState(value) {
  const validStates = ['uncaught', 'none', 'all'];
  if (!ArrayPrototypeIncludes(validStates, value)) {
    throw new Error(`Invalid state passed for pauseOnExceptionState: ${value}. Must be one of 'uncaught', 'none', or 'all'.`);
  }
  return value;
}

function parseArguments(argv) {
  const legacyArguments = processLegacyArgs(argv)

  let options = {
    pauseOnExceptionState: undefined,
    inspectResumeOnStart: undefined
  }

  // `NODE_INSPECT_OPTIONS` is parsed first and can be overwritten by command line arguments

  if (process.env.NODE_INSPECT_OPTIONS) {
    const envOptions = StringPrototypeSplit(process.env.NODE_INSPECT_OPTIONS, ' ');
    for (let i = 0; i < envOptions.length; i++) {
      switch (envOptions[i]) {
        case '--pause-on-exception-state':
          options.pauseOnExceptionState = validatePauseOnExceptionState(envOptions[++i]);
          break;
        case '--inspect-resume-on-start':
          options.inspectResumeOnStart = parseBoolean(envOptions[++i]);
          break;
      }
    }
  }

  for (let i = 0; i < argv.length;) {
    switch (argv[i]) {
      case '--pause-on-exception-state':
        options.pauseOnExceptionState = validatePauseOnExceptionState(argv[i+1]);
        ArrayPrototypeSplice(argv, i, 2);
        break;
      case '--inspect-resume-on-start':
        options.inspectResumeOnStart = parseBoolean(argv[i+1]);
        ArrayPrototypeSplice(argv, i, 2);
        break;
      default:
        i++;
        break;
    }
  }

  return {...options, ...legacyArguments};
}

// the legacy `node inspect` options assumed the first argument was the target
// to avoid breaking existing scripts, we maintain this behavior

function processLegacyArgs(args) {
  const target = ArrayPrototypeShift(args);
  let host = '127.0.0.1';
  let port = 9229;
  let isRemote = false;
  let script = target;
  let scriptArgs = args;

  const hostMatch = RegExpPrototypeExec(/^([^:]+):(\d+)$/, target);
  const portMatch = RegExpPrototypeExec(/^--port=(\d+)$/, target);

  if (hostMatch) {
    // Connecting to remote debugger
    host = hostMatch[1];
    port = Number(hostMatch[2]);
    isRemote = true;
    script = null;
  } else if (portMatch) {
    // Start on custom port
    port = Number(portMatch[1]);
    script = args[0];
    scriptArgs = ArrayPrototypeSlice(args, 1);
  } else if (args.length === 1 && RegExpPrototypeExec(/^\d+$/, args[0]) !== null &&
             target === '-p') {
    // Start debugger against a given pid
    const pid = Number(args[0]);
    try {
      process._debugProcess(pid);
    } catch (e) {
      if (e.code === 'ESRCH') {
        process.stderr.write(`Target process: ${pid} doesn't exist.\n`);
        process.exit(kGenericUserError);
      }
      throw e;
    }
    script = null;
    isRemote = true;
  }

  return {
    host, port, isRemote, script, scriptArgs,
  };
}

module.exports = parseArguments;
