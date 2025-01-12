'use strict';

const {
  isInternalThread,
  isMainThread,
  SHARE_ENV,
  resourceLimits,
  setEnvironmentData,
  getEnvironmentData,
  threadId,
  Worker,
} = require('internal/worker');

const {
  MessagePort,
  MessageChannel,
  markAsUncloneable,
  moveMessagePortToContext,
  receiveMessageOnPort,
  BroadcastChannel,
} = require('internal/worker/io');

const {
  postMessageToThread,
} = require('internal/worker/messaging');

const {
  markAsUntransferable,
  isMarkedAsUntransferable,
} = require('internal/buffer');

module.exports = {
  isInternalThread,
  isMainThread,
  MessagePort,
  MessageChannel,
  markAsUncloneable,
  markAsUntransferable,
  isMarkedAsUntransferable,
  moveMessagePortToContext,
  receiveMessageOnPort,
  resourceLimits,
  postMessageToThread,
  threadId,
  SHARE_ENV,
  Worker,
  parentPort: null,
  workerData: null,
  BroadcastChannel,
  setEnvironmentData,
  getEnvironmentData,
};
