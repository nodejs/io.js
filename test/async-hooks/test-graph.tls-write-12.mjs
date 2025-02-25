import { hasCrypto, skip } from '../common/index.mjs';
if (!hasCrypto)
  skip('missing crypto');

import { DEFAULT_MAX_VERSION } from 'tls';

DEFAULT_MAX_VERSION = 'TLSv1.2';

await import('./test-graph.tls-write.mjs');
