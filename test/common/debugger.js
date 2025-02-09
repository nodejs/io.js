'use strict';
const common = require('../common');
const spawn = require('child_process').spawn;

const BREAK_MESSAGE = new RegExp('(?:' + [
  'assert', 'break', 'break on start', 'debugCommand',
  'exception', 'other', 'promiseRejection', 'step',
].join('|') + ') in', 'i');

let TIMEOUT = common.platformTimeout(5000);
if (common.isWindows) {
  // Some of the windows machines in the CI need more time to receive
  // the outputs from the client.
  // https://github.com/nodejs/build/issues/3014
  TIMEOUT = common.platformTimeout(15000);
}

function isPreBreak(output) {
  return /Break on start/.test(output) && /1 \(function \(exports/.test(output);
}

function startCLI(args, flags = [], spawnOpts = {}) {
  let stderrOutput = '';
  const child =
    spawn(process.execPath, [...flags, 'inspect', ...args], spawnOpts);

  const outputBuffer = [];
  function bufferOutput(chunk) {
    if (this === child.stderr) {
      stderrOutput += chunk;
    }
    outputBuffer.push(chunk);
  }

  function getOutput() {
    return outputBuffer.join('\n').replaceAll('\b', '');
  }

  child.stdout.setEncoding('utf8');
  child.stdout.on('data', bufferOutput);
  child.stderr.setEncoding('utf8');
  child.stderr.on('data', bufferOutput);

  if (process.env.VERBOSE === '1') {
    child.stdout.pipe(process.stdout);
    child.stderr.pipe(process.stderr);
  }

  return {
    flushOutput() {
      const output = this.output;
      outputBuffer.length = 0;
      return output;
    },

    waitFor(pattern) {
      function checkPattern(str) {
        if (Array.isArray(pattern)) {
          return pattern.every((p) => p.test(str));
        }
        return pattern.test(str);
      }

      return new Promise((resolve, reject) => {
        function checkOutput() {
          if (checkPattern(getOutput())) {
            tearDown();
            resolve();
          }
        }

        function onChildClose(code, signal) {
          tearDown();
          let message = 'Child exited';
          if (code) {
            message += `, code ${code}`;
          }
          if (signal) {
            message += `, signal ${signal}`;
          }
          message += ` while waiting for ${pattern}; found: ${this.output}`;
          if (stderrOutput) {
            message += `\n STDERR: ${stderrOutput}`;
          }
          reject(new Error(message));
        }

        const timer = setTimeout(() => {
          tearDown();
          reject(new Error(`Timeout (${TIMEOUT}) while waiting for ${pattern}; found: ${this.output}`));
        }, TIMEOUT);

        function tearDown() {
          clearTimeout(timer);
          child.stdout.removeListener('data', checkOutput);
          child.removeListener('close', onChildClose);
        }

        child.on('close', onChildClose);
        child.stdout.on('data', checkOutput);
        checkOutput();
      });
    },

    waitForPrompt() {
      return this.waitFor(/>\s+$/);
    },

    async waitForInitialBreak() {
      await this.waitFor(/break (?:on start )?in/i);

      if (isPreBreak(this.output)) {
        await this.command('next', false);
        return this.waitFor(/break in/);
      }
    },

    get breakInfo() {
      const output = this.output;
      const breakMatch =
        output.match(/(step |break (?:on start )?)in ([^\n]+):(\d+)\n/i);

      if (breakMatch === null) {
        throw new Error(
          `Could not find breakpoint info in ${JSON.stringify(output)}`);
      }
      return { filename: breakMatch[2], line: +breakMatch[3] };
    },

    ctrlC() {
      return this.command('.interrupt');
    },

    get output() {
      return getOutput();
    },

    get rawOutput() {
      return outputBuffer.join('').toString();
    },

    parseSourceLines() {
      return getOutput().split('\n')
        .map((line) => line.match(/(?:\*|>)?\s*(\d+)/))
        .filter((match) => match !== null)
        .map((match) => +match[1]);
    },

    writeLine(input, flush = true) {
      if (flush) {
        this.flushOutput();
      }
      if (process.env.VERBOSE === '1') {
        process.stderr.write(`< ${input}\n`);
      }
      child.stdin.write(input);
      child.stdin.write('\n');
    },

    command(input, flush = true) {
      this.writeLine(input, flush);
      return this.waitForPrompt();
    },

    stepCommand(input) {
      this.writeLine(input, true);
      return this
        .waitFor(BREAK_MESSAGE)
        .then(() => this.waitForPrompt());
    },

    quit() {
      return new Promise((resolve) => {
        child.stdin.end();
        child.on('close', resolve);
      });
    },
  };
}
module.exports = startCLI;
