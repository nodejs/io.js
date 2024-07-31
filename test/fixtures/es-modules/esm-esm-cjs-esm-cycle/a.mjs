// a.mjs

try {
  await import('./b.mjs');
  console.log('dynamic import b.mjs did not fail');
} catch (err) {
  console.log('dynamic import b.mjs failed', err);
}

try {
  await import('./d.mjs');
  console.log('dynamic import d.mjs did not fail');
} catch (err) {
  console.log('dynamic import d.mjs failed', err);
}
