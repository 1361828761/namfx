/* NAMFX WebUI — 模块目录
 * 引擎注册表 (core/modules) 的前端镜像：分类、模块、参数规格。
 * 服务端模式以 /api/state 返回的 catalog 为准；本文件同时是 demo 模式的数据源。
 * 参数规格与 core/modules/** 的 registerModule 一一对应（勿漂移）。
 */
'use strict';

const NAMFX_CATALOG = {
  groups: [
    {
      id: 'dist', name: '过载 / 失真', en: 'Distortion',
      modules: [
        { id: 'od.ts808', name: 'TS808 过载', desc: 'TS808 风格软削波过载（WDF 电路建模）',
          asset: 'none', specs: [
            { id: 'drive', name: 'Drive', min: 0, max: 10, def: 5, unit: '', taper: 'linear' },
            { id: 'tone', name: 'Tone', min: 0, max: 10, def: 5, unit: '', taper: 'linear' },
            { id: 'level', name: 'Level', min: -60, max: 12, def: 0, unit: 'dB', taper: 'linear' },
          ] },
        { id: 'od.transparent', name: 'Klon 透明过载', desc: 'Klon 风格透明过载（WDF 电路建模）',
          asset: 'none', specs: [
            { id: 'gain', name: 'Gain', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'treble', name: 'Treble', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'level', name: 'Level', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
          ] },
        { id: 'od.mosfet', name: 'OCD 失真', desc: 'OCD 风格 MOSFET 失真（电路建模）',
          asset: 'none', specs: [
            { id: 'drive', name: 'Drive', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'tone', name: 'Tone', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'volume', name: 'Volume', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
          ] },
      ],
    },
    {
      id: 'comp', name: '压缩', en: 'Compressor',
      modules: [
        { id: 'comp.ota', name: 'Dyna Comp', desc: 'Dyna Comp 风格 OTA 压缩（电路级建模）',
          asset: 'none', specs: [
            { id: 'sustain', name: 'Sustain', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'attack', name: 'Attack', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'ratio', name: 'Ratio', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'level', name: 'Level', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
          ] },
      ],
    },
    {
      id: 'mod', name: '调制', en: 'Modulation',
      modules: [
        { id: 'mod.chorus', name: 'CE-2 合唱', desc: 'CE-2 风格 BBD 合唱',
          asset: 'none', specs: [
            { id: 'depth', name: 'Depth', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'rate', name: 'Rate', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'level', name: 'Level', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
          ] },
        { id: 'mod.flanger', name: 'BF-2 镶边', desc: 'BF-2 风格镶边（Electric Mistress 基准）',
          asset: 'none', specs: [
            { id: 'feedback', name: 'Feedback', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'range', name: 'Range', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'rate', name: 'Rate', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
          ] },
        { id: 'mod.phaser', name: 'Phase 90 相位', desc: 'Phase 90 风格相位',
          asset: 'none', specs: [
            { id: 'depth', name: 'Depth', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'rate', name: 'Rate', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'level', name: 'Level', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
          ] },
        { id: 'mod.wah', name: 'Crybaby 哇音', desc: 'Crybaby 风格哇音（position 为控制源钩子）',
          asset: 'none', specs: [
            { id: 'position', name: 'Position', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'resonance', name: 'Resonance', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'level', name: 'Level', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
          ] },
      ],
    },
    {
      id: 'dly', name: '延迟', en: 'Delay',
      modules: [
        { id: 'dly.dm2', name: 'DM-2 延迟', desc: 'DM-2 风格模拟 BBD 延迟',
          asset: 'none', specs: [
            { id: 'time', name: 'Time', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'feedback', name: 'Feedback', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'level', name: 'Level', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
          ] },
        { id: 'dly.tape', name: 'Echoplex 磁带延迟', desc: 'Echoplex 风格磁带延迟',
          asset: 'none', specs: [
            { id: 'time', name: 'Time', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'echo', name: 'Echo', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'tone', name: 'Tone', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'level', name: 'Level', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
          ] },
      ],
    },
    {
      id: 'rvb', name: '混响', en: 'Reverb',
      modules: [
        { id: 'rvb.spring', name: '弹簧混响', desc: '弹簧物理模型混响',
          asset: 'none', specs: [
            { id: 'dwell', name: 'Dwell', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'mix', name: 'Mix', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'damp', name: 'Damp', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
          ] },
        { id: 'rvb.hall', name: 'Hall 混响', desc: '经典 Hall 算法混响',
          asset: 'none', specs: [
            { id: 'room', name: 'Room', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'damp', name: 'Damp', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'mix', name: 'Mix', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
          ] },
      ],
    },
    {
      id: 'eq', name: '滤波 / EQ', en: 'Filter / EQ',
      modules: [
        { id: 'eq.ge7', name: 'GE-7 图示均衡', desc: 'GE-7 风格 7 段图示均衡',
          asset: 'none', specs: [
            { id: 'band100', name: '100Hz', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'band200', name: '200Hz', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'band400', name: '400Hz', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'band800', name: '800Hz', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'band1600', name: '1.6kHz', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'band3200', name: '3.2kHz', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'band6400', name: '6.4kHz', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'level', name: 'Level', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
          ] },
        { id: 'tone', name: '音色', desc: '通用音色滤波（低通/高通）',
          asset: 'none', specs: [
            { id: 'freq', name: 'Freq', min: 80, max: 8000, def: 2000, unit: 'Hz', taper: 'log' },
            { id: 'mode', name: 'Mode', min: 0, max: 1, def: 0, unit: '', taper: 'linear' },
          ] },
      ],
    },
    {
      id: 'gate', name: '门限', en: 'Gate',
      modules: [
        { id: 'gate.ns2', name: 'NS-2 门限', desc: 'NS-2 风格噪声门限',
          asset: 'none', specs: [
            { id: 'threshold', name: 'Threshold', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'decay', name: 'Decay', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'level', name: 'Level', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
          ] },
      ],
    },
    {
      id: 'pitch', name: '音高', en: 'Pitch',
      modules: [
        { id: 'pitch.shift', name: '移调', desc: '单音移调核心 v1',
          asset: 'none', specs: [
            { id: 'shift', name: 'Shift', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'mix', name: 'Mix', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'level', name: 'Level', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
          ] },
        { id: 'pitch.octave', name: '八度', desc: '单音八度（整流/倍频整形）',
          asset: 'none', specs: [
            { id: 'mix', name: 'Mix', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'tone', name: 'Tone', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'level', name: 'Level', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
          ] },
      ],
    },
    {
      id: 'amp', name: '箱头', en: 'Amp (NAM)',
      modules: [
        { id: 'amp.nam', name: 'NAM 箱头', desc: 'Neural Amp Modeler 箱头模型（需 .nam 文件）',
          asset: 'nam', specs: [
            { id: 'gain', name: 'Gain', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'bass', name: 'Bass', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'middle', name: 'Middle', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'treble', name: 'Treble', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'output', name: 'Output', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'tier', name: 'Tier', min: 0, max: 1, def: 1, unit: '', taper: 'linear' },
          ] },
      ],
    },
    {
      id: 'cab', name: '箱体 / IR', en: 'Cabinet / IR',
      modules: [
        { id: 'cab.ir', name: 'IR 箱体', desc: 'IR 卷积箱体模拟（需 .wav IR 文件）',
          asset: 'ir', specs: [
            { id: 'gain', name: 'Gain', min: 0, max: 1, def: 0.5, unit: '', taper: 'linear' },
            { id: 'lowcut', name: 'Low Cut', min: 0, max: 1, def: 0, unit: '', taper: 'linear' },
            { id: 'highcut', name: 'High Cut', min: 0, max: 1, def: 1, unit: '', taper: 'linear' },
          ] },
      ],
    },
    {
      id: 'util', name: '工具', en: 'Utility',
      modules: [
        { id: 'gain', name: '增益', desc: '输入增益（-60..+24 dB）',
          asset: 'none', specs: [
            { id: 'gain', name: 'Gain', min: -60, max: 24, def: 0, unit: 'dB', taper: 'linear' },
          ] },
      ],
    },
  ],
};

/* 模块目录查询 */
NAMFX_CATALOG.moduleById = function (id) {
  for (const g of NAMFX_CATALOG.groups) {
    for (const m of g.modules) {
      if (m.id === id) return m;
    }
  }
  return null;
};
NAMFX_CATALOG.groupOf = function (id) {
  for (const g of NAMFX_CATALOG.groups) {
    for (const m of g.modules) {
      if (m.id === id) return g;
    }
  }
  return null;
};
/* 类别 → 引擎 category 属性（预设 JSON 用） */
NAMFX_CATALOG.engineCategory = function (id) {
  if (id === 'amp.nam') return 'amp';
  if (id === 'cab.ir') return 'cab';
  return 'pedal';
};
NAMFX_CATALOG.engineImpl = function (id) {
  if (id === 'cab.ir') return 'ir';
  if (id === 'amp.nam') return 'nam';
  return 'dsp';
};

/* 演示预设清单（demo 模式由 mock 引擎 fetch www/presets/*.json） */
const NAMFX_DEMO_PRESETS = [
  'clean.json', 'boost.json', 'bright.json', 'mellow.json', 'warm.json',
  'ts_drive.json', 'transparent.json', 'mosfet_drive.json', 'ota_comp.json',
  'chorus.json', 'flanger.json', 'phaser.json', 'wah.json', 'gate.json',
  'eq.json', 'delay.json', 'tape.json', 'spring.json', 'hall.json',
  'pitch.json', 'octave.json', 'cab.json', 'nam_amp.json', 'chain_drive.json',
];

/* 预设 ABC 编号：文件名带 "NN[A-C]_" 前缀则采用，否则按排序位置自动编号 */
const NAMFX_PRESET_LABEL = function (file, index) {
  const m = /^(\d{2})([A-C])[-_]/.exec(file);
  if (m) return m[1] + m[2];
  const group = String(Math.floor(index / 3) + 1).padStart(2, '0');
  const letter = 'ABC'[index % 3];
  return group + letter;
};
