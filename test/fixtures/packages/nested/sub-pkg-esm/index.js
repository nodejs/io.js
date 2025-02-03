import { findPackageJSON } from 'node:module';

export default findPackageJSON('..', import.meta.url);
