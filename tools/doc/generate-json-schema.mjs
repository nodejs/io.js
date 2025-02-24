// Flags: --expose-internals

import internal from 'internal/options';
import { writeFileSync } from 'fs';

const schema = internal.generateConfigJsonSchema();
writeFileSync('doc/node-config-schema.json', `${JSON.stringify(schema, null, 2)}\n`);
