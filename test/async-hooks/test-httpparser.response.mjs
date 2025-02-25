import { mustCall } from '../common/index.mjs';
import { strictEqual } from 'assert';
import process from 'process';
import tick from '../common/tick.js';
import initHooks from './init-hooks.mjs';
import { checkInvocations } from './hook-checks.mjs';

const hooks = initHooks();

hooks.enable();

import { HTTPParser } from '_http_common';

const RESPONSE = HTTPParser.RESPONSE;
const kOnHeadersComplete = HTTPParser.kOnHeadersComplete | 0;
const kOnBody = HTTPParser.kOnBody | 0;

const request = Buffer.from(
  'HTTP/1.1 200 OK\r\n' +
  'Content-Type: text/plain\r\n' +
  'Content-Length: 4\r\n' +
  '\r\n' +
  'pong'
);

const parser = new HTTPParser();
parser.initialize(RESPONSE, {});
const as = hooks.activitiesOfTypes('HTTPCLIENTREQUEST');
const httpparser = as[0];

strictEqual(as.length, 1);
strictEqual(typeof httpparser.uid, 'number');
strictEqual(typeof httpparser.triggerAsyncId, 'number');
checkInvocations(httpparser, { init: 1 }, 'when created new Httphttpparser');

parser[kOnHeadersComplete] = mustCall(onheadersComplete);
parser[kOnBody] = mustCall(onbody);
parser.execute(request, 0, request.length);

function onheadersComplete() {
  checkInvocations(httpparser, { init: 1, before: 1 },
                   'when onheadersComplete called');
}

function onbody() {
  checkInvocations(httpparser, { init: 1, before: 2, after: 1 },
                   'when onbody called');
  tick(1, mustCall(tick1));
}

function tick1() {
  parser.close();
  tick(1);
}

process.on('exit', onexit);

function onexit() {
  hooks.disable();
  hooks.sanityCheck('HTTPCLIENTREQUEST');
  checkInvocations(httpparser, { init: 1, before: 2, after: 2, destroy: 1 },
                   'when process exits');
}
