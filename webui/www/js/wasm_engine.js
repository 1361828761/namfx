/* Browser audio runtime. It intentionally shares MockEngine's shell semantics
   until the worklet is ready, then mirrors commands into namfx_wasm. */
'use strict';

class WasmEngine extends MockEngine {
  constructor() {
    super();
    this._audioContext = null;
    this._stream = null;
    this._source = null;
    this._node = null;
    this._workletReady = false;
    this._readyResolver = null;
    this._pendingMessages = [];
    this._requestId = 0;
    this._requestWaiters = new Map();
    this._registeredAssets = new Set();
  }

  isDemo() { return false; }
  isWasm() { return true; }

  async init() {
    const probe = await fetch('wasm/namfx_wasm.js', { method: 'HEAD' });
    if (!probe.ok) throw new Error('未找到 namfx.wasm，请先构建 wasm-debug');
    const state = await super.init();
    state.mode = 'wasm';
    state.statusText = '浏览器音频未启动';
    state.engine = { sampleRate: 0, blockSize: 128, audio: false, version: 'wasm-dsp' };
    state.library = {
      models: [{ name: 'n-buna', type: 'Amp', brand: 'Demo', file: 'models/n-buna.nam' }],
      irs: [{ name: 'cab_clean', file: 'irs/cab_clean.wav' }],
    };
    Store.state = state;
    Store.engine = this;
    Store.emit();
    return state;
  }

  start() {
    this._timer = setInterval(() => {
      if (this._node && this._workletReady) {
        this._node.port.postMessage({ type: 'tick' });
      } else {
        Store.applyTick({ levels: { in: 0, out: 0 } });
      }
    }, 100);
  }

  async startAudio() {
    if (this._node) return { ok: true };
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
      return { ok: false, error: '当前浏览器不支持麦克风输入' };
    }
    try {
      this._stream = await navigator.mediaDevices.getUserMedia({
        audio: { channelCount: 1, echoCancellation: false, noiseSuppression: false, autoGainControl: false },
      });
      const AudioContextCtor = window.AudioContext || window.webkitAudioContext;
      this._audioContext = new AudioContextCtor({ latencyHint: 'interactive' });
      await this._audioContext.audioWorklet.addModule('js/namfx_worklet.js');
      const wasmResponse = await fetch('/wasm/namfx_wasm.wasm');
      if (!wasmResponse.ok) throw new Error('无法读取 namfx_wasm.wasm');
      const wasmBinary = await wasmResponse.arrayBuffer();
      this._node = new AudioWorkletNode(this._audioContext, 'namfx-processor', {
        numberOfInputs: 1,
        numberOfOutputs: 1,
        outputChannelCount: [2],
        processorOptions: { wasmBinary },
      });
      this._node.port.onmessage = (event) => this._onWorkletMessage(event.data || {});
      this._readyPromise = new Promise((resolve) => { this._readyResolver = resolve; });
      this._source = this._audioContext.createMediaStreamSource(this._stream);
      this._source.connect(this._node);
      this._node.connect(this._audioContext.destination);
      await this._audioContext.resume();
      await this._readyPromise;
      Store.state.engine.sampleRate = this._audioContext.sampleRate;
      Store.state.engine.audio = 'Browser AudioWorklet';
      Store.state.statusText = '浏览器音频已连接';
      Store.state.connected = true;
      Store.emit();
      await this._loadChainToWorklet();
      return { ok: true };
    } catch (error) {
      await this.stopAudio();
      return { ok: false, error: '浏览器音频启动失败: ' + String(error && error.message || error) };
    }
  }

  async stopAudio() {
    if (this._source) this._source.disconnect();
    if (this._node) this._node.disconnect();
    if (this._stream) this._stream.getTracks().forEach((track) => track.stop());
    if (this._audioContext) await this._audioContext.close();
    this._source = null;
    this._node = null;
    this._stream = null;
    this._audioContext = null;
    this._workletReady = false;
    Store.state.engine.audio = false;
    Store.state.statusText = '浏览器音频已停止';
    Store.emit();
    return { ok: true };
  }

  async cmd(name, args) {
    args = args || {};
    if ((name === 'addModule' || name === 'insertModule') && args.asset) {
      const models = Store.state.library.models || [];
      const irs = Store.state.library.irs || [];
      const match = [...models, ...irs].find((item) => item.name === args.asset || item.file === args.asset);
      if (match) args.asset = match.file;
    }
    if (name === 'savePreset' && args.chainJson) {
      const imported = JSON.parse(args.chainJson);
      if ((imported.chain || []).some((slot) => slot.file)) {
        return { ok: false, error: '浏览器 WASM 版当前不能导入包含 NAM/IR 资产的预设' };
      }
    }

    const result = await super.cmd(name, args);
    if (!result || !result.ok || !this._node || !this._workletReady) return result;
    if (name === 'setParam') {
      return this._send({ type: 'setParam', slot: args.slot, param: args.param, value: args.value });
    }
    if (name === 'setBypass') {
      return this._send({ type: 'setBypass', slot: args.slot, bypass: args.bypass });
    }
    if (name === 'setMix') {
      return this._send({ type: 'setMix', slot: args.slot, mix: args.mix });
    }
    if (name === 'setOutput') {
       const keys = { ingain: 0, master: 1, bass: 2, mid: 3, treble: 4, mute: 5, masterBypass: 6, lowcut: 7, highcut: 8 };
      if (keys[args.key] == null) return result;
      const value = typeof args.value === 'boolean' ? (args.value ? 1 : 0) : Number(args.value);
      return this._send({ type: 'setOutput', key: keys[args.key], value });
    }
    if (name === 'loadPreset' || name === 'insertModule' || name === 'addModule'
        || name === 'removeModule' || name === 'moveModule' || name === 'moveModuleTo'
        || name === 'recallScene' || name === 'undo' || name === 'redo'
        || name === 'applyA' || name === 'applyB') {
      const sendResult = await this._loadChainToWorklet();
      return sendResult && !sendResult.ok ? sendResult : result;
    }
    return result;
  }

  async _loadChainToWorklet() {
    if (!this._node || !this._workletReady) return { ok: true };
    const snapshot = this._snapshotJson(Store.state.currentPreset ? Store.state.currentPreset.name : 'preset');
    const json = JSON.parse(snapshot);
    const files = new Set();
    for (const slot of (json.chain || [])) {
      if (slot.file) files.add(slot.file);
    }
    for (const file of files) {
      if (this._registeredAssets.has(file)) continue;
      const response = await fetch('/' + file);
      if (!response.ok) {
        return { ok: false, error: '无法读取资产 ' + file + '（请先导入）' };
      }
      const bytes = await response.arrayBuffer();
      const assetResult = await this._send({ type: 'registerAsset', name: file, bytes });
      if (!assetResult.ok) return assetResult;
      this._registeredAssets.add(file);
    }
    return this._send({ type: 'loadPreset', text: snapshot });
  }

  async upload(path, file, name) {
    if (!file) return { ok: false, error: '没有文件' };
    const bytes = await file.arrayBuffer();
    const key = /\.nam$/i.test(name) ? 'models/' + name : 'irs/' + name;
    if (this._node && this._workletReady) {
      const result = await this._send({ type: 'registerAsset', name: key, bytes });
      if (!result.ok) return result;
      this._registeredAssets.add(key);
    }
    if (/\.nam$/i.test(name)) {
      Store.state.library.models.push({ name: U.basename(name), type: 'Imported', brand: 'Browser', file: key });
    } else {
      Store.state.library.irs.push({ name: U.basename(name), file: key });
    }
    Store.emit();
    Store.state.msg = '已导入 ' + name + '（浏览器内存资产）';
    return { ok: true };
  }

  _send(message) {
    if (!this._node || !this._workletReady) return Promise.resolve({ ok: true });
    return new Promise((resolve) => {
      const requestId = ++this._requestId;
      this._requestWaiters.set(requestId, resolve);
      this._node.port.postMessage({ ...message, requestId });
    });
  }

  _onWorkletMessage(message) {
    if (message.type === 'ready') {
      this._workletReady = true;
      if (this._readyResolver) this._readyResolver();
      this._readyResolver = null;
      return;
    }
    if (message.type === 'error') {
      Store.patch({ statusText: 'WASM 音频错误', msg: message.error, msgErr: true });
      return;
    }
    if (message.type === 'tick') {
      Store.applyTick({ levels: { in: message.input, out: message.output } });
      return;
    }
    if (message.type === 'result' && this._requestWaiters.has(message.requestId)) {
      const resolve = this._requestWaiters.get(message.requestId);
      this._requestWaiters.delete(message.requestId);
      resolve({ ok: !!message.ok, error: message.error || '' });
    }
  }

  dispose() {
    super.dispose();
    this.stopAudio();
  }
}

window.WasmEngine = WasmEngine;
