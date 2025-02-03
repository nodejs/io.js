let exportedResolve;
let exportedReject;
const promise = new Promise((resolve, reject) => {
  exportedResolve = resolve;
  exportedReject = reject;
});
export default promise;
export { exportedResolve, exportedReject };
