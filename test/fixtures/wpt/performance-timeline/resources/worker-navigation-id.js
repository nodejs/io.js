self.onmessage = () => {
  const mark_name = 'user_timig_mark';
  performance.mark(mark_name);
  postMessage(performance.getEntriesByName(mark_name)[0].navigationId);
  self.close();
}
