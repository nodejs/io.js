'use strict';
const { relative } = require('node:path');
const { inspect } = require('node:util');
const cwd = process.cwd();

module.exports = async function* errorReporter(source) {
  for await (const event of source) {
    if (event.type === 'test:fail') {
      const { name, details, line, column, file } = event.data;
      let { error } = details;

      if (error?.failureType === 'subtestsFailed') {
        // In the interest of keeping things concise, skip failures that are
        // only due to nested failures.
        continue;
      }

      if (error?.code === 'ERR_TEST_FAILURE') {
        error = error.cause;
      }

      const output = [
        `Test failure: '${name}'`,
      ];

      if (file) {
        output.push(`Location: ${relative(cwd, file)}:${line}:${column}`);
      }

      output.push(inspect(error));
      output.push('\n');
      yield output.join('\n');

      if (process.env.FAIL_FAST) {
        yield `\nBailing on failed test: ${event.data.name}\n`;
        process.exitCode = 1;
        process.emit('SIGINT');
      }
    }
  }
};
