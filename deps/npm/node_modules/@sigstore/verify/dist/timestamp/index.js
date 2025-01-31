"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.verifyTSATimestamp = verifyTSATimestamp;
exports.verifyTLogTimestamp = verifyTLogTimestamp;
const error_1 = require("../error");
const checkpoint_1 = require("./checkpoint");
const merkle_1 = require("./merkle");
const set_1 = require("./set");
const tsa_1 = require("./tsa");
function verifyTSATimestamp(timestamp, data, timestampAuthorities) {
    (0, tsa_1.verifyRFC3161Timestamp)(timestamp, data, timestampAuthorities);
    return {
        type: 'timestamp-authority',
        logID: timestamp.signerSerialNumber,
        timestamp: timestamp.signingTime,
    };
}
function verifyTLogTimestamp(entry, tlogAuthorities) {
    let inclusionVerified = false;
    if (isTLogEntryWithInclusionPromise(entry)) {
        (0, set_1.verifyTLogSET)(entry, tlogAuthorities);
        inclusionVerified = true;
    }
    if (isTLogEntryWithInclusionProof(entry)) {
        (0, merkle_1.verifyMerkleInclusion)(entry);
        (0, checkpoint_1.verifyCheckpoint)(entry, tlogAuthorities);
        inclusionVerified = true;
    }
    if (!inclusionVerified) {
        throw new error_1.VerificationError({
            code: 'TLOG_MISSING_INCLUSION_ERROR',
            message: 'inclusion could not be verified',
        });
    }
    return {
        type: 'transparency-log',
        logID: entry.logId.keyId,
        timestamp: new Date(Number(entry.integratedTime) * 1000),
    };
}
function isTLogEntryWithInclusionPromise(entry) {
    return entry.inclusionPromise !== undefined;
}
function isTLogEntryWithInclusionProof(entry) {
    return entry.inclusionProof !== undefined;
}
