if (typeof window === 'undefined' && typeof globalThis !== 'undefined') {
  var window = globalThis;
}
if (typeof document === 'undefined') {
  var document = { currentScript: null };
}
