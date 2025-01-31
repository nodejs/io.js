// This module is invalid in both ESM and CJS, because
// 'exports' are not defined in ESM, while require cannot be
// redeclared in CJS.
Object.defineProperty(exports, "__esModule", { value: true });
const require = () => {};
