"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.verify = exports.sign = exports.createVerifier = exports.attest = exports.VerificationError = exports.PolicyError = exports.TUFError = exports.InternalError = exports.DEFAULT_REKOR_URL = exports.DEFAULT_FULCIO_URL = exports.ValidationError = void 0;
/*
Copyright 2022 The Sigstore Authors.

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
var bundle_1 = require("@sigstore/bundle");
Object.defineProperty(exports, "ValidationError", { enumerable: true, get: function () { return bundle_1.ValidationError; } });
var sign_1 = require("@sigstore/sign");
Object.defineProperty(exports, "DEFAULT_FULCIO_URL", { enumerable: true, get: function () { return sign_1.DEFAULT_FULCIO_URL; } });
Object.defineProperty(exports, "DEFAULT_REKOR_URL", { enumerable: true, get: function () { return sign_1.DEFAULT_REKOR_URL; } });
Object.defineProperty(exports, "InternalError", { enumerable: true, get: function () { return sign_1.InternalError; } });
var tuf_1 = require("@sigstore/tuf");
Object.defineProperty(exports, "TUFError", { enumerable: true, get: function () { return tuf_1.TUFError; } });
var verify_1 = require("@sigstore/verify");
Object.defineProperty(exports, "PolicyError", { enumerable: true, get: function () { return verify_1.PolicyError; } });
Object.defineProperty(exports, "VerificationError", { enumerable: true, get: function () { return verify_1.VerificationError; } });
var sigstore_1 = require("./sigstore");
Object.defineProperty(exports, "attest", { enumerable: true, get: function () { return sigstore_1.attest; } });
Object.defineProperty(exports, "createVerifier", { enumerable: true, get: function () { return sigstore_1.createVerifier; } });
Object.defineProperty(exports, "sign", { enumerable: true, get: function () { return sigstore_1.sign; } });
Object.defineProperty(exports, "verify", { enumerable: true, get: function () { return sigstore_1.verify; } });
