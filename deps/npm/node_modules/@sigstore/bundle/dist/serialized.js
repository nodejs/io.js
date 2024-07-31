"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.envelopeToJSON = exports.envelopeFromJSON = exports.bundleToJSON = exports.bundleFromJSON = void 0;
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
const protobuf_specs_1 = require("@sigstore/protobuf-specs");
const bundle_1 = require("./bundle");
const validate_1 = require("./validate");
const bundleFromJSON = (obj) => {
    const bundle = protobuf_specs_1.Bundle.fromJSON(obj);
    switch (bundle.mediaType) {
        case bundle_1.BUNDLE_V01_MEDIA_TYPE:
            (0, validate_1.assertBundleV01)(bundle);
            break;
        case bundle_1.BUNDLE_V02_MEDIA_TYPE:
            (0, validate_1.assertBundleV02)(bundle);
            break;
        default:
            (0, validate_1.assertBundleLatest)(bundle);
            break;
    }
    return bundle;
};
exports.bundleFromJSON = bundleFromJSON;
const bundleToJSON = (bundle) => {
    return protobuf_specs_1.Bundle.toJSON(bundle);
};
exports.bundleToJSON = bundleToJSON;
const envelopeFromJSON = (obj) => {
    return protobuf_specs_1.Envelope.fromJSON(obj);
};
exports.envelopeFromJSON = envelopeFromJSON;
const envelopeToJSON = (envelope) => {
    return protobuf_specs_1.Envelope.toJSON(envelope);
};
exports.envelopeToJSON = envelopeToJSON;
