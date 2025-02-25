import { mustCall } from '../common/index.mjs';
import { strictEqual } from 'assert';
import process from 'process';
import tick from '../common/tick.js';
import initHooks from './init-hooks.mjs';
import { checkInvocations } from './hook-checks.mjs';

const hooks = initHooks();
hooks.enable();

import { HTTPParser } from '_http_common';

const REQUEST = HTTPParser.REQUEST;

const kOnHeadersComplete = HTTPParser.kOnHeadersComplete | 0;

const request = Buffer.from(
  'GET /hello HTTP/1.1\r\n\r\n'
);

const parser = new HTTPParser();
parser.initialize(REQUEST, {});
const as = hooks.activitiesOfTypes('HTTPINCOMINGMESSAGE');
const httpparser = as[0];

strictEqual(as.length, 1);
strictEqual(typeof httpparser.uid, 'number');
strictEqual(typeof httpparser.triggerAsyncId, 'number');
checkInvocations(httpparser, { init: 1 }, 'when created new Httphttpparser');

parser[kOnHeadersComplete] = mustCall(onheadersComplete);
parser.execute(request, 0, request.length);

function onheadersComplete() {
  checkInvocations(httpparser, { init: 1, before: 1 },
                   'when onheadersComplete called');
  tick(1, mustCall(tick1));
}

function tick1() {
  parser.close();
  tick(1);
}

process.on('exit', onexit);

function onexit() {
  hooks.disable();
  hooks.sanityCheck('HTTPINCOMINGMESSAGE');
  checkInvocations(httpparser, { init: 1, before: 1, after: 1, destroy: 1 },
                   'when process exits');
}
