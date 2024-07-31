import { run } from 'node:test';
import { tap } from 'node:test/reporters';
import { parseArgs } from 'node:util';

const options = {
  file: {
    type: 'string',
  },
};
const {
  values,
  positionals,
} = parseArgs({ args: process.argv.slice(2), options });

let files;

if (values.file) {
  files = [values.file];
}

run({
  files,
  watch: true
}).compose(tap).pipe(process.stdout);
