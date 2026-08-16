/* NAMFX WebUI — 渲染与交互
 * 分区：标题栏/应用栏/场景条/预设侧边栏/效果链/下半区(单块库·演出·模型库·MIDI·设置)/调音器
 * 渲染纪律：全量 render() 仅在离散变更后；10Hz 心跳走 UI.onTick（只碰动态 DOM）；
 * 参数拖动走 silent send（只同步旋钮/读数，不重建 DOM）。
 */
'use strict';

function clamp01(value) {
  return Math.max(0, Math.min(1, value));
}

function valueToNorm(value, spec) {
  if (spec.taper === 'log' && spec.min > 0 && spec.max > spec.min) {
    return clamp01(Math.log(value / spec.min) / Math.log(spec.max / spec.min));
  }
  return clamp01((value - spec.min) / (spec.max - spec.min));
}

function normToValue(norm, spec) {
  const n = clamp01(norm);
  if (spec.taper === 'log' && spec.min > 0 && spec.max > spec.min) {
    return spec.min * Math.pow(spec.max / spec.min, n);
  }
  return spec.min + n * (spec.max - spec.min);
}

const UI = {
  els: {},
  _paramQueue: new Map(),
  _paramFlush: null,

  init() {
    const $ = (id) => this.els[id] = document.getElementById(id);
    ['conn-dot', 'conn-text', 'win-controls',
     'ab-preset-label', 'ab-preset-name', 'ab-dirty', 'ab-a', 'ab-b',
     'ab-undo', 'ab-redo', 'ab-tuner', 'ab-audio', 'ab-master-bypass', 'ab-mute',
     'meter-in', 'meter-out', 'meter-in-db', 'meter-out-db',
     'perf-fill', 'perf-text', 'ab-lock', 'ab-settings',
      'scenes', 'ps-body', 'ps-count', 'ps-search', 'ps-new-name', 'ps-save', 'ps-export',
     'ps-import', 'ps-import-file', 'chain', 'lower-tabs', 'lower-body',
     'status-msg', 'status-xrun', 'status-engine',
     'tuner-overlay', 'tuner-note', 'tuner-sub', 'tuner-needle',
     'tuner-close', 'tuner-tuning', 'tuner-mute', 'tuner-sig',
     'modal-root', 'toast-root'].forEach($);

    // 窗口控制（EXE 桥；浏览器形态隐藏）
    const hasBridge = !!window.namfxBridge;
    this.els['win-controls'].classList.toggle('hidden', !hasBridge);
    this.els['win-controls'].addEventListener('click', (e) => {
      const wc = e.target.closest('[data-wc]');
      if (wc && window.namfxBridge) window.namfxBridge.windowControl(wc.dataset.wc);
    });

    document.addEventListener('click', (e) => this.onDocClick(e));
    document.addEventListener('dblclick', (e) => this.onDocDoubleClick(e));
    document.addEventListener('pointerdown', (e) => this.onDocPointerDown(e));
    document.addEventListener('change', (e) => this.onDocChange(e));
    document.addEventListener('input', (e) => this.onDocInput(e));
    document.addEventListener('contextmenu', (e) => this.onDocContext(e));
    document.addEventListener('dragover', (e) => { e.preventDefault(); });
    document.addEventListener('drop', (e) => this.onDocDrop(e));

    this.els['tuner-close'].addEventListener('click', () => this.closeTuner());
    this.els['tuner-tuning'].addEventListener('change', (e) => {
      this.send('setTuning', { tuning: parseInt(e.target.value, 10) });
    });
    this.els['tuner-mute'].addEventListener('change', (e) => {
      this.send('setOutput', { key: 'mute', value: e.target.checked });
    });
    this.els['tuner-sig'].addEventListener('change', (e) => {
      this.send('setSignal', { on: e.target.checked });
    });

    this.els['ab-a'].addEventListener('click', () => this.abPress('A'));
    this.els['ab-b'].addEventListener('click', () => this.abPress('B'));
    this.els['ab-undo'].addEventListener('click', () => this.send('undo'));
    this.els['ab-redo'].addEventListener('click', () => this.send('redo'));
    this.els['ab-tuner'].addEventListener('click', () => this.openTuner());
    this.els['ab-audio'].addEventListener('click', async () => {
      if (!Store.engine || !Store.engine.isWasm || !Store.engine.isWasm()) return;
      const button = this.els['ab-audio'];
      button.disabled = true;
      const result = Store.state.engine.audio
        ? await Store.engine.stopAudio()
        : await Store.engine.startAudio();
      button.disabled = false;
      if (!result || !result.ok) this.toast((result && result.error) || '音频操作失败', 'error');
      else this.toast(Store.state.engine.audio ? '浏览器音频已连接' : '浏览器音频已停止', 'ok');
      this.renderAppBar();
    });
    this.els['ab-master-bypass'].addEventListener('click', () => {
      this.send('setOutput', { key: 'masterBypass', value: !Store.state.output.masterBypass });
    });
    this.els['ab-mute'].addEventListener('click', () => {
      this.send('setOutput', { key: 'mute', value: !Store.state.output.mute });
    });
    this.els['ab-lock'].addEventListener('click', () => {
      const locked = !Store.state.locked;
      this.send('setLocked', { on: locked });
      document.body.classList.toggle('locked', locked);
    });
    this.els['ab-settings'].addEventListener('click', () => this.setTab('settings'));

    this.els['ps-save'].addEventListener('click', () => this.savePreset());
    this.els['ps-new-name'].addEventListener('keydown', (e) => { if (e.key === 'Enter') this.savePreset(); });
    this.els['ps-export'].addEventListener('click', () => this.exportPreset());
    this.els['ps-import'].addEventListener('click', () => this.els['ps-import-file'].click());
    this.els['ps-import-file'].addEventListener('change', (e) => this.importPresetFile(e.target.files[0]));
    this.els['ps-search'].addEventListener('input', () => this.renderSidebar());

    this.els['lower-tabs'].addEventListener('click', (e) => {
      const b = e.target.closest('[data-tab]');
      if (b) this.setTab(b.dataset.tab);
    });

    Store.on(() => { this.render(); });
    Store.onTick(() => { this.onTick(); });
  },

  /* ---------- 工具 ---------- */
  el(html) {
    const t = document.createElement('template');
    t.innerHTML = html.trim();
    return t.content.firstElementChild;
  },

  /* 命令下发：silent 命令只做局部同步（旋钮拖动不重建 DOM） */
  send(cmd, args) {
    const silent = cmd === 'setParam'
      || cmd === 'setMix'
      || (cmd === 'setOutput' && args && args.key !== 'mute' && args.key !== 'masterBypass');
    const p = Store.engine.cmd(cmd, args);
    if (p && p.then) {
      return p.then((res) => {
        if (res && !res.ok) {
          this.toast(res.error || '操作失败', 'error');
          Store.state.msg = res.error; Store.state.msgErr = true;
          this.renderStatusBar();
        } else if (!silent) {
          Store.emit();
        } else {
          syncKnobValues();
          this.renderAppBar();
          this.renderScenes();
        }
        return res;
      });
    }
    if (!silent) Store.emit();
    return Promise.resolve({ ok: true });
  },

  queueParam(args) {
    const key = String(args.slot) + ':' + args.param;
    this._paramQueue.set(key, args);
    if (this._paramFlush) return;
    this._paramFlush = setTimeout(() => {
      const pending = [...this._paramQueue.values()];
      this._paramQueue.clear();
      this._paramFlush = null;
      pending.forEach((item) => this.send('setParam', item));
    }, 16);
  },

  toast(msg, type) {
    const t = this.el(`<div class="toast ${type === 'error' ? 'error' : type === 'ok' ? 'ok' : ''}">${U.esc(msg)}</div>`);
    this.els['toast-root'].appendChild(t);
    setTimeout(() => { t.style.opacity = '0'; t.style.transition = 'opacity .3s'; }, 2600);
    setTimeout(() => t.remove(), 3000);
  },

  modal(html) {
    const mask = this.el(`<div class="modal-mask">${html}</div>`);
    mask.addEventListener('click', (e) => { if (e.target === mask) mask.remove(); });
    this.els['modal-root'].appendChild(mask);
    return mask;
  },

  setTab(tab) {
    Store.patch({ tab });
  },

  /* ---------- 全量渲染 ---------- */
  render() {
    this.renderTitleBar();
    this.renderAppBar();
    this.renderScenes();
    this.renderSidebar();
    this.renderChain();
    this.renderLower();
    this.renderStatusBar();
    if (!this.els['tuner-overlay'].classList.contains('hidden')) this.renderTuner();
    afterRender();
  },

  /* ---------- 10Hz 快路径（不重建 DOM） ---------- */
  onTick() {
    const s = Store.state;
    this.setMeter(this.els['meter-in'], s.levels.in);
    this.setMeter(this.els['meter-out'], s.levels.out);
    this.els['meter-in-db'].textContent = U.db(s.levels.in) + ' dB';
    this.els['meter-out-db'].textContent = U.db(s.levels.out) + ' dB';
    const olIn = document.getElementById('ol-in');
    const olOut = document.getElementById('ol-out');
    if (olIn) this.setMeter(olIn, s.levels.in);
    if (olOut) this.setMeter(olOut, s.levels.out);
    const fill = this.els['perf-fill'];
    fill.style.width = Math.max(0, Math.min(100, s.perf.remaining)) + '%';
    fill.className = 'fill' + (s.perf.remaining < 20 ? ' danger' : s.perf.remaining < 50 ? ' warn' : '');
    this.els['perf-text'].textContent = Math.round(s.perf.remaining) + '%';
    if (s.perf.xrun) this.els['status-xrun'].textContent = 'xrun ' + s.perf.xrun;
    if (!this.els['tuner-overlay'].classList.contains('hidden')) this.renderTuner();
  },

  renderTitleBar() {
    const s = Store.state;
    const dot = this.els['conn-dot'];
    dot.className = 'conn-dot';
    if (!s.connected) {
      dot.classList.add(s.statusText && s.statusText.indexOf('重连') >= 0 ? 'reconnecting' : 'offline');
    }
    this.els['conn-text'].textContent = s.statusText || '';
  },

  renderAppBar() {
    const s = Store.state, els = this.els;
    els['ab-preset-label'].textContent = s.currentPreset ? s.currentPreset.label : '--';
    els['ab-preset-name'].textContent = s.currentPreset ? s.currentPreset.name : '未加载';
    els['ab-dirty'].classList.toggle('hidden', !s.dirty);
    els['ab-undo'].disabled = !s.undo.canUndo;
    els['ab-redo'].disabled = !s.undo.canRedo;
    els['ab-a'].classList.toggle('active', !!s.ab.a);
    els['ab-b'].classList.toggle('active', !!s.ab.b);
    els['ab-a'].classList.toggle('selected', s.abActive === 'A');
    els['ab-b'].classList.toggle('selected', s.abActive === 'B');
    const audioButton = els['ab-audio'];
    const wasm = s.mode === 'wasm' && Store.engine && Store.engine.isWasm && Store.engine.isWasm();
    audioButton.classList.toggle('hidden', !wasm);
    if (wasm) {
      audioButton.textContent = s.engine.audio ? '停止音频' : '启动音频';
      audioButton.classList.toggle('active', !!s.engine.audio);
    }
    els['ab-master-bypass'].classList.toggle('on', !!s.output.masterBypass);
    els['ab-mute'].classList.toggle('on', !!s.output.mute);
    els['ab-mute'].classList.toggle('warn', !!s.output.mute);
    const bypassInput = els['ab-master-bypass'].querySelector('input');
    const muteInput = els['ab-mute'].querySelector('input');
    if (bypassInput) bypassInput.checked = !!s.output.masterBypass;
    if (muteInput) muteInput.checked = !!s.output.mute;
    this.setMeter(els['meter-in'], s.levels.in);
    this.setMeter(els['meter-out'], s.levels.out);
    els['meter-in-db'].textContent = U.db(s.levels.in) + ' dB';
    els['meter-out-db'].textContent = U.db(s.levels.out) + ' dB';
    const rem = s.perf.remaining;
    const fill = els['perf-fill'];
    fill.style.width = Math.max(0, Math.min(100, rem)) + '%';
    fill.className = 'fill' + (rem < 20 ? ' danger' : rem < 50 ? ' warn' : '');
    els['perf-text'].textContent = Math.round(rem) + '%';
  },

  setMeter(el, level) {
    el.style.width = Math.max(0, Math.min(100, level * 100)) + '%';
    el.classList.toggle('clip', level > 0.98);
  },

  /* ---------- 场景条 ---------- */
  renderScenes() {
    const s = Store.state;
    const wrap = this.els['scenes'];
    let html = '<span class="sc-label">场景 SCENES</span>';
    const diffCount = (sc) => {
      let n = 0;
      for (const ov of (sc.overrides || [])) {
        const c = s.chain.find(x => x.module === ov.moduleId);
        if (!c) { n++; continue; }
        if (!!c.bypass !== !!ov.bypass) n++;
        for (const k in (ov.params || {})) {
          if (Math.abs((c.params[k] || 0) - ov.params[k]) > 0.001) n++;
        }
      }
      return n;
    };
    for (let i = 0; i < 8; i++) {
      const sc = s.scenes[i];
      const active = s.activeScene === i;
      const sel = s.sceneSel === i;
      const learning = s.midi.learning && s.midi.learning.kind === 'scene' && s.midi.learning.index === i;
      const hasDiff = sc && diffCount(sc) > 0 && !active;
      html += `<button class="scene-btn ${active ? 'active' : ''} ${sel ? 'scene-sel' : ''} ${learning ? 'learning' : ''}"
        data-action="scene" data-index="${i}" title="${sc ? U.esc(sc.name || '') : '空槽：点「存储」把当前音色存入'}">
        <span class="snum">${i + 1}</span>
        <span class="sname">${sc ? U.esc(sc.name) : '空'}</span>
        ${hasDiff ? '<span class="sdiff"></span>' : ''}
      </button>`;
    }
    html += `<input id="sc-name" class="sc-name-input" type="text" placeholder="场景名（≤8字）" maxlength="8"
      value="${s.sceneSel != null && s.scenes[s.sceneSel] ? U.esc(s.scenes[s.sceneSel].name) : ''}">`;
    html += `<button id="sc-save" data-action="scene-save" title="将当前音色状态存储到所选场景">存储</button>`;
    html += `<button id="sc-learn" data-action="learn-scene" title="MIDI 学习绑定到所选场景">L CC</button>`;
    html += `<span class="sc-label">${s.activeScene >= 0 ? '（当前场景 ' + (s.activeScene + 1) + '）' : '（点击槽位选中，再点「存储」）'}</span>`;
    wrap.innerHTML = html;
  },

  /* ---------- 预设侧边栏（固定 50 组 × ABC = 150 槽） ---------- */
  renderSidebar() {
    const s = Store.state;
    const body = this.els['ps-body'];
    const bank = new Map();
    for (const p of (s.presets.demo || [])) bank.set(p.label, { ...p, scope: 'demo', removable: false });
    for (const p of (s.presets.user || [])) bank.set(p.label, { ...p, scope: 'user', removable: true });
    this.els['ps-count'].textContent = bank.size + '/150';
    const collapsed = s.collapsedGroups || {};
    const query = (this.els['ps-search'] ? this.els['ps-search'].value : '').trim().toLowerCase();
    const badge = (preset) => {
      const b = [];
      if (preset.kind === 'nam') b.push('<span class="pi-badge nam">NAM</span>');
      if (preset.kind === 'ir') b.push('<span class="pi-badge ir">IR</span>');
      return b.join('');
    };
    const slotHTML = (label) => {
      const p = bank.get(label);
      const sel = s.currentPreset && s.currentPreset.label === label;
      const pick = s.presetSel === label && !sel;
      if (p) {
        const matched = !query || (p.name + ' ' + label).toLowerCase().indexOf(query) >= 0;
        if (!matched) return '';
        return `<div class="preset-item ${sel ? 'sel' : ''}" data-action="load-preset"
          data-file="${U.esc(p.file)}" data-scope="${p.scope}" data-label="${label}" title="${U.esc(p.name)}">
          <span class="pi-label">${label}</span>
          <span class="pi-name">${U.esc(p.name)}</span>
          ${badge(p)}
          ${p.removable ? `<button class="pi-del" data-action="del-preset" data-file="${U.esc(p.file)}" title="删除">&#x2715;</button>` : ''}
        </div>`;
      }
      if (query && label.indexOf(query) < 0) return '';
      return `<div class="preset-item empty ${pick ? 'pick' : ''}" data-action="pick-empty" data-label="${label}"
        title="空槽：编辑音色后点保存写入此槽">
        <span class="pi-label">${label}</span>
        <span class="pi-name">${pick ? '→ 保存到此' : '空'}</span>
      </div>`;
    };
    let html = '';
    for (let g = 1; g <= 50; g++) {
      const gg = String(g).padStart(2, '0');
      const items = ['A', 'B', 'C'].map(ch => slotHTML(gg + ch)).filter(Boolean).join('');
      if (!items) continue;
      const isCollapsed = collapsed[gg];
      html += `<div class="preset-group">
        <div class="pg-head ${isCollapsed ? 'collapsed' : ''}" data-action="toggle-group" data-group="${gg}">
          <span class="pg-arrow">&#x25BC;</span><span>${gg}</span>
          <span class="pg-count">${bank.has(gg + 'A') ? 1 : 0}${bank.has(gg + 'B') ? 1 : 0}${bank.has(gg + 'C') ? 1 : 0}/3</span>
        </div>`;
      if (!isCollapsed) html += items;
      html += '</div>';
    }
    if (!html) {
      body.innerHTML = '<div class="ps-empty">没有匹配的预设<br>点击空槽编辑音色后保存</div>';
      return;
    }
    body.innerHTML = html;
  },

  /* ---------- 效果链 ---------- */
  renderChain() {
    const s = Store.state;
    const wrap = this.els['chain'];
    const pathLive = !s.output.mute && !s.output.masterBypass;
    const emptyCell = (i) => {
      const pending = s.insertIndex === i;
      const target = s.dropSlot === i ? ' drop-target' : '';
      return `<div class="chain-cell chain-empty-slot add ${pending ? 'pending' : ''}${target}" data-action="insert-at" data-index="${i}"
        data-slot="${i}" title="在第 ${i + 1} 个槽位插入模块"><span>${pending ? '×' : '+'}</span><small>${String(i + 1).padStart(2, '0')}</small></div>`;
    };
    const cell = (i) => s.chain.find((item) => (item.uiSlot != null ? item.uiSlot : item.slot) === i) || null;
    const lane = (indices, name) => `<div class="chain-lane ${name}">${indices.map((i) => {
      const item = cell(i);
      return item ? this.chainCellHTML(item) : emptyCell(i);
    }).join('')}</div>`;
    document.querySelectorAll('[data-action="chain-view"]').forEach((button) => {
      button.classList.toggle('active', button.dataset.view === (s.chainView || 'cards'));
    });
    wrap.innerHTML = `<div class="chain-stage">
      <div class="chain-port input">IN</div>
      <div class="chain-port output">OUT</div>
      <div class="chain-grid ${pathLive ? 'live' : 'dim'}">
        <div class="chain-route" aria-hidden="true">
          <span class="route-line route-top"></span><span class="route-line route-right"></span>
          <span class="route-line route-middle"></span><span class="route-line route-left"></span>
          <span class="route-line route-bottom"></span>
        </div>
        ${lane([0, 1, 2, 3, 4, 5], 'top-lane')}
        ${lane([6, 7, 8, 9, 10, 11], 'bottom-lane')}
      </div>
    </div>`;
  },

  chainCellHTML(c) {
    const engineSlot = c.engineSlot != null ? c.engineSlot : c.slot;
    const visualSlot = c.uiSlot != null ? c.uiSlot : c.slot;
    const sel = Store.state.selectedSlot === engineSlot;
    const dragging = Store.state.dragSlot === visualSlot;
    const dropSlot = Store.state.dropSlot;
    const cur = Store.state.chain.findIndex(x => (x.uiSlot != null ? x.uiSlot : x.slot) === visualSlot);
    const dropCls = dropSlot === visualSlot ? 'drop-target' : '';
    const catClass = c.category === 'cab' ? 'cab' : c.category === 'amp' ? 'amp' : 'pedal';
    const catName = c.category === 'cab' ? 'CAB' : c.category === 'amp' ? 'AMP' : 'PED';
    const viewClass = Store.state.chainView === 'tiles' ? 'tile' : 'card';
    const group = Store.state.catalog.groupOf(c.module);
    const glyph = { dist: 'OD', comp: 'CP', mod: 'MD', dly: 'DL', rvb: 'RV', eq: 'EQ', gate: 'GT', pitch: 'PT', amp: 'AMP', cab: 'IR', util: 'FX' }[group ? group.id : 'util'] || 'FX';
    return `<div class="chain-cell ${viewClass} ${catClass} ${c.bypass ? 'bypassed' : ''} ${sel ? 'sel' : ''} ${dragging ? 'dragging' : ''} ${dropCls}"
      data-action="select-slot" data-slot="${visualSlot}" data-engine-slot="${engineSlot}" title="单击编辑，双击${c.bypass ? '启用' : '旁路'}">
       <div class="cc-icon" data-grip="${visualSlot}" data-engine-slot="${engineSlot}" aria-label="拖动重排"><span>${glyph}</span></div>
      <div class="cc-top">
        <span class="cc-led ${c.bypass ? '' : 'on'}"></span>
        <span class="cc-slot">${String(visualSlot + 1).padStart(2, '0')}</span>
        <span class="cc-name" title="${U.esc(c.name)}">${U.esc(c.name)}</span>
        <span class="cc-grip" data-grip="${visualSlot}" data-engine-slot="${engineSlot}" title="拖动重排">&#x2237;</span>
      </div>
      <div class="cc-asset">${c.assetName
        ? `<span class="chip">${c.category === 'cab' ? 'IR' : 'NAM'}</span>${U.esc(c.assetName)}`
        : '<span class="chip">DSP</span>内置算法'}</div>
      <div class="cc-bottom">
        <span class="cc-cat ${catClass}">${catName}</span>
        <div class="cc-actions">
          <button data-action="move" data-slot="${visualSlot}" data-engine-slot="${engineSlot}" data-dir="-1" title="上移">&#x25B2;</button>
          <button data-action="move" data-slot="${visualSlot}" data-engine-slot="${engineSlot}" data-dir="1" title="下移">&#x25BC;</button>
          <button class="del" data-action="del-slot" data-slot="${visualSlot}" data-engine-slot="${engineSlot}" title="删除">&#x2715;</button>
        </div>
      </div>
    </div>`;
  },

  /* ---------- 下半区 ---------- */
  renderLower() {
    const tabs = this.els['lower-tabs'];
    for (const b of tabs.querySelectorAll('button')) {
      b.classList.toggle('active', b.dataset.tab === Store.state.tab);
    }
    const body = this.els['lower-body'];
    switch (Store.state.tab) {
      case 'output': body.innerHTML = this.outputViewHTML(); break;
      case 'library': body.innerHTML = this.libraryViewHTML(); break;
      case 'midi': body.innerHTML = this.midiViewHTML(); break;
      case 'settings': body.innerHTML = this.settingsViewHTML(); break;
      default: this.renderModulesView(body);
    }
  },

  renderModulesView(body) {
    const s = Store.state;
    let html = `<div class="rail">`;
    for (const g of s.catalog.groups) {
      const active = s.railSel === g.id;
      html += `<div class="rail-item ${active ? 'active' : ''}" data-action="rail" data-group="${g.id}">
        <span class="ri-cn">${U.esc(g.name)}</span>
        <span class="ri-en">${U.esc(g.en)}</span>
      </div>`;
    }
    html += `</div>`;
    const group = s.catalog.groups.find(g => g.id === s.railSel) || s.catalog.groups[0];
    html += `<div class="modlist">`;
    for (const m of group.modules) {
      const sel = s.modulePreview === m.id;
      const inUse = s.chain.some(c => c.module === m.id);
      html += `<div class="mod-item ${sel ? 'sel' : ''}" data-action="preview-module" data-module="${m.id}">
        <div class="mi-name">${U.esc(m.name)}
          ${m.asset === 'nam' ? '<span class="mi-badge nam">NAM</span>'
            : m.asset === 'ir' ? '<span class="mi-badge ir">IR</span>' : ''}
        </div>
        <div class="mi-desc">${U.esc(m.desc)}</div>
        ${inUse ? '<div class="mi-inuse">&#9679; 已在链中</div>' : ''}
      </div>`;
    }
    html += `</div>`;
    html += `<div class="param-panel">${this.paramPanelHTML()}</div>`;
    body.innerHTML = html;
  },

  paramPanelHTML() {
    const s = Store.state;
    if (s.pendingAsset) return this.assetPickerHTML(s.pendingAsset);
    const c = s.chain.find(x => x.slot === s.selectedSlot);
    if (c) return this.moduleEditHTML(c);
    if (s.modulePreview) {
      const m = s.catalog.moduleById(s.modulePreview);
      if (m) return this.modulePreviewHTML(m);
    }
    return `<div class="pp-empty"><span class="pe-icon">&#x266B;</span>
      在左侧选择一个分类与模块，或点击上方效果链中的模块格子编辑参数</div>`;
  },

  moduleEditHTML(c) {
    const s = Store.state;
    let html = '';
    if (s.insertIndex >= 0) {
      html += `<div class="pp-banner">将在效果链位置 <b>${s.insertIndex + 1}</b> 插入模块
        <button data-action="cancel-insert">取消插入</button></div>`;
    }
    const catName = c.category === 'cab' ? '箱体' : c.category === 'amp' ? '箱头' : '单块';
    html += `<div class="pp-head">
      <span class="pph-slot">${String(c.slot + 1).padStart(2, '0')}</span>
      <span class="pph-name">${U.esc(c.name)}</span>
      <span class="pph-sub">${catName} · ${c.assetName ? U.esc(c.assetName) : '内置算法'}</span>
      <div class="pph-mix" title="模块干湿比">
        <span>MIX</span>
        <input type="range" min="0" max="1" step="0.01" value="${c.mix}" data-action="module-mix" data-slot="${c.slot}">
        <b>${U.fmt(c.mix * 100, '%', 0)}</b>
      </div>
      <span class="pph-spacer"></span>
      <label class="ab-toggle ${c.bypass ? '' : 'on'}" title="模块启用开关（交叉淡化防爆音）">
        <input type="checkbox" data-action="bypass" data-slot="${c.slot}" ${c.bypass ? '' : 'checked'}>启用
      </label>
      <div class="cc-actions">
        <button data-action="move" data-slot="${c.slot}" data-dir="-1" title="上移">&#x25B2;</button>
        <button data-action="move" data-slot="${c.slot}" data-dir="1" title="下移">&#x25BC;</button>
        <button class="del" data-action="del-slot" data-slot="${c.slot}" title="删除">&#x2715;</button>
      </div>
    </div>`;
    html += `<div class="pp-body"><div class="knob-grid">`;
    for (const spec of c.specs) {
      const v = c.params[spec.id] != null ? c.params[spec.id] : spec.def;
      const learning = s.midi.learning && s.midi.learning.kind === 'param'
        && s.midi.learning.module === c.module && s.midi.learning.param === spec.id;
      html += `<div class="knob-cell ${learning ? 'learning' : ''}" data-param="${spec.id}" data-slot="${c.slot}" data-module="${c.module}">
        <div class="kc-name">${U.esc(spec.name || spec.id)}</div>
        <div data-knob="${spec.id}" data-slot="${c.slot}"></div>
        <div class="kc-value"><span class="kc-val">${U.fmt(v, spec.unit)}</span></div>
        <div class="kc-tools">
          <button data-action="param-reset" title="重置默认值">R</button>
          <button data-action="param-learn" title="MIDI 学习">L</button>
        </div>
      </div>`;
    }
    html += `</div></div>`;
    return html;
  },

  modulePreviewHTML(m) {
    let html = `<div class="pp-head">
      <span class="pph-name">${U.esc(m.name)}</span>
      <span class="pph-sub">${U.esc(m.desc)}</span>
      <span class="pph-spacer"></span>
    </div>
    <div class="pp-body">`;
    if (m.asset === 'none') {
      html += `<button class="pp-add-btn" data-action="quick-add" data-module="${m.id}" style="padding:9px 24px;font-size:14px">
        &#x2795; ${Store.state.insertIndex >= 0 ? '插入到效果链' : '添加到效果链'}</button>
        <p style="font-size:11.5px;color:var(--text-dim);margin-top:12px;line-height:1.6">参数规格来自引擎注册表（min/max/默认/单位/taper）。添加后可在链上点击该模块编辑参数。</p>`;
    } else {
      html += this.assetPickerHTML({ moduleId: m.id, name: m.name });
    }
    html += `</div>`;
    return html;
  },

  assetPickerHTML(pa) {
    const s = Store.state;
    const m = s.catalog.moduleById(pa.moduleId);
    const wantNam = m.asset === 'nam';
    const list = wantNam ? s.library.models : (s.library.irs || []);
    const selName = pa.assetName || '';
    let html = `<div class="pp-head">
      <span class="pph-name">${U.esc(m.name)}</span>
      <span class="pph-sub">${wantNam ? '选择 NAM 模型 (.nam)' : '选择 IR 文件 (.wav)'}</span>
      <span class="pph-spacer"></span>
    </div>
    <div class="pp-body"><div class="asset-picker">
      <div class="ap-title">${wantNam ? '模型库' : 'IR 库'} — 点击选择</div>
      <div class="ap-list">`;
    if (!list.length) {
      html += `<div class="midi-empty">库为空 — 点「导入文件…」添加${wantNam ? ' .nam 模型' : ' IR'}，或切到「模型库」页导入。</div>`;
    }
    for (const a of list) {
      const meta = wantNam ? (a.type || '') + (a.brand ? ' / ' + a.brand : '') : 'IR';
      html += `<div class="asset-row ${a.name === selName ? 'sel' : ''}" data-action="pick-asset" data-asset="${U.esc(a.name)}">
        <span class="ar-name">${U.esc(a.name)}</span><span class="ar-meta">${U.esc(meta)}</span>
      </div>`;
    }
    html += `</div>
      <div class="ap-actions">
        <button data-action="do-add" data-module="${m.id}" ${selName ? '' : 'disabled'}>${s.insertIndex >= 0 ? '插入到链' : '添加到链'}</button>
        <span class="ap-current">${selName ? '已选：' + U.esc(selName) : '请选择' + (wantNam ? '模型' : 'IR')}</span>
        <button data-action="import-asset" data-kind="${wantNam ? 'nam' : 'ir'}">导入文件…</button>
        <button data-action="cancel-insert">取消</button>
      </div>
    </div></div>`;
    return html;
  },

  /* ---- 演出面板 ---- */
  outputViewHTML() {
    const o = Store.state.output;
    const s = Store.state;
    const slider = (label, key, min, max, step, val, unit) => `
      <div class="out-row">
        <span class="or-label">${label}</span>
        <input type="range" min="${min}" max="${max}" step="${step}" value="${val}" data-action="out-slider" data-key="${key}">
        <span class="or-unit">${U.fmt(val, unit)}</span>
      </div>`;
    return `<div class="out-panel">
      <div class="panel-title">演出层 OUTPUT</div>
      ${slider('Master 主音量', 'master', -60, 0, 0.5, o.master, 'dB')}
      ${slider('In Gain 输入增益', 'ingain', -60, 24, 0.5, o.ingain, 'dB')}
       ${slider('Bass 低频', 'bass', 0, 1, 0.01, o.bass, '')}
       ${slider('Mid 中频', 'mid', 0, 1, 0.01, o.mid, '')}
       ${slider('Treble 高频', 'treble', 0, 1, 0.01, o.treble, '')}
       ${slider('Low Cut 低切', 'lowcut', 20, 1000, 1, o.lowcut, 'Hz')}
       ${slider('High Cut 高切', 'highcut', 2000, 20000, 10, o.highcut, 'Hz')}
      <div class="out-toggles">
        <label class="ab-toggle ${o.mute ? 'warn on' : ''}"><input type="checkbox" data-action="out-mute" ${o.mute ? 'checked' : ''}>静音 Mute</label>
        <label class="ab-toggle ${o.masterBypass ? 'on' : ''}"><input type="checkbox" data-action="out-bypass" ${o.masterBypass ? 'checked' : ''}>总旁路（干音直通）</label>
      </div>
      <div class="out-levels" style="margin-top:16px">
        <div class="ol-item"><span>输入电平</span><div class="meter"><div class="bar" id="ol-in"></div></div><span id="ol-in-db">${U.db(s.levels.in)} dB</span></div>
        <div class="ol-item"><span>输出电平</span><div class="meter"><div class="bar" id="ol-out"></div></div><span id="ol-out-db">${U.db(s.levels.out)} dB</span></div>
      </div>
      <p class="out-note" style="margin-top:22px">说明：全局三段 EQ（低架 250Hz / 峰 1kHz / 高架 4kHz）为房间调音，不进入录音总线（dry tap 在输入增益后，wet tap 在 Master 后 EQ 前）。</p>
    </div>`;
  },

  /* ---- 模型库 ---- */
  libraryViewHTML() {
    const s = Store.state;
    const models = s.library.models || [];
    const irs = s.library.irs || [];
    const typeCls = (t) => (t === 'Amp' || t === 'Amp+Cab') ? 'amp' : (t === 'Cab' ? 'cab' : '');
    const grouped = new Map();
    for (const model of models) {
      const type = model.type || 'Other';
      const brand = model.brand || 'Other';
      if (!grouped.has(type)) grouped.set(type, new Map());
      if (!grouped.get(type).has(brand)) grouped.get(type).set(brand, []);
      grouped.get(type).get(brand).push(model);
    }
    let html = `<div class="lib-panel">
      <div class="panel-title">模型库 LIBRARY</div>
      <div class="lib-section">
       <h4>NAM 模型 (${models.length})</h4>`;
    for (const [type, brands] of grouped) {
      html += `<div class="lib-group"><div class="lib-group-title"><span class="lr-type ${typeCls(type)}">${U.esc(type)}</span></div>`;
      for (const [brand, entries] of brands) {
        html += `<div class="lib-brand-title">${U.esc(brand)} <span>${entries.length}</span></div>`;
        for (const m of entries) {
          html += `<div class="lib-row">
            <span class="lr-name">${U.esc(m.name)}</span>
            <button data-action="add-amp" data-asset="${U.esc(m.name)}">添加</button>
          </div>`;
        }
      }
      html += `</div>`;
    }
    html += `</div>
      <div class="lib-section">
        <h4>IR 文件 (${irs.length})</h4>`;
    for (const ir of irs) {
      html += `<div class="lib-row">
        <span class="lr-type cab">IR</span>
        <span class="lr-name">${U.esc(ir.name)}</span>
        <span class="lr-brand"></span>
        <button data-action="add-cab" data-asset="${U.esc(ir.name)}">添加</button>
      </div>`;
    }
    html += `</div>
      <div class="lib-actions">
        <button data-action="import-asset" data-kind="nam">导入 NAM 模型…</button>
        <button data-action="import-asset" data-kind="ir">导入 IR (.wav)…</button>
        <span style="font-size:11px;color:var(--text-dim)">也可以把 .nam / .wav 文件直接拖进窗口</span>
      </div>
    </div>`;
    return html;
  },

  /* ---- MIDI ---- */
  midiViewHTML() {
    const s = Store.state;
    let html = `<div class="midi-panel">
      <div class="panel-title">MIDI 绑定</div>`;
    if (!s.midi.binds.length) {
      html += `<div class="midi-empty">暂无绑定 — 在参数上点 <b>L</b>（MIDI 学习）或场景条的 <b>L CC</b>，然后转动踏板/推子。</div>`;
    }
    for (const b of s.midi.binds) {
      html += `<div class="midi-row"><span class="mr-cc">CC ${b.cc}</span><span class="mr-target">${U.esc(b.target)}</span></div>`;
    }
    if (s.midi.binds.length) {
      html += `<div class="lib-actions"><button data-action="midi-clear">清空全部绑定</button></div>`;
    }
    if (s.midi.learning) {
      const l = s.midi.learning;
      html += `<div class="pp-banner" style="margin-top:14px;border-radius:6px">学习中：等待 CC 输入 →
        ${l.kind === 'param' ? U.esc(l.module) + '.' + U.esc(l.param) : '场景 ' + (l.index + 1)}
        <button data-action="learn-cancel">取消</button></div>`;
      if (Store.engine.isDemo()) {
        html += `<div class="lib-actions" style="margin-top:10px">
          <span style="font-size:11.5px;color:var(--text-dim)">演示模式：模拟 CC 输入</span>
          <input type="number" id="demo-cc" min="0" max="127" value="10" style="width:70px">
          <button data-action="demo-cc-send">发送 CC</button>
        </div>`;
      }
    }
    html += `</div>`;
    return html;
  },

  /* ---- 设置 ---- */
  settingsViewHTML() {
    const s = Store.state;
    const eng = s.engine || {};
    const types = eng.audioTypes || [];
    const selectedType = eng.audioType || (types[0] && types[0].name) || '';
    const selected = types.find((type) => type.name === selectedType) || types[0] || { devices: [] };
    const typeOptions = types.map((type) => `<option value="${U.esc(type.name)}" ${type.name === selectedType ? 'selected' : ''}>${U.esc(type.name)}</option>`).join('');
    const deviceOptions = (selected.devices || []).map((device) => `<option value="${U.esc(device)}" ${device === eng.audioDevice ? 'selected' : ''}>${U.esc(device)}</option>`).join('');
    const audioControls = types.length ? `<div class="set-row audio-settings"><span class="sr-label">音频后端</span>
        <select id="audio-type" data-action="audio-type">${typeOptions}</select>
        <select id="audio-device">${deviceOptions}</select>
        <select id="audio-rate"><option value="44100" ${eng.sampleRate === 44100 ? 'selected' : ''}>44100 Hz</option><option value="48000" ${eng.sampleRate === 48000 ? 'selected' : ''}>48000 Hz</option><option value="96000" ${eng.sampleRate === 96000 ? 'selected' : ''}>96000 Hz</option></select>
        <select id="audio-block"><option value="64" ${eng.blockSize === 64 ? 'selected' : ''}>64</option><option value="128" ${eng.blockSize === 128 ? 'selected' : ''}>128</option><option value="256" ${eng.blockSize === 256 ? 'selected' : ''}>256</option><option value="512" ${eng.blockSize === 512 ? 'selected' : ''}>512</option></select>
        <button data-action="audio-apply">应用</button></div>`
      : `<div class="set-row"><span class="sr-label">音频设备</span><span class="sr-value">${s.mode === 'server' ? '宿主未提供原生音频后端' : '浏览器音频由「启动音频」控制，不能直接加载 ASIO'}</span></div>`;
    return `<div class="set-panel">
      <div class="panel-title">设置 SETTINGS</div>
      <div class="set-row"><span class="sr-label">运行形态</span>
        <span class="sr-value">${s.mode === 'demo' ? '演示模式（内置模拟引擎）' : '宿主模式（C++ 引擎）'} · ${U.esc(s.statusText)}</span></div>
       <div class="set-row"><span class="sr-label">引擎</span>
         <span class="sr-value">namfx_core · ${eng.sampleRate || 48000} Hz / ${eng.blockSize || 128} 样本${eng.audio ? ' · 音频后端：' + U.esc(eng.audio) : ''}${eng.audioDevice ? ' · ' + U.esc(eng.audioDevice) : ''}</span></div>
       ${audioControls}
       ${eng.audioError ? `<div class="set-error">${U.esc(eng.audioError)}</div>` : ''}
      <div class="set-row"><span class="sr-label">语言</span>
        <span class="sr-value">中文 UI + 英文行业术语（模块名/参数单位保留英文）</span></div>
      <div class="set-row"><span class="sr-label">快捷键</span>
        <span class="sr-value"><code>1/2</code> A/B · <code>Ctrl+Z/Y</code> 撤销/重做 · <code>T</code> 调音器 · <code>M</code> 静音 · <code>Alt+1..8</code> 场景</span></div>
      <p class="set-note">NAMFX WebUI · 版本 0.1.0<br>
        设计令牌单源 <code>webui/design-tokens.json</code>；宿主协议 <code>/api/cmd</code> + <code>/api/events</code>（SSE），见 <code>webui/DESIGN.md</code>。</p>
    </div>`;
  },

  /* ---------- 调音器 ---------- */
  openTuner() {
    this.els['tuner-overlay'].classList.remove('hidden');
    this.renderTuner();
  },
  closeTuner() {
    this.els['tuner-overlay'].classList.add('hidden');
  },
  renderTuner() {
    const t = Store.state.tuner;
    const noteEl = this.els['tuner-note'];
    const subEl = this.els['tuner-sub'];
    if (!t.detected) {
      noteEl.textContent = '-';
      noteEl.classList.remove('detected');
      subEl.textContent = '无信号（弹响一根弦）';
      subEl.className = 'tuner-sub';
    } else {
      noteEl.textContent = t.note;
      noteEl.classList.add('detected');
      subEl.textContent = t.string + ' 目标 ' + (t.target || '--') + ' · ' + t.freq.toFixed(1) + ' Hz · ' +
        Math.round(t.cents) + ' ct ' + (t.inTune ? '准了' : (t.cents > 0 ? '偏高' : '偏低'));
      subEl.className = 'tuner-sub ' + (t.inTune ? 'in-tune' : (t.cents > 0 ? 'sharp' : 'flat'));
    }
    const deg = Math.max(-40, Math.min(40, (t.cents || 0) * 0.8));
    const needle = this.els['tuner-needle'];
    needle.style.transform = `rotate(${deg}deg)`;
    needle.style.background = t.detected
      ? 'linear-gradient(180deg, var(--amber), rgba(242,163,60,0))'
      : 'linear-gradient(180deg, #3a4250, rgba(58,66,80,0))';
    this.els['tuner-tuning'].value = String(t.tuning || 0);
    this.els['tuner-sig'].checked = !!t.signal;
  },

  /* ---------- 状态栏 ---------- */
  renderStatusBar() {
    const s = Store.state;
    const msg = this.els['status-msg'];
    msg.textContent = s.msg || '就绪';
    msg.className = 'sb-msg' + (s.msgErr ? ' error' : '');
    this.els['status-xrun'].textContent = s.perf.xrun ? 'xrun ' + s.perf.xrun : '';
    this.els['status-engine'].textContent = '引擎 ' + ((s.engine && s.engine.version) || '0.1.0') + ' · ' + (s.perf.tier || 'Full');
  },

  /* ---------- A/B ---------- */
  async abPress(which) {
    const s = Store.state;
    const buf = which === 'A' ? s.ab.a : s.ab.b;
    if (buf) {
      const r = await this.send(which === 'A' ? 'applyA' : 'applyB');
      if (r && r.ok) {
        Store.patch({ abActive: which });
        this.toast('已切换到 ' + which + ' 音色', 'ok');
      }
    } else {
      const r = await this.send(which === 'A' ? 'copyToA' : 'copyToB');
      if (r && r.ok) {
        Store.patch({ abActive: which });
        this.toast('已将当前音色存入 ' + which + '（再次按 ' + which + ' 切换对比）', 'ok');
      }
    }
  },

  /* ---------- 预设操作 ---------- */
  savePreset() {
    const input = this.els['ps-new-name'];
    const name = input.value.trim();
    if (!name) { this.toast('先输入预设名', 'error'); input.focus(); return; }
    const s = Store.state;
    const label = s.presetSel || (s.currentPreset ? s.currentPreset.label : null);
    if (!label) {
      this.toast('先在左侧点选一个槽位（空槽或已有预设）再保存', 'error');
      return;
    }
    this.send('savePreset', { name, label }).then((r) => {
      if (r && r.ok) {
        input.value = '';
        Store.patch({ presetSel: label });
        this.toast('已保存到 ' + label + ' · ' + name, 'ok');
      }
    });
  },

  async exportPreset() {
    const s = Store.state;
    if (!s.currentPreset) { this.toast('没有已加载的预设', 'error'); return; }
    const r = await Store.engine.cmd('exportPreset', {});
    if (!r || !r.ok) { this.toast((r && r.error) || '导出失败', 'error'); return; }
    const mask = this.modal(`<div class="modal">
      <div class="m-head">导出预设 ${U.esc(s.currentPreset.name)}<button data-action="modal-close">&#x2715;</button></div>
      <div class="m-body"><textarea readonly>${U.esc(r.text)}</textarea></div>
      <div class="m-foot">
        <button data-action="download-export">下载 .json</button>
        <button data-action="modal-close">关闭</button>
      </div>
    </div>`);
    mask.querySelector('[data-action="download-export"]').addEventListener('click', () => {
      const blob = new Blob([r.text], { type: 'application/json' });
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = (s.currentPreset.name || 'preset') + '.json';
      a.click();
      URL.revokeObjectURL(a.href);
    });
    mask.querySelector('[data-action="modal-close"]').addEventListener('click', () => mask.remove());
  },

  async importPresetFile(file) {
    if (!file) return;
    try {
      const text = await file.text();
      const json = JSON.parse(text);
      if (!json || !Array.isArray(json.chain)) throw new Error('不是合法的预设 JSON（缺 chain 数组）');
      const name = String(json.name || file.name.replace(/\.json$/i, '')).trim();
      if (!name || /[\\/.]/.test(name)) throw new Error('预设名不合法（不允许 / \\ .）');
      const r = await this.send('savePreset', { name, chainJson: text });
      if (r && r.ok) this.toast('已导入预设 ' + name, 'ok');
    } catch (e) {
      this.toast('导入失败: ' + (e && e.message || e), 'error');
    }
  },

  async importAssetFile(file, kind) {
    if (!file) return;
    const name = file.name;
    if (Store.engine.isDemo()) {
      const r = await Store.engine.cmd(kind === 'nam' ? 'importModel' : 'importIr', { name });
      if (r && r.ok) this.toast('已导入 ' + name + '（演示）', 'ok');
      Store.emit();
      return;
    }
    const r = await Store.engine.upload('/api/import', file, name);
    if (r && r.ok) this.toast('已导入 ' + name, 'ok');
    else this.toast((r && r.error) || '导入失败', 'error');
  },

  /* ---------- 文件拖入 ---------- */
  onDocDrop(e) {
    e.preventDefault();
    const files = e.dataTransfer && e.dataTransfer.files;
    if (!files || !files.length) return;
    for (const f of files) {
      const kind = /\.nam$/i.test(f.name) ? 'nam' : (/\.wav$/i.test(f.name) ? 'ir' : null);
      if (kind) this.importAssetFile(f, kind);
      else if (/\.json$/i.test(f.name)) this.importPresetFile(f);
      else this.toast('不支持的文件: ' + f.name, 'error');
    }
  },

  /* ---------- 事件委托 ---------- */
  onDocClick(e) {
    const action = e.target.closest('[data-action]');
    if (!action) return;
    const a = action.dataset.action;
    const slot = action.dataset.engineSlot != null
      ? parseInt(action.dataset.engineSlot, 10)
      : (action.dataset.slot != null ? parseInt(action.dataset.slot, 10) : null);
    const file = action.dataset.file;
    const scope = action.dataset.scope;
    const group = action.dataset.group;
    const moduleId = action.dataset.module;
    const index = action.dataset.index != null ? parseInt(action.dataset.index, 10) : null;
    switch (a) {
      case 'load-preset':
        this.send('loadPreset', { file, scope }).then((r) => {
          if (r && r.ok && action.dataset.label) Store.patch({ presetSel: action.dataset.label });
        });
        break;
      case 'pick-empty': {
        const label = action.dataset.label;
        Store.patch({ presetSel: label });
        this.toast('已选中空槽 ' + label + '：编辑音色后点「保存」写入');
        break;
      }
      case 'copy-ab': {
        const which = action.dataset.ab;
        this.send(which === 'A' ? 'copyToA' : 'copyToB').then((r) => {
          if (r && r.ok) {
            Store.patch({ abActive: which });
            this.toast('已重新存入 ' + which, 'ok');
          }
        });
        break;
      }
      case 'del-preset': {
        e.stopPropagation();
        const mask = this.modal(`<div class="modal">
          <div class="m-head">删除预设</div>
          <div class="m-body">确定删除 <b>${U.esc(file)}</b>？此操作不可恢复。</div>
          <div class="m-foot"><button data-action="confirm-del">删除</button><button data-action="modal-close">取消</button></div>
        </div>`);
        mask.querySelector('[data-action="confirm-del"]').addEventListener('click', async () => {
          const r = await Store.engine.cmd('deletePreset', { file, scope });
          mask.remove();
          if (r && r.ok) { Store.emit(); this.toast('已删除 ' + file, 'ok'); }
          else this.toast(r && r.error || '删除失败', 'error');
        });
        mask.querySelector('[data-action="modal-close"]').addEventListener('click', () => mask.remove());
        break;
      }
      case 'toggle-group': {
        const collapsed = Object.assign({}, Store.state.collapsedGroups || {});
        collapsed[group] = !collapsed[group];
        Store.patch({ collapsedGroups: collapsed });
        break;
      }
      case 'scene': {
        const idx = parseInt(action.dataset.index, 10);
        if (Store.state.scenes[idx]) {
          this.send('recallScene', { index: idx });
          Store.patch({ sceneSel: idx });
        } else {
          Store.patch({ sceneSel: idx, msg: '空场景槽 ' + (idx + 1) + '：点「存储」把当前音色存入' });
        }
        break;
      }
      case 'scene-save': {
        const idx = Store.state.sceneSel != null ? Store.state.sceneSel : Store.state.scenes.length;
        const nameInput = document.getElementById('sc-name');
        const name = nameInput ? nameInput.value.trim() : '';
        this.send('saveScene', { index: idx, name });
        break;
      }
      case 'learn-scene': {
        const idx = Store.state.sceneSel != null ? Store.state.sceneSel : 0;
        Store.patch({ midi: { ...Store.state.midi, learning: { kind: 'scene', index: idx } } });
        this.send('learnScene', { index: idx });
        this.toast('学习中：等待 CC 绑定场景 ' + (idx + 1));
        break;
      }
      case 'insert-at': {
        Store.patch({ insertIndex: index, modulePreview: null, pendingAsset: null, selectedSlot: -1 });
        this.toast('选择左侧模块，添加到位置 ' + (index + 1));
        break;
      }
      case 'cancel-insert':
        Store.patch({ insertIndex: -1, pendingAsset: null });
        break;
      case 'select-slot':
        Store.patch({ selectedSlot: slot, modulePreview: null, insertIndex: -1, pendingAsset: null });
        break;
      case 'rail':
        Store.patch({ railSel: group, modulePreview: null });
        break;
      case 'chain-view':
        Store.patch({ chainView: action.dataset.view === 'tiles' ? 'tiles' : 'cards' });
        break;
      case 'preview-module':
        Store.patch({ modulePreview: moduleId, selectedSlot: -1, pendingAsset: null });
        break;
      case 'quick-add': {
        const args = { moduleId };
        if (Store.state.insertIndex >= 0) {
          args.index = Store.state.insertIndex;
          this.send('insertModule', args);
        } else {
          this.send('addModule', args);
        }
        break;
      }
      case 'pick-asset': {
        const name = action.dataset.asset;
        const pa = Store.state.pendingAsset;
        if (pa) { pa.assetName = name; Store.emit(); }
        else {
          const m = Store.state.catalog.moduleById(Store.state.modulePreview);
          if (m) Store.patch({ pendingAsset: { moduleId: m.id, name: m.name, assetName: name } });
        }
        break;
      }
      case 'do-add': {
        const pa = Store.state.pendingAsset;
        const mid = action.dataset.module;
        const assetName = pa && pa.assetName;
        if (!assetName) { this.toast('请先选择资产', 'error'); break; }
        const at = Store.state.insertIndex;
        const args = { moduleId: mid, asset: assetName };
        this.send(at >= 0 ? 'insertModule' : 'addModule', at >= 0 ? { ...args, index: at } : args);
        break;
      }
      case 'import-asset': {
        const kind = action.dataset.kind;
        const input = this.el(`<input type="file" accept="${kind === 'nam' ? '.nam' : '.wav,audio/wav'}" hidden>`);
        document.body.appendChild(input);
        input.onchange = () => this.importAssetFile(input.files[0], kind);
        input.click();
        break;
      }
      case 'move': {
        const dir = parseInt(action.dataset.dir, 10);
        this.send('moveModule', { slot, direction: dir });
        break;
      }
      case 'del-slot':
        this.send('removeModule', { slot });
        break;
      case 'add-amp':
      case 'add-cab': {
        const mid = a === 'add-amp' ? 'amp.nam' : 'cab.ir';
        this.send('addModule', { moduleId: mid, asset: action.dataset.asset });
        break;
      }
      case 'param-reset': {
        const cell = action.closest('.knob-cell');
        const specId = cell.dataset.param;
        const sl = parseInt(cell.dataset.slot, 10);
        const mod = Store.state.catalog.moduleById(cell.dataset.module);
        const spec = mod && mod.specs.find(x => x.id === specId);
        if (spec) this.send('setParam', { slot: sl, param: specId, value: spec.def });
        break;
      }
      case 'param-learn': {
        const cell = action.closest('.knob-cell');
        Store.patch({ midi: { ...Store.state.midi, learning: { kind: 'param', module: cell.dataset.module, param: cell.dataset.param } } });
        this.send('learnParam', { module: cell.dataset.module, param: cell.dataset.param });
        this.toast('学习中：转动踏板/推子（CC）绑定 ' + cell.dataset.module + '.' + cell.dataset.param);
        break;
      }
      case 'learn-cancel':
        Store.patch({ midi: { ...Store.state.midi, learning: null } });
        this.send('learnCancel');
        break;
      case 'demo-cc-send': {
        const ccEl = document.getElementById('demo-cc');
        const cc = parseInt(ccEl ? ccEl.value : '10', 10);
        const l = Store.state.midi.learning;
        if (!l) break;
        if (l.kind === 'param') this.send('midiLearnParam', { cc });
        else this.send('midiLearnScene', { cc });
        break;
      }
      case 'midi-clear':
        this.send('midiClear');
        break;
      case 'audio-apply': {
        const type = document.getElementById('audio-type');
        const device = document.getElementById('audio-device');
        const rate = document.getElementById('audio-rate');
        const block = document.getElementById('audio-block');
        this.send('setAudio', {
          type: type ? type.value : '',
          device: device ? device.value : '',
          sampleRate: rate ? parseInt(rate.value, 10) : 44100,
          blockSize: block ? parseInt(block.value, 10) : 128,
        });
        break;
      }
      case 'modal-close':
        action.closest('.modal-mask').remove();
        break;
    }
  },

  onDocDoubleClick(e) {
    const cell = e.target.closest('.chain-cell:not(.add)');
    if (!cell || e.target.closest('button, input, [data-grip]')) return;
    const slot = parseInt(cell.dataset.engineSlot != null ? cell.dataset.engineSlot : cell.dataset.slot, 10);
    const current = Store.state.chain.find(item => item.slot === slot);
    if (!current) return;
    this.send('setBypass', { slot, bypass: !current.bypass });
  },

  onDocChange(e) {
    const t = e.target;
    const a = t.dataset ? t.dataset.action : null;
    if (a === 'out-mute') this.send('setOutput', { key: 'mute', value: t.checked });
    if (a === 'out-bypass') this.send('setOutput', { key: 'masterBypass', value: t.checked });
    if (a === 'bypass') {
      const slot = parseInt(t.dataset.slot, 10);
      this.send('setBypass', { slot, bypass: !t.checked });
    }
    if (a === 'audio-type') {
      const type = (Store.state.engine.audioTypes || []).find((item) => item.name === t.value);
      const device = document.getElementById('audio-device');
      if (device) {
        device.innerHTML = (type ? type.devices : []).map((name) => `<option value="${U.esc(name)}">${U.esc(name)}</option>`).join('');
      }
    }
  },

  onDocInput(e) {
    const t = e.target;
    const a = t.dataset ? t.dataset.action : null;
    if (a === 'module-mix') {
      const slot = parseInt(t.dataset.slot, 10);
      this.send('setMix', { slot, mix: parseFloat(t.value) });
      const readout = t.parentElement && t.parentElement.querySelector('b');
      if (readout) readout.textContent = Math.round(parseFloat(t.value) * 100) + '%';
    }
    if (a === 'out-slider') {
      const key = t.dataset.key;
      this.send('setOutput', { key, value: parseFloat(t.value) });
      const row = t.closest('.out-row');
      if (row) {
         const unit = key === 'master' || key === 'ingain' ? 'dB' : (key === 'lowcut' || key === 'highcut' ? 'Hz' : '');
        row.querySelector('.or-unit').textContent = U.fmt(parseFloat(t.value), unit);
      }
    }
  },

  /* 右键菜单（旋钮） */
  onDocContext(e) {
    const cell = e.target.closest('.knob-cell');
    if (!cell) return;
    e.preventDefault();
    const specId = cell.dataset.param;
    const slot = parseInt(cell.dataset.slot, 10);
    const mod = Store.state.catalog.moduleById(cell.dataset.module);
    const spec = mod ? mod.specs.find(x => x.id === specId) : null;
    if (!spec) return;
    const menu = this.el(`<div class="modal-mask" style="z-index:400;background:transparent;position:fixed;inset:auto;align-items:flex-start;justify-content:flex-start;pointer-events:auto">
      <div class="modal" style="min-width:220px;box-shadow:0 10px 30px rgba(0,0,0,.7)">
        <div class="m-body" style="padding:8px;display:flex;flex-direction:column;gap:4px">
          <button data-ctx="reset" style="text-align:left">重置默认 (${U.fmt(spec.def, spec.unit)})</button>
          <button data-ctx="random" style="text-align:left">随机微调</button>
          <button data-ctx="learn" style="text-align:left">MIDI 学习…</button>
        </div>
      </div>
    </div>`);
    menu.style.left = Math.min(e.clientX, window.innerWidth - 250) + 'px';
    menu.style.top = e.clientY + 'px';
    menu.addEventListener('click', (ev) => {
      const btn = ev.target.closest('[data-ctx]');
      if (btn) {
        const kind = btn.dataset.ctx;
        if (kind === 'reset') this.send('setParam', { slot, param: specId, value: spec.def });
        else if (kind === 'random') {
          const r = spec.min + Math.random() * (spec.max - spec.min);
          this.send('setParam', { slot, param: specId, value: Math.round(r * 1000) / 1000 });
        } else if (kind === 'learn') {
          Store.patch({ midi: { ...Store.state.midi, learning: { kind: 'param', module: cell.dataset.module, param: specId } } });
          UI.send('learnParam', { module: cell.dataset.module, param: specId });
        }
      }
      menu.remove();
    });
    document.addEventListener('pointerdown', () => menu.remove(), { once: true });
    this.els['modal-root'].appendChild(menu);
  },

  /* 旋钮指针拖动 + 链重排 */
  onDocPointerDown(e) {
    const knobEl = e.target.closest('[data-knob]');
    if (knobEl) {
      this.startKnobDrag(e, knobEl);
      return;
    }
    const grip = e.target.closest('[data-grip]');
    if (grip) {
      this.startChainDrag(e, grip);
      return;
    }
    const cell = e.target.closest('.chain-cell:not(.add)');
    if (cell && !e.target.closest('button, input, select')) {
      this.startChainDrag(e, cell);
    }
  },

  startKnobDrag(e, knobEl) {
    e.preventDefault();
    const slot = parseInt(knobEl.dataset.slot, 10);
    const specId = knobEl.dataset.knob;
    const cell = knobEl.closest('.knob-cell');
    const mod = Store.state.catalog.moduleById(cell.dataset.module);
    const spec = mod ? mod.specs.find(x => x.id === specId) : null;
    if (!spec) return;
    const c = Store.state.chain.find(x => x.slot === slot);
    if (!c) return;
    const startY = e.clientY;
    const startV = c.params[specId];
    const startNorm = valueToNorm(startV, spec);
    const sens = e.shiftKey ? 1 / 600 : 1 / 160;
    const move = (ev) => {
      const delta = (startY - ev.clientY) * sens;
      const v = normToValue(startNorm + delta, spec);
      this.queueParam({ slot, param: specId, value: v });
    };
    const up = () => {
      document.removeEventListener('pointermove', move);
      document.removeEventListener('pointerup', up);
    };
    document.addEventListener('pointermove', move);
    document.addEventListener('pointerup', up);
  },

  startChainDrag(e, grip) {
    if (grip.dataset.grip != null) e.preventDefault();
    const chainEl = this.els['chain'];
    const visualSlot = parseInt(grip.dataset.grip != null ? grip.dataset.grip : grip.dataset.slot, 10);
    const engineSlot = parseInt(grip.dataset.engineSlot != null ? grip.dataset.engineSlot : grip.dataset.slot, 10);
    const startX = e.clientX;
    const startY = e.clientY;
    let dragging = false;
    const move = (ev) => {
      // click/long-press with tiny jitter must stay a click: only engage
      // the drag once the pointer clearly moves away from the press point
      if (!dragging && Math.hypot(ev.clientX - startX, ev.clientY - startY) < 4) {
        return;
      }
      if (!dragging) {
        dragging = true;
        Store.patch({ dragSlot: visualSlot, dropIndex: null, dropSlot: null });
      }
      const cells = [...chainEl.querySelectorAll('.chain-cell[data-slot]')];
      let closest = null;
      let closestDistance = Infinity;
      for (const item of cells) {
        const rect = item.getBoundingClientRect();
        const distance = Math.hypot(ev.clientX - (rect.left + rect.width / 2),
          ev.clientY - (rect.top + rect.height / 2));
        if (distance < closestDistance) {
          closest = { item, rect };
          closestDistance = distance;
        }
      }
      let targetSlot = null;
      if (closest) {
        targetSlot = parseInt(closest.item.dataset.slot, 10);
      }
      Store.patch({ dropSlot: targetSlot });
    };
    const up = () => {
      document.removeEventListener('pointermove', move);
      document.removeEventListener('pointerup', up);
      if (!dragging) return;
      const target = Store.state.dropSlot;
      const source = Store.state.chain.find(c => (c.uiSlot != null ? c.uiSlot : c.slot) === visualSlot);
      const targetItem = Store.state.chain.find(c => (c.uiSlot != null ? c.uiSlot : c.slot) === target);
      Store.patch({ dragSlot: null, dropIndex: null, dropSlot: null });
      if (target != null && source && target !== visualSlot) {
        if (targetItem) {
          this.send('swapModule', {
            slot: engineSlot,
            target: targetItem.engineSlot != null ? targetItem.engineSlot : targetItem.slot,
            visualSource: visualSlot,
            visualTarget: target,
          });
        } else {
          const targetIndex = Store.state.chain.filter((item) =>
            (item.uiSlot != null ? item.uiSlot : item.slot) < target).length;
          this.send('moveModuleTo', {
            slot: engineSlot,
            index: targetIndex,
            visualSource: visualSlot,
            visualTarget: target,
          });
        }
      }
    };
    document.addEventListener('pointermove', move);
    document.addEventListener('pointerup', up);
  },
};

/* ---------- SVG 旋钮 ---------- */
function mountKnobs(root) {
  root.querySelectorAll('[data-knob]').forEach((el) => {
    if (el.dataset.mounted) return;
    el.dataset.mounted = '1';
    const slot = parseInt(el.dataset.slot, 10);
    const specId = el.dataset.knob;
    const cell = el.closest('.knob-cell');
    const mod = Store.state.catalog.moduleById(cell.dataset.module);
    const spec = mod ? mod.specs.find(x => x.id === specId) : null;
    if (!spec) return;
    const size = 58;
    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    svg.setAttribute('viewBox', `0 0 ${size} ${size}`);
    svg.setAttribute('width', size);
    svg.setAttribute('height', size);
    svg.classList.add('knob-svg');
    const cx = size / 2, cy = size / 2, r = size / 2 - 7;
    const polar = (x, y, rr, deg) => {
      const rad = (deg - 90) * Math.PI / 180;
      return { x: x + rr * Math.cos(rad), y: y + rr * Math.sin(rad) };
    };
    const arc = (ang1, ang2) => {
      const p1 = polar(cx, cy, r, ang1), p2 = polar(cx, cy, r, ang2);
      const large = (ang2 - ang1) > 180 ? 1 : 0;
      return `M ${p1.x} ${p1.y} A ${r} ${r} 0 ${large} 1 ${p2.x} ${p2.y}`;
    };
    const makeEl = (tag, attrs) => {
      const el2 = document.createElementNS('http://www.w3.org/2000/svg', tag);
      for (const k in attrs) el2.setAttribute(k, attrs[k]);
      return el2;
    };
    const draw = (v) => {
      const norm = valueToNorm(v, spec);
      const ang = -135 + norm * 270;
      const ind = polar(cx, cy, r - 8, ang);
      svg.innerHTML = '';
      svg.appendChild(makeEl('path', { d: arc(-135, 135), 'class': 'knob-ring' }));
      if (norm > 0.003) svg.appendChild(makeEl('path', { d: arc(-135, ang), 'class': 'knob-ring-fill' }));
      svg.appendChild(makeEl('circle', { cx, cy, r: r - 6, 'class': 'knob-body' }));
      svg.appendChild(makeEl('line', { x1: cx, y1: cy - 1.5, x2: ind.x, y2: ind.y, 'class': 'knob-ind' }));
    };
    draw((Store.state.chain.find(c => c.slot === slot) || {}).params ? Store.state.chain.find(c => c.slot === slot).params[specId] : spec.def);
    el.appendChild(svg);
    el._knob = { spec, slot, draw };
    // 滚轮 + 双击输入（只挂一次）
    el.addEventListener('wheel', (ev) => {
      ev.preventDefault();
      const c = Store.state.chain.find(x => x.slot === slot);
      if (!c) return;
      const cur = c.params[specId];
       const step = ev.shiftKey ? 0.001 : 0.01;
       const v = normToValue(valueToNorm(cur, spec) + (ev.deltaY < 0 ? step : -step), spec);
       UI.queueParam({ slot, param: specId, value: v });
    }, { passive: false });
    el.addEventListener('dblclick', (ev) => {
      ev.stopPropagation();
      const c = Store.state.chain.find(x => x.slot === slot);
      const cur = c ? c.params[specId] : spec.def;
      const val = prompt(`输入 ${spec.name || specId} 数值 (${spec.min} ~ ${spec.max})`, cur);
      if (val != null && !isNaN(parseFloat(val))) {
        const v = Math.min(spec.max, Math.max(spec.min, parseFloat(val)));
        UI.send('setParam', { slot, param: specId, value: v });
      }
    });
  });
}

/* 旋钮值同步（silent 局部刷新路径） */
function syncKnobValues() {
  document.querySelectorAll('[data-knob]').forEach((el) => {
    const slot = parseInt(el.dataset.slot, 10);
    const specId = el.dataset.knob;
    const c = Store.state.chain.find(x => x.slot === slot);
    const cell = el.closest('.knob-cell');
    if (!c || !cell) return;
    const v = c.params[specId];
    const spec = c.specs.find(x => x.id === specId);
    if (!spec) return;
    if (el._knob) el._knob.draw(v);
    const valEl = cell.querySelector('.kc-val');
    if (valEl) valEl.textContent = U.fmt(v, spec.unit);
  });
}

/* 全量渲染后钩子 */
function afterRender() {
  const body = document.getElementById('lower-body');
  mountKnobs(body);
  syncKnobValues();
  const olIn = document.getElementById('ol-in');
  const olOut = document.getElementById('ol-out');
  if (olIn) {
    olIn.style.width = Math.max(0, Math.min(100, Store.state.levels.in * 100)) + '%';
    olIn.classList.toggle('clip', Store.state.levels.in > 0.98);
  }
  if (olOut) {
    olOut.style.width = Math.max(0, Math.min(100, Store.state.levels.out * 100)) + '%';
    olOut.classList.toggle('clip', Store.state.levels.out > 0.98);
  }
}
