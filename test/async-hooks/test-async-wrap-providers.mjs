// Flags: --expose-internals

import { expectsError } from '../common/index.mjs';
import internalTestBinding from 'internal/test/binding';
import { ok, deepStrictEqual, throws } from 'assert';
import { asyncWrapProviders } from 'async_hooks';

const { internalBinding } = internalTestBinding;
const providers = internalBinding('async_wrap').Providers;

ok(typeof asyncWrapProviders === 'object');
deepStrictEqual(asyncWrapProviders, { __proto__: null, ...providers });

const providerKeys = Object.keys(asyncWrapProviders);
throws(() => {
  asyncWrapProviders[providerKeys[0]] = 'another value';
}, expectsError({
  name: 'TypeError',
}), 'should not allow modify asyncWrap providers');
