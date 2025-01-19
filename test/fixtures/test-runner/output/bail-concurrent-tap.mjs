import * as fixtures from '../../../common/fixtures.mjs';
import { run } from 'node:test';
import { tap } from 'node:test/reporters';
import { Readable } from 'stream';

const testFixtures = fixtures.path('test-runner', 'bailout', 'parallel-output');

const stream = run({
  bail: true,
  concurrency: 2,
  cwd: testFixtures,
});

const testResults = await stream.toArray();

const readable = Readable.from(testResults);
readable.compose(tap).pipe(process.stdout);
