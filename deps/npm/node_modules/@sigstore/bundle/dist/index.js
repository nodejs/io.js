"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.isBundleV01 = exports.assertBundleV02 = exports.assertBundleV01 = exports.assertBundleLatest = exports.assertBundle = exports.envelopeToJSON = exports.envelopeFromJSON = exports.bundleToJSON = exports.bundleFromJSON = exports.ValidationError = exports.isBundleWithPublicKey = exports.isBundleWithMessageSignature = exports.isBundleWithDsseEnvelope = exports.isBundleWithCertificateChain = exports.BUNDLE_V03_MEDIA_TYPE = exports.BUNDLE_V03_LEGACY_MEDIA_TYPE = exports.BUNDLE_V02_MEDIA_TYPE = exports.BUNDLE_V01_MEDIA_TYPE = exports.toMessageSignatureBundle = exports.toDSSEBundle = void 0;
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
var build_1 = require("./build");
Object.defineProperty(exports, "toDSSEBundle", { enumerable: true, get: function () { return build_1.toDSSEBundle; } });
Object.defineProperty(exports, "toMessageSignatureBundle", { enumerable: true, get: function () { return build_1.toMessageSignatureBundle; } });
var bundle_1 = require("./bundle");
Object.defineProperty(exports, "BUNDLE_V01_MEDIA_TYPE", { enumerable: true, get: function () { return bundle_1.BUNDLE_V01_MEDIA_TYPE; } });
Object.defineProperty(exports, "BUNDLE_V02_MEDIA_TYPE", { enumerable: true, get: function () { return bundle_1.BUNDLE_V02_MEDIA_TYPE; } });
Object.defineProperty(exports, "BUNDLE_V03_LEGACY_MEDIA_TYPE", { enumerable: true, get: function () { return bundle_1.BUNDLE_V03_LEGACY_MEDIA_TYPE; } });
Object.defineProperty(exports, "BUNDLE_V03_MEDIA_TYPE", { enumerable: true, get: function () { return bundle_1.BUNDLE_V03_MEDIA_TYPE; } });
Object.defineProperty(exports, "isBundleWithCertificateChain", { enumerable: true, get: function () { return bundle_1.isBundleWithCertificateChain; } });
Object.defineProperty(exports, "isBundleWithDsseEnvelope", { enumerable: true, get: function () { return bundle_1.isBundleWithDsseEnvelope; } });
Object.defineProperty(exports, "isBundleWithMessageSignature", { enumerable: true, get: function () { return bundle_1.isBundleWithMessageSignature; } });
Object.defineProperty(exports, "isBundleWithPublicKey", { enumerable: true, get: function () { return bundle_1.isBundleWithPublicKey; } });
var error_1 = require("./error");
Object.defineProperty(exports, "ValidationError", { enumerable: true, get: function () { return error_1.ValidationError; } });
var serialized_1 = require("./serialized");
Object.defineProperty(exports, "bundleFromJSON", { enumerable: true, get: function () { return serialized_1.bundleFromJSON; } });
Object.defineProperty(exports, "bundleToJSON", { enumerable: true, get: function () { return serialized_1.bundleToJSON; } });
Object.defineProperty(exports, "envelopeFromJSON", { enumerable: true, get: function () { return serialized_1.envelopeFromJSON; } });
Object.defineProperty(exports, "envelopeToJSON", { enumerable: true, get: function () { return serialized_1.envelopeToJSON; } });
var validate_1 = require("./validate");
Object.defineProperty(exports, "assertBundle", { enumerable: true, get: function () { return validate_1.assertBundle; } });
Object.defineProperty(exports, "assertBundleLatest", { enumerable: true, get: function () { return validate_1.assertBundleLatest; } });
Object.defineProperty(exports, "assertBundleV01", { enumerable: true, get: function () { return validate_1.assertBundleV01; } });
Object.defineProperty(exports, "assertBundleV02", { enumerable: true, get: function () { return validate_1.assertBundleV02; } });
Object.defineProperty(exports, "isBundleV01", { enumerable: true, get: function () { return validate_1.isBundleV01; } });
