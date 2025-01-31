"use strict";
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.appDataPath = appDataPath;
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
const os_1 = __importDefault(require("os"));
const path_1 = __importDefault(require("path"));
function appDataPath(name) {
    const homedir = os_1.default.homedir();
    switch (process.platform) {
        /* istanbul ignore next */
        case 'darwin': {
            const appSupport = path_1.default.join(homedir, 'Library', 'Application Support');
            return path_1.default.join(appSupport, name);
        }
        /* istanbul ignore next */
        case 'win32': {
            const localAppData = process.env.LOCALAPPDATA || path_1.default.join(homedir, 'AppData', 'Local');
            return path_1.default.join(localAppData, name, 'Data');
        }
        /* istanbul ignore next */
        default: {
            const localData = process.env.XDG_DATA_HOME || path_1.default.join(homedir, '.local', 'share');
            return path_1.default.join(localData, name);
        }
    }
}
