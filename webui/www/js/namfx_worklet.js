const MAX_BLOCK_SIZE = 2048;
import createNamfxModule from '../wasm/namfx_wasm.js';

class NamfxProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    this.module = null;
    this.engine = 0;
    this.ready = false;
    this.queue = [];
    this.inL = 0;
    this.inR = 0;
    this.outL = 0;
    this.outR = 0;
    this.levels = 0;
    this.assetPointers = [];
    this.port.onmessage = (event) => this.handleMessage(event.data || {});
    const wasmBinary = options.processorOptions && options.processorOptions.wasmBinary;
    createNamfxModule({ wasmBinary, locateFile: (file) => '/wasm/' + file }).then((module) => {
      this.module = module;
      this.engine = module._namfx_wasm_create();
      module._namfx_wasm_prepare(this.engine, sampleRate, MAX_BLOCK_SIZE);
      const bytes = MAX_BLOCK_SIZE * Float32Array.BYTES_PER_ELEMENT;
      this.inL = module._malloc(bytes);
      this.inR = module._malloc(bytes);
      this.outL = module._malloc(bytes);
      this.outR = module._malloc(bytes);
      this.levels = module._malloc(Float32Array.BYTES_PER_ELEMENT * 2);
      this.ready = true;
      this.port.postMessage({ type: 'ready', sampleRate });
      const pending = this.queue;
      this.queue = [];
      pending.forEach((message) => this.handleMessage(message));
    }).catch((error) => {
      this.port.postMessage({ type: 'error', error: String(error && error.message || error) });
    });
  }

  handleMessage(message) {
    if (!this.ready) {
      this.queue.push(message);
      return;
    }
    const m = this.module;
    const call = (name, returnType, argTypes, args) => m.ccall(name, returnType, argTypes, args);
    if (message.type === 'loadPreset') {
      const result = call('namfx_wasm_load_preset_json', 'number', ['number', 'string', 'number'], [this.engine, message.text, m.lengthBytesUTF8(message.text)]);
      this.port.postMessage({ type: 'result', requestId: message.requestId, ok: result === 0, error: result === 0 ? '' : m.ccall('namfx_wasm_last_error', 'string', ['number'], [this.engine]) });
    } else if (message.type === 'registerAsset') {
      const bytes = message.bytes ? new Uint8Array(message.bytes) : new Uint8Array(0);
      const ptr = m._malloc(Math.max(bytes.length, 1));
      if (bytes.length) m.HEAPU8.set(bytes, ptr);
      this.assetPointers.push(ptr);
      const result = call('namfx_wasm_register_asset', 'number', ['number', 'string', 'number', 'number'], [this.engine, message.name, ptr, bytes.length]);
      this.port.postMessage({ type: 'result', requestId: message.requestId, ok: result === 0, error: result === 0 ? '' : '资产注册失败' });
    } else if (message.type === 'setParam') {
      const result = call('namfx_wasm_set_param', 'number', ['number', 'number', 'string', 'number'], [this.engine, message.slot, message.param, message.value]);
      this.port.postMessage({ type: 'result', requestId: message.requestId, ok: result === 0 });
    } else if (message.type === 'setBypass') {
      const result = call('namfx_wasm_set_bypass', 'number', ['number', 'number', 'number'], [this.engine, message.slot, message.bypass ? 1 : 0]);
      this.port.postMessage({ type: 'result', requestId: message.requestId, ok: result === 0 });
    } else if (message.type === 'setMix') {
      const result = call('namfx_wasm_set_mix', 'number', ['number', 'number', 'number'], [this.engine, message.slot, message.mix]);
      this.port.postMessage({ type: 'result', requestId: message.requestId, ok: result === 0 });
    } else if (message.type === 'setOutput') {
      const result = call('namfx_wasm_set_output', 'number', ['number', 'number', 'number'], [this.engine, message.key, message.value]);
      this.port.postMessage({ type: 'result', requestId: message.requestId, ok: result === 0 });
    } else if (message.type === 'tick') {
      m._namfx_wasm_get_levels(this.engine, this.levels, this.levels + Float32Array.BYTES_PER_ELEMENT);
      const levels = m.HEAPF32.subarray(this.levels >> 2, (this.levels >> 2) + 2);
      this.port.postMessage({ type: 'tick', input: levels[0], output: levels[1] });
    }
  }

  process(inputs, outputs) {
    const output = outputs[0];
    if (!output || !output[0]) return true;
    const frames = output[0].length;
    if (!this.ready || frames > MAX_BLOCK_SIZE) {
      output.forEach((channel) => channel.fill(0));
      return true;
    }

    const input = inputs[0] || [];
    const inputL = input[0];
    const inputR = input[1];
    const heap = this.module.HEAPF32;
    const inL = heap.subarray(this.inL >> 2, (this.inL >> 2) + frames);
    const inR = heap.subarray(this.inR >> 2, (this.inR >> 2) + frames);
    inL.fill(0);
    inR.fill(0);
    if (inputL) inL.set(inputL);
    if (inputR) inR.set(inputR);
    this.module._namfx_wasm_process(this.engine, this.inL, this.inR, this.outL, this.outR, frames);
    const outL = heap.subarray(this.outL >> 2, (this.outL >> 2) + frames);
    const outR = heap.subarray(this.outR >> 2, (this.outR >> 2) + frames);
    output[0].set(outL);
    if (output[1]) output[1].set(outR);
    return true;
  }
}

registerProcessor('namfx-processor', NamfxProcessor);
