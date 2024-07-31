"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.RekorWitness = exports.DEFAULT_REKOR_URL = void 0;
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
const util_1 = require("../../util");
const client_1 = require("./client");
const entry_1 = require("./entry");
exports.DEFAULT_REKOR_URL = 'https://rekor.sigstore.dev';
class RekorWitness {
    constructor(options) {
        this.entryType = options.entryType;
        this.tlog = new client_1.TLogClient({
            ...options,
            rekorBaseURL: options.rekorBaseURL || /* istanbul ignore next */ exports.DEFAULT_REKOR_URL,
        });
    }
    async testify(content, publicKey) {
        const proposedEntry = (0, entry_1.toProposedEntry)(content, publicKey, this.entryType);
        const entry = await this.tlog.createEntry(proposedEntry);
        return toTransparencyLogEntry(entry);
    }
}
exports.RekorWitness = RekorWitness;
function toTransparencyLogEntry(entry) {
    const logID = Buffer.from(entry.logID, 'hex');
    // Parse entry body so we can extract the kind and version.
    const bodyJSON = util_1.encoding.base64Decode(entry.body);
    const entryBody = JSON.parse(bodyJSON);
    const promise = entry?.verification?.signedEntryTimestamp
        ? inclusionPromise(entry.verification.signedEntryTimestamp)
        : undefined;
    const proof = entry?.verification?.inclusionProof
        ? inclusionProof(entry.verification.inclusionProof)
        : undefined;
    const tlogEntry = {
        logIndex: entry.logIndex.toString(),
        logId: {
            keyId: logID,
        },
        integratedTime: entry.integratedTime.toString(),
        kindVersion: {
            kind: entryBody.kind,
            version: entryBody.apiVersion,
        },
        inclusionPromise: promise,
        inclusionProof: proof,
        canonicalizedBody: Buffer.from(entry.body, 'base64'),
    };
    return {
        tlogEntries: [tlogEntry],
    };
}
function inclusionPromise(promise) {
    return {
        signedEntryTimestamp: Buffer.from(promise, 'base64'),
    };
}
function inclusionProof(proof) {
    return {
        logIndex: proof.logIndex.toString(),
        treeSize: proof.treeSize.toString(),
        rootHash: Buffer.from(proof.rootHash, 'hex'),
        hashes: proof.hashes.map((h) => Buffer.from(h, 'hex')),
        checkpoint: {
            envelope: proof.checkpoint,
        },
    };
}
