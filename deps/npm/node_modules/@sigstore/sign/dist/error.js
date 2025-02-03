"use strict";
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
Object.defineProperty(exports, "__esModule", { value: true });
exports.InternalError = void 0;
exports.internalError = internalError;
const error_1 = require("./external/error");
class InternalError extends Error {
    constructor({ code, message, cause, }) {
        super(message);
        this.name = this.constructor.name;
        this.cause = cause;
        this.code = code;
    }
}
exports.InternalError = InternalError;
function internalError(err, code, message) {
    if (err instanceof error_1.HTTPError) {
        message += ` - ${err.message}`;
    }
    throw new InternalError({
        code: code,
        message: message,
        cause: err,
    });
}
