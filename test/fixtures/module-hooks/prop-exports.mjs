let prop = 'original';
function getProp() {
  return prop;
};
const namespace = { prop, getProp }
export { prop, getProp, namespace as default };
