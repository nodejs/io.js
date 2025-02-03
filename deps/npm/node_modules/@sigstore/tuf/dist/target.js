"use strict";
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.readTarget = readTarget;
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
const fs_1 = __importDefault(require("fs"));
const error_1 = require("./error");
// Downloads and returns the specified target from the provided TUF Updater.
async function readTarget(tuf, targetPath) {
    const path = await getTargetPath(tuf, targetPath);
    return new Promise((resolve, reject) => {
        fs_1.default.readFile(path, 'utf-8', (err, data) => {
            if (err) {
                reject(new error_1.TUFError({
                    code: 'TUF_READ_TARGET_ERROR',
                    message: `error reading target ${path}`,
                    cause: err,
                }));
            }
            else {
                resolve(data);
            }
        });
    });
}
// Returns the local path to the specified target. If the target is not yet
// cached locally, the provided TUF Updater will be used to download and
// cache the target.
async function getTargetPath(tuf, target) {
    let targetInfo;
    try {
        targetInfo = await tuf.getTargetInfo(target);
    }
    catch (err) {
        throw new error_1.TUFError({
            code: 'TUF_REFRESH_METADATA_ERROR',
            message: 'error refreshing TUF metadata',
            cause: err,
        });
    }
    if (!targetInfo) {
        throw new error_1.TUFError({
            code: 'TUF_FIND_TARGET_ERROR',
            message: `target ${target} not found`,
        });
    }
    let path = await tuf.findCachedTarget(targetInfo);
    // An empty path here means the target has not been cached locally, or is
    // out of date. In either case, we need to download it.
    if (!path) {
        try {
            path = await tuf.downloadTarget(targetInfo);
        }
        catch (err) {
            throw new error_1.TUFError({
                code: 'TUF_DOWNLOAD_TARGET_ERROR',
                message: `error downloading target ${path}`,
                cause: err,
            });
        }
    }
    return path;
}
