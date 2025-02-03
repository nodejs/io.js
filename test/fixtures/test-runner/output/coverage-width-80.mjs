// Flags: --experimental-test-coverage
// here we can't import common module as the coverage will be different based on the system
// Unused imports are here in order to populate the coverage report
import * as a from '../coverage-snap/b.js';
import * as b from '../coverage-snap/a.js';

import { test } from 'node:test';

process.stdout.columns = 80;

test(`Coverage Print Fixed Width ${process.stdout.columns}`);
