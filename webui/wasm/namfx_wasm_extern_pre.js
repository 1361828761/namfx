if (typeof globalThis !== 'undefined') {
  if (typeof globalThis.window === 'undefined') globalThis.window = globalThis;
  if (typeof globalThis.document === 'undefined') globalThis.document = { currentScript: null };
}
