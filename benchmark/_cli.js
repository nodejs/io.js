'use strict';

const fs = require('fs');
const path = require('path');

// Create an object of all benchmark scripts
const benchmarks = {};
fs.readdirSync(__dirname)
  .filter((name) => {
    return name !== 'fixtures' &&
           fs.statSync(path.resolve(__dirname, name)).isDirectory();
  })
  .forEach((category) => {
    benchmarks[category] = fs.readdirSync(path.resolve(__dirname, category))
      .filter((filename) => filename[0] !== '.' && filename[0] !== '_');
  });

function CLI(usage, settings) {
  if (process.argv.length < 3) {
    this.abort(usage); // Abort will exit the process
  }

  this.usage = usage;
  this.optional = {};
  this.items = [];
  this.test = false;

  for (const argName of settings.arrayArgs) {
    this.optional[argName] = [];
  }

  let currentOptional = null;
  let mode = 'both'; // Possible states are: [both, option, item]

  for (const arg of process.argv.slice(2)) {
    if (arg === '--help') this.abort(usage);
    if (arg === '--') {
      // Only items can follow --
      mode = 'item';
    } else if (mode === 'both' && arg[0] === '-') {
      // Optional arguments declaration

      if (arg[1] === '-') {
        currentOptional = arg.slice(2);
      } else {
        currentOptional = arg.slice(1);
      }

      if (settings.boolArgs && settings.boolArgs.includes(currentOptional)) {
        this.optional[currentOptional] = true;
        mode = 'both';
      } else {
        // Expect the next value to be option related (either -- or the value)
        mode = 'option';
      }
    } else if (mode === 'option') {
      // Optional arguments value

      if (settings.arrayArgs.includes(currentOptional)) {
        this.optional[currentOptional].push(arg);
      } else {
        this.optional[currentOptional] = arg;
      }

      // The next value can be either an option or an item
      mode = 'both';
    } else if (arg === 'test') {
      this.test = true;
    } else if (['both', 'item'].includes(mode)) {
      // item arguments
      this.items.push(arg);

      // The next value must be an item
      mode = 'item';
    } else {
      // Bad case, abort
      this.abort(usage);
    }
  }
}
module.exports = CLI;

CLI.prototype.abort = function(msg) {
  console.error(msg);
  process.exit(1);
};

CLI.prototype.benchmarks = function() {
  const paths = [];

  if (this.items.includes('all')) {
    this.items = Object.keys(benchmarks);
  }

  for (const category of this.items) {
    if (benchmarks[category] === undefined) {
      console.error(`The "${category}" category does not exist.`);
      process.exit(1);
    }
    for (const scripts of benchmarks[category]) {
      if (this.shouldSkip(scripts)) continue;

      paths.push(path.join(category, scripts));
    }
  }

  return paths;
};

CLI.prototype.shouldSkip = function(scripts) {
  const filters = this.optional.filter || [];
  const excludes = this.optional.exclude || [];
  let skip = filters.length > 0;

  for (const filter of filters) {
    if (scripts.lastIndexOf(filter) !== -1) {
      skip = false;
    }
  }

  for (const exclude of excludes) {
    if (scripts.lastIndexOf(exclude) !== -1) {
      skip = true;
    }
  }

  return skip;
};

/**
 * Extracts the CPU core setting from the CLI arguments.
 * @returns {string|null} The CPU core setting if found, otherwise null.
 */
CLI.prototype.getCpuCoreSetting = function() {
  const cpuCoreSetting = this.optional.set.find((s) => s.startsWith('CPUSET='));
  if (!cpuCoreSetting) return null;

  const value = cpuCoreSetting.split('=')[1];
  // Validate the CPUSET value to match patterns like "0", "0-2", "0,1,2", "0,2-4,6" or "0,0,1-2"
  const isValid = /^(\d+(-\d+)?)(,\d+(-\d+)?)*$/.test(value);
  if (!isValid) {
    throw new Error(`
        Invalid CPUSET format: "${value}". Please use a single core number (e.g., "0"), 
        a range of cores (e.g., "0-3"), or a list of cores/ranges 
        (e.g., "0,2,4" or "0-2,4").\n\n${this.usage}
    `);
  }
  return value;
};
