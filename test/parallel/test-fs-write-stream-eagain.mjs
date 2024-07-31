import * as common from '../common/index.mjs';
import tmpdir from '../common/tmpdir.js';
import assert from 'node:assert';
import fs from 'node:fs';
import { describe, it, mock } from 'node:test';
import { finished } from 'node:stream/promises';

tmpdir.refresh();
const file = tmpdir.resolve('writeStreamEAGAIN.txt');
const errorWithEAGAIN = (fd, buffer, offset, length, position, callback) => {
  callback(Object.assign(new Error(), { code: 'EAGAIN' }), 0, buffer);
};

describe('WriteStream EAGAIN', { concurrency: !process.env.TEST_PARALLEL }, () => {
  it('_write', async () => {
    const mockWrite = mock.fn(fs.write);
    mockWrite.mock.mockImplementationOnce(errorWithEAGAIN);
    const stream = fs.createWriteStream(file, {
      fs: {
        open: common.mustCall(fs.open),
        write: mockWrite,
        close: common.mustCall(fs.close),
      }
    });
    stream.end('foo');
    stream.on('close', common.mustCall());
    stream.on('error', common.mustNotCall());
    await finished(stream);
    assert.strictEqual(mockWrite.mock.callCount(), 2);
    assert.strictEqual(fs.readFileSync(file, 'utf8'), 'foo');
  });

  it('_write', async () => {
    const stream = fs.createWriteStream(file);
    mock.getter(stream, 'destroyed', () => true);
    stream.end('foo');
    await finished(stream).catch(common.mustCall());
  });
});
