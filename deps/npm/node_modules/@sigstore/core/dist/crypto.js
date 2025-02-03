"use strict";
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.createPublicKey = createPublicKey;
exports.digest = digest;
exports.verify = verify;
exports.bufferEqual = bufferEqual;
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
const crypto_1 = __importDefault(require("crypto"));
function createPublicKey(key, type = 'spki') {
    if (typeof key === 'string') {
        return crypto_1.default.createPublicKey(key);
    }
    else {
        return crypto_1.default.createPublicKey({ key, format: 'der', type: type });
    }
}
function digest(algorithm, ...data) {
    const hash = crypto_1.default.createHash(algorithm);
    for (const d of data) {
        hash.update(d);
    }
    return hash.digest();
}
function verify(data, key, signature, algorithm) {
    // The try/catch is to work around an issue in Node 14.x where verify throws
    // an error in some scenarios if the signature is invalid.
    try {
        return crypto_1.default.verify(algorithm, data, key, signature);
    }
    catch (e) {
        /* istanbul ignore next */
        return false;
    }
}
function bufferEqual(a, b) {
    try {
        return crypto_1.default.timingSafeEqual(a, b);
    }
    catch {
        /* istanbul ignore next */
        return false;
    }
}
