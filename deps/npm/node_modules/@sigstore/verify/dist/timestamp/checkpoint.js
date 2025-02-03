"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.verifyCheckpoint = verifyCheckpoint;
/*
Copyright 2023 The Sigstore Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
const core_1 = require("@sigstore/core");
const error_1 = require("../error");
const trust_1 = require("../trust");
// Separator between the note and the signatures in a checkpoint
const CHECKPOINT_SEPARATOR = '\n\n';
// Checkpoint signatures are of the following form:
// "– <identity> <key_hint+signature_bytes>\n"
// where:
// - the prefix is an emdash (U+2014).
// - <identity> gives a human-readable representation of the signing ID.
// - <key_hint+signature_bytes> is the first 4 bytes of the SHA256 hash of the
//   associated public key followed by the signature bytes.
const SIGNATURE_REGEX = /\u2014 (\S+) (\S+)\n/g;
// Verifies the checkpoint value in the given tlog entry. There are two steps
// to the verification:
// 1. Verify that all signatures in the checkpoint can be verified against a
//    trusted public key
// 2. Verify that the root hash in the checkpoint matches the root hash in the
//    inclusion proof
// See: https://github.com/transparency-dev/formats/blob/main/log/README.md
function verifyCheckpoint(entry, tlogs) {
    // Filter tlog instances to just those which were valid at the time of the
    // entry
    const validTLogs = (0, trust_1.filterTLogAuthorities)(tlogs, {
        targetDate: new Date(Number(entry.integratedTime) * 1000),
    });
    const inclusionProof = entry.inclusionProof;
    const signedNote = SignedNote.fromString(inclusionProof.checkpoint.envelope);
    const checkpoint = LogCheckpoint.fromString(signedNote.note);
    // Verify that the signatures in the checkpoint are all valid
    if (!verifySignedNote(signedNote, validTLogs)) {
        throw new error_1.VerificationError({
            code: 'TLOG_INCLUSION_PROOF_ERROR',
            message: 'invalid checkpoint signature',
        });
    }
    // Verify that the root hash from the checkpoint matches the root hash in the
    // inclusion proof
    if (!core_1.crypto.bufferEqual(checkpoint.logHash, inclusionProof.rootHash)) {
        throw new error_1.VerificationError({
            code: 'TLOG_INCLUSION_PROOF_ERROR',
            message: 'root hash mismatch',
        });
    }
}
// Verifies the signatures in the SignedNote. For each signature, the
// corresponding transparency log is looked up by the key hint and the
// signature is verified against the public key in the transparency log.
// Throws an error if any of the signatures are invalid.
function verifySignedNote(signedNote, tlogs) {
    const data = Buffer.from(signedNote.note, 'utf-8');
    return signedNote.signatures.every((signature) => {
        // Find the transparency log instance with the matching key hint
        const tlog = tlogs.find((tlog) => core_1.crypto.bufferEqual(tlog.logID.subarray(0, 4), signature.keyHint));
        if (!tlog) {
            return false;
        }
        return core_1.crypto.verify(data, tlog.publicKey, signature.signature);
    });
}
// SignedNote represents a signed note from a transparency log checkpoint. Consists
// of a body (or note) and one more signatures calculated over the body. See
// https://github.com/transparency-dev/formats/blob/main/log/README.md#signed-envelope
class SignedNote {
    constructor(note, signatures) {
        this.note = note;
        this.signatures = signatures;
    }
    // Deserialize a SignedNote from a string
    static fromString(envelope) {
        if (!envelope.includes(CHECKPOINT_SEPARATOR)) {
            throw new error_1.VerificationError({
                code: 'TLOG_INCLUSION_PROOF_ERROR',
                message: 'missing checkpoint separator',
            });
        }
        // Split the note into the header and the data portions at the separator
        const split = envelope.indexOf(CHECKPOINT_SEPARATOR);
        const header = envelope.slice(0, split + 1);
        const data = envelope.slice(split + CHECKPOINT_SEPARATOR.length);
        // Find all the signature lines in the data portion
        const matches = data.matchAll(SIGNATURE_REGEX);
        // Parse each of the matched signature lines into the name and signature.
        // The first four bytes of the signature are the key hint (should match the
        // first four bytes of the log ID), and the rest is the signature itself.
        const signatures = Array.from(matches, (match) => {
            const [, name, signature] = match;
            const sigBytes = Buffer.from(signature, 'base64');
            if (sigBytes.length < 5) {
                throw new error_1.VerificationError({
                    code: 'TLOG_INCLUSION_PROOF_ERROR',
                    message: 'malformed checkpoint signature',
                });
            }
            return {
                name,
                keyHint: sigBytes.subarray(0, 4),
                signature: sigBytes.subarray(4),
            };
        });
        if (signatures.length === 0) {
            throw new error_1.VerificationError({
                code: 'TLOG_INCLUSION_PROOF_ERROR',
                message: 'no signatures found in checkpoint',
            });
        }
        return new SignedNote(header, signatures);
    }
}
// LogCheckpoint represents a transparency log checkpoint. Consists of the
// following:
//  - origin: the name of the transparency log
//  - logSize: the size of the log at the time of the checkpoint
//  - logHash: the root hash of the log at the time of the checkpoint
//  - rest: the rest of the checkpoint body, which is a list of log entries
// See:
// https://github.com/transparency-dev/formats/blob/main/log/README.md#checkpoint-body
class LogCheckpoint {
    constructor(origin, logSize, logHash, rest) {
        this.origin = origin;
        this.logSize = logSize;
        this.logHash = logHash;
        this.rest = rest;
    }
    static fromString(note) {
        const lines = note.trimEnd().split('\n');
        if (lines.length < 3) {
            throw new error_1.VerificationError({
                code: 'TLOG_INCLUSION_PROOF_ERROR',
                message: 'too few lines in checkpoint header',
            });
        }
        const origin = lines[0];
        const logSize = BigInt(lines[1]);
        const rootHash = Buffer.from(lines[2], 'base64');
        const rest = lines.slice(3);
        return new LogCheckpoint(origin, logSize, rootHash, rest);
    }
}
