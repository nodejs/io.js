"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.verifyMerkleInclusion = void 0;
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
const RFC6962_LEAF_HASH_PREFIX = Buffer.from([0x00]);
const RFC6962_NODE_HASH_PREFIX = Buffer.from([0x01]);
function verifyMerkleInclusion(entry) {
    const inclusionProof = entry.inclusionProof;
    const logIndex = BigInt(inclusionProof.logIndex);
    const treeSize = BigInt(inclusionProof.treeSize);
    if (logIndex < 0n || logIndex >= treeSize) {
        throw new error_1.VerificationError({
            code: 'TLOG_INCLUSION_PROOF_ERROR',
            message: `invalid index: ${logIndex}`,
        });
    }
    // Figure out which subset of hashes corresponds to the inner and border
    // nodes
    const { inner, border } = decompInclProof(logIndex, treeSize);
    if (inclusionProof.hashes.length !== inner + border) {
        throw new error_1.VerificationError({
            code: 'TLOG_INCLUSION_PROOF_ERROR',
            message: 'invalid hash count',
        });
    }
    const innerHashes = inclusionProof.hashes.slice(0, inner);
    const borderHashes = inclusionProof.hashes.slice(inner);
    // The entry's hash is the leaf hash
    const leafHash = hashLeaf(entry.canonicalizedBody);
    // Chain the hashes belonging to the inner and border portions
    const calculatedHash = chainBorderRight(chainInner(leafHash, innerHashes, logIndex), borderHashes);
    // Calculated hash should match the root hash in the inclusion proof
    if (!core_1.crypto.bufferEqual(calculatedHash, inclusionProof.rootHash)) {
        throw new error_1.VerificationError({
            code: 'TLOG_INCLUSION_PROOF_ERROR',
            message: 'calculated root hash does not match inclusion proof',
        });
    }
}
exports.verifyMerkleInclusion = verifyMerkleInclusion;
// Breaks down inclusion proof for a leaf at the specified index in a tree of
// the specified size. The split point is where paths to the index leaf and
// the (size - 1) leaf diverge. Returns lengths of the bottom and upper proof
// parts.
function decompInclProof(index, size) {
    const inner = innerProofSize(index, size);
    const border = onesCount(index >> BigInt(inner));
    return { inner, border };
}
// Computes a subtree hash for a node on or below the tree's right border.
// Assumes the provided proof hashes are ordered from lower to higher levels
// and seed is the initial hash of the node specified by the index.
function chainInner(seed, hashes, index) {
    return hashes.reduce((acc, h, i) => {
        if ((index >> BigInt(i)) & BigInt(1)) {
            return hashChildren(h, acc);
        }
        else {
            return hashChildren(acc, h);
        }
    }, seed);
}
// Computes a subtree hash for nodes along the tree's right border.
function chainBorderRight(seed, hashes) {
    return hashes.reduce((acc, h) => hashChildren(h, acc), seed);
}
function innerProofSize(index, size) {
    return bitLength(index ^ (size - BigInt(1)));
}
// Counts the number of ones in the binary representation of the given number.
// https://en.wikipedia.org/wiki/Hamming_weight
function onesCount(num) {
    return num.toString(2).split('1').length - 1;
}
// Returns the number of bits necessary to represent an integer in binary.
function bitLength(n) {
    if (n === 0n) {
        return 0;
    }
    return n.toString(2).length;
}
// Hashing logic according to RFC6962.
// https://datatracker.ietf.org/doc/html/rfc6962#section-2
function hashChildren(left, right) {
    return core_1.crypto.hash(RFC6962_NODE_HASH_PREFIX, left, right);
}
function hashLeaf(leaf) {
    return core_1.crypto.hash(RFC6962_LEAF_HASH_PREFIX, leaf);
}
