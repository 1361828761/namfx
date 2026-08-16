/* NAMFX WebUI — 状态仓库 + 引擎抽象
 * MockEngine：浏览器内模拟（无后端），RemoteEngine：C++ 宿主（POST /api/cmd + SSE /api/events）。
 * 渲染纪律：cmd() 只改状态不 emit；由 UI.send 决定全量渲染或局部刷新；
 * 10Hz 心跳走 Store.applyTick（不重建 DOM，旋钮拖动不被打断）。
 */
'use strict';

/* ---------- 状态仓库 ---------- */
const Store = {
  state: null,
  engine: null,
  _listeners: [],
  _tickListeners: [],
  on(fn) { this._listeners.push(fn); },
  onTick(fn) { this._tickListeners.push(fn); },
  emit() { for (const fn of this._listeners) fn(this.state); },
  applyTick(partial) {
    Object.assign(this.state, partial);
    for (const fn of this._tickListeners) fn(this.state);
  },
  patch(partial) {
    Object.assign(this.state, partial);
    this.emit();
  },
};

/* ---------- 通用工具 ---------- */
const U = {
  esc(s) {
    return String(s == null ? '' : s).replace(/[&<>"']/g,
      c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  },
  fmt(v, unit, digits) {
    if (v == null) return '--';
    const d = digits != null ? digits : (Math.abs(v) >= 100 ? 0 : 1);
    let s = Number(v).toFixed(d);
    if (unit) s += unit;
    return s;
  },
  db(level) {
    if (level < 1e-5) return '-inf';
    return (20 * Math.log10(level)).toFixed(1);
  },
  basename(p) {
    const s = String(p || '').split(/[\\/]/).pop();
    return s.replace(/\.[^.]+$/, '');
  },
};

function presetKind(json) {
  const chain = json && Array.isArray(json.chain) ? json.chain : [];
  if (chain.some(slot => slot.category === 'cab' || slot.module === 'cab.ir' || /\.wav$/i.test(slot.file || ''))) return 'ir';
  if (chain.some(slot => slot.category === 'amp' || slot.module === 'amp.nam' || /\.nam$/i.test(slot.file || ''))) return 'nam';
  return 'dsp';
}

function normalizePresetLabels(state) {
  const all = [...(state.presets.demo || []), ...(state.presets.user || [])];
  all.forEach((preset, index) => {
    const explicit = /^(\d{2})([A-C])[-_]/.exec(preset.file || '');
    preset.label = explicit ? explicit[1] + explicit[2]
      : String(Math.floor(index / 3) + 1).padStart(2, '0') + 'ABC'[index % 3];
  });
  if (state.currentPreset) {
    const current = all.find(p => p.file === state.currentPreset.file && (p.scope || 'demo') === state.currentPreset.scope);
    if (current) state.currentPreset.label = current.label;
  }
}

/* ---------- 默认状态（与 /api/state 同形） ---------- */
function defaultState() {
  return {
    mode: 'demo',
    connected: true,
    statusText: '演示模式',
    catalog: NAMFX_CATALOG,
    presets: { demo: [], user: [] },
    currentPreset: null,        // {file, name, label, scope}
    chain: [],
    chainLayout: Array(12).fill(-1),
    dirty: false,
    // UI 态
    selectedSlot: -1,
    insertIndex: -1,
    modulePreview: null,
    railSel: 'dist',
    chainView: typeof location !== 'undefined' && new URLSearchParams(location.search).get('view') === 'tiles' ? 'tiles' : 'cards',
    sceneSel: null,
    presetSel: null,
    collapsedGroups: {},
    dragSlot: null,
    dropIndex: null,
    dropSlot: null,
    pendingAsset: null,         // {moduleId, name, assetName}
    abActive: null,
    locked: false,
    tab: 'modules',
    scenes: [],                 // {name, overrides:[]}
    activeScene: -1,
     output: { master: 0, ingain: 0, bass: 0.5, mid: 0.5, treble: 0.5, lowcut: 20, highcut: 20000, mute: false, masterBypass: false },
    levels: { in: 0, out: 0 },
    tuner: { on: true, tuning: 0, detected: false, note: '', freq: 0, cents: 0, string: '', target: 0, inTune: false, signal: false },
    perf: { cpu: 8, remaining: 92, xrun: 0, tier: 'Full' },
    midi: { binds: [], learning: null },
    ab: { a: null, b: null },
    undo: { canUndo: false, canRedo: false },
    library: { models: [], irs: [] },
    msg: '',
    msgErr: false,
    engine: { sampleRate: 48000, blockSize: 128, audio: false, version: 'demo' },
  };
}

/* ---------- Mock 引擎（演示/浏览器形态） ---------- */
class MockEngine {
  constructor() {
    this._undoStack = [];
    this._redoStack = [];
    this._presetJson = new Map();
    this._timer = null;
    this._phase = 0;
  }

  isDemo() { return true; }

  async init() {
    const s = defaultState();
    for (const f of NAMFX_DEMO_PRESETS) {
      try {
        const r = await fetch('presets/' + f);
        if (!r.ok) continue;
        const json = await r.json();
        s.presets.demo.push({ file: f, name: json.name || U.basename(f), kind: presetKind(json), label: NAMFX_PRESET_LABEL(f, s.presets.demo.length) });
        this._presetJson.set('demo:' + f, JSON.stringify(json));
      } catch (e) { /* 静态目录下个别缺失可容忍 */ }
    }
    s.library.models = [
      { name: 'n-buna', type: 'Amp', brand: 'BNO', file: 'n-buna.nam' },
      { name: 'Plexi 1959', type: 'Amp', brand: 'Marshall', file: 'plexi59.nam' },
      { name: 'AC30 TB', type: 'Amp', brand: 'Vox', file: 'ac30tb.nam' },
      { name: 'JCM800 2203', type: 'Amp+Cab', brand: 'Marshall', file: 'jcm800_cab.nam' },
      { name: 'TS9', type: 'Pedal', brand: 'Ibanez', file: 'ts9.nam' },
      { name: 'Twin Reverb', type: 'Amp', brand: 'Fender', file: 'twin.nam' },
    ];
    normalizePresetLabels(s);
    s.library.irs = [
      { name: 'cab_clean', file: 'irs/cab_clean.wav' },
      { name: 'V30 57 off-axis', file: 'irs/v30_57.wav' },
      { name: 'Greenback 121', file: 'irs/gb121.wav' },
    ];
    Store.state = s;
    Store.engine = this;
    if (s.presets.demo.length) {
      const p = s.presets.demo[0];
      try {
        const r = await fetch('presets/' + p.file);
        const json = await r.json();
         this._applyPresetJson(json, p, true, true);
      } catch (e) { /* ignore */ }
    }
    Store.emit();
    return s;
  }

  /* ---- 预设装载（JSON → 链/场景） ---- */
  _applyPresetJson(json, p, recordUndo = true, resetCompare = false) {
    const s = Store.state;
    const chain = [];
    for (const slot of (json.chain || [])) {
      const mod = NAMFX_CATALOG.moduleById(slot.module);
      if (!mod) continue;
      const params = {};
      for (const spec of mod.specs) {
        params[spec.id] = slot.params && slot.params[spec.id] != null ? slot.params[spec.id] : spec.def;
      }
      chain.push({
        slot: chain.length,
        module: slot.module,
        name: mod.name,
        category: slot.category || NAMFX_CATALOG.engineCategory(slot.module),
        asset: slot.file || '',
        assetName: slot.file ? U.basename(slot.file) : '',
        bypass: !!slot.bypass,
        mix: slot.mix != null ? slot.mix : 1,
        specs: mod.specs,
        params,
      });
    }
    s.chain = chain;
    this._setLayoutFromChain();
    s.scenes = (json.scenes || []).map(sc => ({
      name: sc.name || '',
      overrides: (sc.overrides || []).map(ov => ({
        moduleId: ov.moduleId,
        bypass: !!ov.bypass,
        params: ov.params || {},
      })),
    }));
    s.activeScene = -1;
    s.dirty = false;
    s.currentPreset = p ? { file: p.file, name: json.name || p.name, label: p.label, scope: p.scope || 'demo' } : null;
    if (resetCompare) {
      s.ab = { a: null, b: null };
      s.abActive = null;
    }
    s.selectedSlot = chain.length ? 0 : -1;
    s.insertIndex = -1;
    s.pendingAsset = null;
    s.modulePreview = null;
    s.msg = p ? '已加载预设 ' + (p.name || p.file) : '';
    s.msgErr = false;
    if (recordUndo) this._pushUndo();
  }

  /* ---- 命令 ---- */
  async cmd(name, args) {
    const s = Store.state;
    args = args || {};
    try {
      switch (name) {
        case 'loadPreset': {
          const list = s.presets[args.scope || 'demo'] || [];
          const p = list.find(x => x.file === args.file);
          if (!p) return { ok: false, error: '未找到预设 ' + args.file };
          try {
            let json;
            const stored = this._presetJson.get((args.scope || 'demo') + ':' + p.file);
            if (stored) {
              json = JSON.parse(stored);
            } else {
              const r = await fetch('presets/' + p.file);
              if (!r.ok) throw new Error('fetch failed');
              json = await r.json();
            }
             this._applyPresetJson(json, p, true, true);
             s.presetSel = p.label;
           } catch (e) {
             return { ok: false, error: '预设文件读取失败' };
           }
          return { ok: true };
        }
        case 'savePreset': {
          const nm = String(args.name || '').trim();
          if (!nm || /[\\/.]/.test(nm)) return { ok: false, error: '预设名不合法（不允许 / \\ .）' };
          let json;
          if (args.chainJson) {
            try { json = JSON.parse(args.chainJson); } catch (e) { return { ok: false, error: '导入 JSON 解析失败' }; }
            if (!json || !Array.isArray(json.chain)) return { ok: false, error: '不是合法预设（缺 chain）' };
            json.name = nm;
          } else {
            json = JSON.parse(this._snapshotJson(nm));
          }
          const label = String(args.label || '').toUpperCase();
          const file = (label ? label + '_' : '') + nm + '.json';
          const p = { file, name: nm, kind: presetKind(json), label: label || NAMFX_PRESET_LABEL(file, s.presets.user.length), scope: 'user' };
          s.presets.user = s.presets.user.filter(x => x.file !== file && !(label && x.label === label));
          s.presets.user.push(p);
          s.presets.user.sort((a, b) => a.file.localeCompare(b.file));
          this._presetJson.set('user:' + file, JSON.stringify(json));
          if (label) this._presetJson.delete('user:' + file.replace(label + '_' + nm, nm));
          normalizePresetLabels(s);
          this._applyPresetJson(json, p);
          s.currentPreset = p;
          s.presetSel = p.label;
          s.msg = '已保存预设 ' + nm + (label ? ' → ' + label : '');
          return { ok: true };
        }
        case 'deletePreset': {
          const list = s.presets[args.scope || 'user'] || [];
          const idx = list.findIndex(x => x.file === args.file);
          if (idx < 0) return { ok: false, error: '未找到预设' };
          if ((args.scope || 'user') === 'demo') return { ok: false, error: '出厂预设不可删除' };
          list.splice(idx, 1);
          this._presetJson.delete((args.scope || 'user') + ':' + args.file);
          if (s.currentPreset && s.currentPreset.file === args.file) {
            s.currentPreset = null;
            this._applyPresetJson({ schema: 1, chain: [], scenes: [] }, null);
            s.dirty = false;
            s.msg = '已删除预设并清空效果链';
          } else {
            s.msg = '已删除预设 ' + args.file;
          }
          normalizePresetLabels(s);
          return { ok: true };
        }
        case 'loadEmpty': {
          this._applyPresetJson({ schema: 1, chain: [], scenes: [] }, null);
          s.currentPreset = null;
          s.dirty = false;
          s.msg = '已清空效果链（空预设槽）';
          return { ok: true };
        }
        case 'exportPreset':
          return { ok: true, text: this._snapshotJson(s.currentPreset ? s.currentPreset.name : 'preset') };
        case 'insertModule':
        case 'addModule': {
          if (s.chain.length >= 12) return { ok: false, error: '音色链已达到 12 个槽位上限' };
          const mod = NAMFX_CATALOG.moduleById(args.moduleId);
          if (!mod) return { ok: false, error: '未知模块 ' + args.moduleId };
          if (mod.asset !== 'none' && !args.asset) return { ok: false, error: '请先选择 ' + (mod.asset === 'nam' ? 'NAM 模型' : 'IR 文件') };
          const params = {};
          for (const spec of mod.specs) params[spec.id] = spec.def;
          const entry = {
            slot: s.chain.length,
            module: mod.id,
            name: mod.name,
            category: NAMFX_CATALOG.engineCategory(mod.id),
            asset: args.asset || '',
            assetName: args.asset ? U.basename(args.asset) : '',
            bypass: false,
            mix: 1,
            specs: mod.specs,
            params,
          };
          const at = (name === 'insertModule' && args.index != null)
            ? Math.min(Math.max(args.index, 0), s.chain.length) : s.chain.length;
          s.chain.splice(at, 0, entry);
          s.chain.forEach((c, i) => { c.slot = i; });
          this._setLayoutFromChain();
          s.selectedSlot = at;
          s.insertIndex = -1;
          s.pendingAsset = null;
          s.modulePreview = null;
          s.dirty = true;
          s.msg = '已添加 ' + mod.name;
          this._pushUndo();
          return { ok: true };
        }
        case 'removeModule': {
          const idx = s.chain.findIndex(c => c.slot === args.slot);
          if (idx < 0) return { ok: false, error: '无此槽位' };
          s.chain.splice(idx, 1);
          s.chain.forEach((c, i) => { c.slot = i; });
          this._setLayoutFromChain();
          s.selectedSlot = -1;
          s.dirty = true;
          s.msg = '已移除槽位 ' + (args.slot + 1);
          this._pushUndo();
          return { ok: true };
        }
        case 'moveModule': {
          const idx = s.chain.findIndex(c => c.slot === args.slot);
          if (idx < 0) return { ok: false, error: '无此槽位' };
          const dst = idx + (args.direction || 0);
          if (dst < 0 || dst >= s.chain.length) return { ok: false, error: '已在边缘' };
          const [m] = s.chain.splice(idx, 1);
          s.chain.splice(dst, 0, m);
          s.chain.forEach((c, i) => { c.slot = i; });
          this._setLayoutFromChain();
          s.dirty = true;
          this._pushUndo();
          return { ok: true };
        }
        case 'moveModuleTo': {
          const idx = s.chain.findIndex(c => c.slot === args.slot);
          if (idx < 0) return { ok: false, error: '无此槽位' };
          const dst = Math.min(Math.max(args.index, 0), s.chain.length - 1);
          const [m] = s.chain.splice(idx, 1);
          s.chain.splice(dst, 0, m);
          s.chain.forEach((c, i) => { c.slot = i; });
          if (args.visualSource != null && args.visualTarget != null
              && s.chainLayout[args.visualSource] >= 0 && s.chainLayout[args.visualTarget] < 0) {
            s.chainLayout[args.visualTarget] = s.chainLayout[args.visualSource];
            s.chainLayout[args.visualSource] = -1;
            this._applyLayoutToChain();
          } else {
            this._setLayoutFromChain();
          }
          s.dirty = true;
          this._pushUndo();
          return { ok: true };
        }
        case 'swapModule': {
          const a = s.chain.findIndex(c => c.slot === args.slot);
          const b = s.chain.findIndex(c => c.slot === args.target);
          if (a < 0 || b < 0 || a === b) return { ok: false, error: '无效的交换槽位' };
          [s.chain[a], s.chain[b]] = [s.chain[b], s.chain[a]];
          s.chain.forEach((c, i) => { c.slot = i; });
          if (args.visualSource != null && args.visualTarget != null) {
            [s.chainLayout[args.visualSource], s.chainLayout[args.visualTarget]] =
              [s.chainLayout[args.visualTarget], s.chainLayout[args.visualSource]];
            this._applyLayoutToChain();
          } else {
            this._setLayoutFromChain();
          }
          s.dirty = true;
          this._pushUndo();
          return { ok: true };
        }
        case 'setParam': {
          const c = s.chain.find(x => x.slot === args.slot);
          if (!c) return { ok: false, error: '无此槽位' };
          const spec = c.specs.find(x => x.id === args.param);
          if (!spec) return { ok: false, error: '未知参数 ' + args.param };
          c.params[args.param] = Math.min(Math.max(args.value, spec.min), spec.max);
          s.dirty = true;
          this._noteEdit();
          return { ok: true };
        }
        case 'setBypass': {
          const c = s.chain.find(x => x.slot === args.slot);
          if (!c) return { ok: false, error: '无此槽位' };
          c.bypass = !!args.bypass;
          s.dirty = true;
          this._noteEdit();
          return { ok: true };
        }
        case 'setMix': {
          const c = s.chain.find(x => x.slot === args.slot);
          if (!c) return { ok: false, error: '无此槽位' };
          c.mix = args.mix == null ? 1 : Math.min(Math.max(args.mix, 0), 1);
          s.dirty = true;
          this._noteEdit();
          return { ok: true };
        }
        case 'recallScene': {
          const sc = s.scenes[args.index];
          if (!sc) return { ok: false, error: '无此场景' };
          for (const ov of sc.overrides) {
            const c = s.chain.find(x => x.module === ov.moduleId);
            if (!c) continue;
            c.bypass = !!ov.bypass;
            for (const k in (ov.params || {})) {
              const spec = c.specs.find(x => x.id === k);
              if (spec) c.params[k] = Math.min(Math.max(ov.params[k], spec.min), spec.max);
            }
          }
          s.activeScene = args.index;
          s.msg = '场景 ' + (args.index + 1) + ' · ' + (sc.name || '');
          this._pushUndo();
          return { ok: true };
        }
        case 'saveScene': {
          const idx = args.index != null ? args.index : s.scenes.length;
          if (idx > s.scenes.length) return { ok: false, error: '场景索引越界' };
          if (idx === s.scenes.length && s.scenes.length >= 8) return { ok: false, error: '场景数已达上限 8' };
          const overrides = s.chain.map(c => ({
            moduleId: c.module,
            bypass: c.bypass,
            params: Object.assign({}, c.params),
          }));
          const sc = { name: (args.name || '').trim() || ('场景 ' + (idx + 1)), overrides };
          if (idx === s.scenes.length) s.scenes.push(sc); else s.scenes[idx] = sc;
          s.sceneSel = idx;
          s.msg = '已存储场景 ' + (idx + 1);
          this._pushUndo();
          return { ok: true };
        }
        case 'setOutput': {
          const o = s.output;
          if (args.key in o) {
            o[args.key] = args.value;
            if (args.key === 'mute') s.msg = o.mute ? '已静音' : '已取消静音';
            if (args.key === 'masterBypass') s.msg = o.masterBypass ? '总旁路开启（干音直通）' : '总旁路关闭';
          }
          return { ok: true };
        }
        case 'setTunerOn': s.tuner.on = !!args.on; return { ok: true };
        case 'setTuning': s.tuner.tuning = args.tuning || 0; return { ok: true };
        case 'setSignal': s.tuner.signal = !!args.on; return { ok: true };
        case 'learnParam': {
          s.midi.learning = { kind: 'param', module: args.module, param: args.param };
          s.msg = '学习中：等待 CC 输入';
          return { ok: true };
        }
        case 'learnScene': {
          s.midi.learning = { kind: 'scene', index: args.index };
          s.msg = '学习中：等待 CC 输入';
          return { ok: true };
        }
        case 'learnCancel': {
          s.midi.learning = null;
          return { ok: true };
        }
        case 'midiLearnParam': {
          if (!s.midi.learning) return { ok: false, error: '未处于学习态' };
          s.midi.binds.push({ cc: args.cc, target: s.midi.learning.module + '.' + s.midi.learning.param });
          s.midi.learning = null;
          s.msg = '已学习 CC ' + args.cc;
          return { ok: true };
        }
        case 'midiLearnScene': {
          if (!s.midi.learning) return { ok: false, error: '未处于学习态' };
          s.midi.binds.push({ cc: args.cc, target: '场景 ' + (s.midi.learning.index + 1) });
          s.midi.learning = null;
          s.msg = '已学习 CC ' + args.cc;
          return { ok: true };
        }
        case 'midiClear': s.midi.binds = []; s.msg = '已清空 MIDI 绑定'; return { ok: true };
        case 'undo': return this._undo();
        case 'redo': return this._redo();
        case 'copyToA': s.ab.a = this._snapshotJson('A'); s.msg = '已复制当前音色到 A'; return { ok: true };
        case 'copyToB': s.ab.b = this._snapshotJson('B'); s.msg = '已复制当前音色到 B'; return { ok: true };
        case 'applyA': {
          if (!s.ab.a) return { ok: false, error: 'A 缓冲为空' };
          this._applySnapshotText(s.ab.a, 'A');
          return { ok: true };
        }
        case 'applyB': {
          if (!s.ab.b) return { ok: false, error: 'B 缓冲为空' };
          this._applySnapshotText(s.ab.b, 'B');
          return { ok: true };
        }
        case 'importModel': {
          const nm = args.name || 'imported.nam';
          s.library.models.push({ name: U.basename(nm), type: 'Amp', brand: 'Imported', file: nm });
          s.msg = '已导入 ' + nm;
          return { ok: true };
        }
        case 'importIr': {
          const nm = args.name || 'imported.wav';
          s.library.irs.push({ name: U.basename(nm), file: nm });
          s.msg = '已导入 ' + nm;
          return { ok: true };
        }
        case 'setLocked': s.locked = !!args.on; return { ok: true };
        default:
          return { ok: false, error: '未知命令 ' + name };
      }
    } catch (e) {
      return { ok: false, error: String(e && e.message || e) };
    }
  }

  /* 当前链 → 预设 JSON 文本 */
  _snapshotJson(name) {
    const s = Store.state;
    return JSON.stringify({
      schema: 1,
      name: name || (s.currentPreset ? s.currentPreset.name : 'preset'),
      chain: s.chain.map(c => ({
        slot: c.slot,
        category: c.category,
        impl: NAMFX_CATALOG.engineImpl(c.module),
        module: c.module,
        file: c.asset || '',
        params: c.params,
        bypass: c.bypass,
        mix: c.mix,
      })),
      scenes: s.scenes,
    }, null, 2);
  }

  _setLayoutFromChain() {
    const s = Store.state;
    s.chainLayout = Array(12).fill(-1);
    s.chain.forEach((c, i) => {
      c.engineSlot = c.slot;
      c.uiSlot = i;
      if (i < 12) s.chainLayout[i] = c.slot;
    });
  }

  _applyLayoutToChain() {
    const s = Store.state;
    for (const c of s.chain) {
      c.engineSlot = c.slot;
      const pos = s.chainLayout.indexOf(c.slot);
      c.uiSlot = pos >= 0 ? pos : c.slot;
    }
  }

  _applySnapshotText(text, abTag) {
    let json;
    try { json = JSON.parse(text); } catch (e) { return; }
    const s = Store.state;
    const current = s.currentPreset;
    const replayPreset = current
      ? { ...current }
      : { file: '', name: json.name || (abTag || 'undo'), label: '', scope: 'user' };
    this._applyPresetJson(json, replayPreset, false);
    if (abTag) {
      s.dirty = true;
      s.msg = '已切换到 ' + abTag + ' 音色';
    }
  }

  _pushUndo() {
    const snap = this._snapshotJson('undo');
    this._undoStack.push(snap);
    if (this._undoStack.length > 50) this._undoStack.shift();
    this._redoStack = [];
    Store.state.undo = { canUndo: this._undoStack.length > 1, canRedo: false };
  }
  _noteEdit() {
    this._redoStack = [];
    Store.state.undo = { canUndo: this._undoStack.length > 1, canRedo: false };
  }
  _undo() {
    if (this._undoStack.length <= 1) return { ok: false, error: '没有可撤销的操作' };
    this._redoStack.push(this._undoStack.pop());
    this._applySnapshotText(this._undoStack[this._undoStack.length - 1]);
    Store.state.undo = { canUndo: this._undoStack.length > 1, canRedo: true };
    return { ok: true };
  }
  _redo() {
    if (!this._redoStack.length) return { ok: false, error: '没有可重做的操作' };
    const snap = this._redoStack.pop();
    this._undoStack.push(snap);
    this._applySnapshotText(snap);
    Store.state.undo = { canUndo: true, canRedo: this._redoStack.length > 0 };
    return { ok: true };
  }

  /* ---- 仿真心跳（10Hz，applyTick 不重建 DOM） ---- */
  start() {
    this._timer = setInterval(() => this._tick(), 100);
  }
  _tick() {
    const s = Store.state;
    const sig = s.tuner.signal && !s.output.mute;
    let inL = s.levels.in * 0.86, outL = s.levels.out * 0.86;
    if (sig) {
      const v = 0.25 + 0.2 * Math.abs(Math.sin(this._phase * 0.13)) + 0.04 * Math.random();
      inL = Math.max(inL, v);
      outL = Math.max(outL, Math.min(1, v * (1.0 + 0.3 * Math.sin(this._phase * 0.05))));
    }
    const tuner = Object.assign({}, s.tuner);
    if (sig && tuner.on) {
      const midi = 40 + (this._phase % 20 < 4 ? 2 : 0);
      const cents = Math.sin(this._phase * 0.21) * 9;
      tuner.detected = true;
      tuner.note = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'][midi % 12] + (Math.floor(midi / 12) - 1);
      tuner.freq = 440 * Math.pow(2, (midi - 69) / 12);
      tuner.cents = cents;
      tuner.inTune = Math.abs(cents) <= 5;
      tuner.string = '5thA2';
      tuner.target = 45;
    } else {
      tuner.detected = false;
      tuner.freq = 0;
      tuner.cents = 0;
      tuner.inTune = false;
      tuner.string = '';
    }
    const target = sig ? 24 + 14 * Math.random() : 6 + 4 * Math.random();
    const cpu = s.perf.cpu * 0.8 + target * 0.2;
    const perf = { cpu, remaining: Math.max(0, Math.round(100 - cpu)), xrun: s.perf.xrun, tier: s.perf.tier };
    Store.applyTick({ levels: { in: inL, out: outL }, tuner, perf });
    this._phase++;
  }
  dispose() {
    if (this._timer) clearInterval(this._timer);
    this._timer = null;
  }
}

/* ---------- Remote 引擎（C++ 宿主） ---------- */
function normalizeState(prev, data) {
  // catalog 由前端单源（catalog.js）；服务端链条目只带 module id，名称/类别在此补齐
  data.catalog = NAMFX_CATALOG;
  for (const c of (data.chain || [])) {
    const mod = NAMFX_CATALOG.moduleById(c.module);
    if (mod) {
      c.name = mod.name;
      c.category = NAMFX_CATALOG.engineCategory(c.module);
    }
    if (c.asset == null) c.asset = '';
    c.engineSlot = c.slot;
  }
  const engineSlots = (data.chain || []).map(c => c.slot);
  let layout = Array.isArray(data.chainLayout) && data.chainLayout.length === 12
    ? data.chainLayout.slice(0, 12).map(v => (typeof v === 'number' && v >= 0 ? v : -1)) : [];
  const layoutSlots = layout.filter(v => v >= 0);
  if (layoutSlots.length !== engineSlots.length
      || layoutSlots.some(s => !engineSlots.includes(s))) {
    layout = Array(12).fill(-1);
    engineSlots.forEach((slot, index) => { if (index < 12) layout[index] = slot; });
  }
  data.chainLayout = layout;
  for (const c of (data.chain || [])) {
    const uiSlot = layout.indexOf(c.slot);
    c.uiSlot = uiSlot >= 0 ? uiSlot : c.engineSlot;
  }
  if (!data.library) data.library = { models: [], irs: [] };
  if (!data.presets) data.presets = { demo: [], user: [] };
  for (const preset of data.presets.demo) preset.scope = 'demo';
  for (const preset of data.presets.user) preset.scope = 'user';
  if (!data.undo) data.undo = { canUndo: false, canRedo: false };
  if (!data.ab) data.ab = { a: null, b: null };
  // 保留纯 UI 态（服务端全量快照不携带）
  if (prev) {
    for (const k of ['tab', 'railSel', 'chainView', 'selectedSlot', 'modulePreview', 'insertIndex',
                     'pendingAsset', 'sceneSel', 'collapsedGroups', 'dragSlot',
                      'dropIndex', 'dropSlot', 'locked', 'abActive']) {
      if (prev[k] !== undefined) data[k] = prev[k];
    }
  }
  // fixed-slot bank: user presets override demo presets on the same ABC label
  if (prev && prev.presetSel !== undefined) data.presetSel = prev.presetSel;
  if (!data.ab.a && !data.ab.b) data.abActive = null;
  return data;
}

class RemoteEngine {
  constructor() {
    this._es = null;
    this._reconnectDelay = 1000;
    this._refreshTimer = null;
  }
  isDemo() { return false; }
  async init() {
    const r = await fetch('/api/state');
    if (!r.ok) throw new Error('无法连接宿主 (/api/state ' + r.status + ')');
    const data = await r.json();
    normalizeState(null, data);
    normalizePresetLabels(data);
    data.mode = 'server';
    data.statusText = '已连接';
    Store.state = data;
    Store.engine = this;
    Store.emit();
    return data;
  }
  start() {
    const connect = () => {
      this._es = new EventSource('/api/events');
      this._es.onopen = () => {
        Store.patch({ connected: true, statusText: '已连接' });
        this._reconnectDelay = 1000;
        this.refreshState();
      };
      this._es.onmessage = (ev) => {
        try {
          const data = JSON.parse(ev.data);
          if (!data || !data.state) return;
          if (data.tick) {
            // 10Hz 心跳：只合并动态字段，不重建 DOM
            Store.applyTick({
              levels: data.state.levels,
              tuner: data.state.tuner,
              perf: data.state.perf,
            });
          } else {
            normalizeState(Store.state, data.state);
            normalizePresetLabels(data.state);
            data.state.statusText = '已连接';
            data.state.connected = true;
            data.state.mode = 'server';
            Store.state = data.state;
            Store.emit();
          }
        } catch (e) { /* 忽略坏帧 */ }
      };
      this._es.onerror = () => {
        Store.patch({ connected: false, statusText: '重连中…' });
        this._es.close();
        setTimeout(connect, this._reconnectDelay);
        this._reconnectDelay = Math.min(this._reconnectDelay * 1.5, 8000);
      };
    };
    connect();
  }
  async cmd(name, args) {
    try {
      const r = await fetch('/api/cmd', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ cmd: name, ...(args || {}) }),
      });
      const result = await r.json();
      if (result && result.ok) this.scheduleRefresh();
      return result;
    } catch (e) {
      Store.patch({ connected: false, statusText: '离线' });
      return { ok: false, error: '宿主无响应: ' + (e && e.message || e) };
    }
  }
  async upload(path, blob, name) {
    const r = await fetch('/api/import?name=' + encodeURIComponent(name), { method: 'PUT', body: blob });
    const result = await r.json();
    if (result && result.ok) this.scheduleRefresh();
    return result;
  }
  scheduleRefresh() {
    if (this._refreshTimer) clearTimeout(this._refreshTimer);
    this._refreshTimer = setTimeout(() => this.refreshState(), 90);
  }
  async refreshState() {
    this._refreshTimer = null;
    try {
      const r = await fetch('/api/state');
      if (!r.ok) return;
      const data = await r.json();
      normalizeState(Store.state, data);
      normalizePresetLabels(data);
      data.mode = 'server';
      data.statusText = '已连接';
      data.connected = true;
      Store.state = data;
      Store.emit();
    } catch (e) {
      /* SSE owns the visible reconnect state. */
    }
  }
  dispose() {
    if (this._es) { this._es.close(); this._es = null; }
    if (this._refreshTimer) clearTimeout(this._refreshTimer);
  }
}

/* ---------- 引擎创建（宿主不可达回退 demo） ---------- */
async function createEngine(preferDemo) {
  const query = new URLSearchParams(location.search);
  const useDemo = preferDemo || query.get('demo') === '1';
  if (useDemo) {
    const e = new MockEngine();
    await e.init();
    return e;
  }
  if (query.get('wasm') !== '0' && typeof WasmEngine !== 'undefined') {
    try {
      const e = new WasmEngine();
      await e.init();
      return e;
    } catch (err) {
      if (query.get('wasm') === '1') {
        const e = new MockEngine();
        await e.init();
        Store.patch({ msg: '未找到浏览器 WASM 音频目标，已回退演示模式' });
        return e;
      }
    }
  }
  try {
    const e = new RemoteEngine();
    await e.init();
    return e;
  } catch (err) {
    const e = new MockEngine();
    await e.init();
    Store.patch({ msg: '未检测到 C++ 宿主，已进入演示模式（?demo=0 强制连接宿主）' });
    return e;
  }
}
