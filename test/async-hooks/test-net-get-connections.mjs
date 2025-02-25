import { mustCall } from '../common/index.mjs';
import { createServer } from 'net';
const server = createServer();

// This test was based on an error raised by Haraka.
// It caused server.getConnections to raise an exception.
// Ref: https://github.com/haraka/Haraka/pull/1951

server.getConnections(mustCall());
