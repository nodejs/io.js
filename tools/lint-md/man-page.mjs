import { generators, createParser } from '@node-core/api-docs-tooling';
import { read } from 'to-vfile';

export default async function generateManPage(path) {
  const { parseApiDocs } = createParser();
  const parsedApiDocs = await parseApiDocs([read(path)]);
  return generators['man-page'].generate(parsedApiDocs, {});
}
