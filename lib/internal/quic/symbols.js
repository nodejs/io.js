'use strict';

const {
  Symbol,
} = primordials;

const {
  customInspectSymbol: kInspect,
} = require('internal/util');

const {
  kHandle: kKeyObjectHandle,
  kKeyObject: kKeyObjectInner,
} = require('internal/crypto/util');

// Symbols used to hide various private properties and methods from the
// public API.

const kApplicationProvider = Symbol('kApplicationProvider');
const kBlocked = Symbol('kBlocked');
const kConnect = Symbol('kConnect');
const kDatagram = Symbol('kDatagram');
const kDatagramStatus = Symbol('kDatagramStatus');
const kFinishClose = Symbol('kFinishClose');
const kHandshake = Symbol('kHandshake');
const kHeaders = Symbol('kHeaders');
const kListen = Symbol('kListen');
const kNewSession = Symbol('kNewSession');
const kNewStream = Symbol('kNewStream');
const kOnHeaders = Symbol('kOnHeaders');
const kOnTrailers = Symbol('kOwnTrailers');
const kOwner = Symbol('kOwner');
const kPathValidation = Symbol('kPathValidation');
const kPrivateConstructor = Symbol('kPrivateConstructor');
const kRemoveSession = Symbol('kRemoveSession');
const kRemoveStream = Symbol('kRemoveStream');
const kReset = Symbol('kReset');
const kSendHeaders = Symbol('kSendHeaders');
const kSessionTicket = Symbol('kSessionTicket');
const kState = Symbol('kState');
const kTrailers = Symbol('kTrailers');
const kVersionNegotiation = Symbol('kVersionNegotiation');
const kWantsHeaders = Symbol('kWantsHeaders');
const kWantsTrailers = Symbol('kWantsTrailers');

module.exports = {
  kApplicationProvider,
  kBlocked,
  kConnect,
  kDatagram,
  kDatagramStatus,
  kFinishClose,
  kHandshake,
  kHeaders,
  kInspect,
  kKeyObjectHandle,
  kKeyObjectInner,
  kListen,
  kNewSession,
  kNewStream,
  kOnHeaders,
  kOnTrailers,
  kOwner,
  kPathValidation,
  kPrivateConstructor,
  kRemoveSession,
  kRemoveStream,
  kReset,
  kSendHeaders,
  kSessionTicket,
  kState,
  kTrailers,
  kVersionNegotiation,
  kWantsHeaders,
  kWantsTrailers,
};
