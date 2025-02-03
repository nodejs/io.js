import { run } from 'node:test';
import { tap } from 'node:test/reporters';
import { parseArgs } from 'node:util';

const options = {
  file: {
    type: 'string',
  },
  cwd: {
    type: 'string',
  },
  isolation: {
    type: 'string',
  },
};
const {
  values,
  positionals,
} = parseArgs({ args: process.argv.slice(2), options });

let files;
let cwd;
let isolation;

if (values.file) {
  files = [values.file];
}

if (values.cwd) {
  cwd = values.cwd;
}

if (values.isolation) {
  isolation = values.isolation;
}

run({
  files,
  watch: true,
  cwd,
  isolation,
}).compose(tap).pipe(process.stdout);
