"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.defaultConfig = void 0;
exports.defaultConfig = {
    maxRootRotations: 32,
    maxDelegations: 32,
    rootMaxLength: 512000, //bytes
    timestampMaxLength: 16384, // bytes
    snapshotMaxLength: 2000000, // bytes
    targetsMaxLength: 5000000, // bytes
    prefixTargetsWithHash: true,
    fetchTimeout: 100000, // milliseconds
    fetchRetries: undefined,
    fetchRetry: 2,
};
