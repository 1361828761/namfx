/* NAMFX WebUI — 启动与全局快捷键 */
'use strict';

async function boot() {
  const engine = await createEngine();
  engine.start();
  UI.init();
  document.body.classList.toggle('locked', !!Store.state.locked);
  renderNow();
}

function renderNow() {
  UI.render();
}

/* ---------- 快捷键（PLAN §10：非文本输入焦点时生效） ---------- */
document.addEventListener('keydown', (e) => {
  const tag = document.activeElement && document.activeElement.tagName;
  const typing = tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT';
  if (e.ctrlKey || e.altKey || e.metaKey) {
    if (typing) return;
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'k') {
      e.preventDefault();
      const search = document.getElementById('ps-search');
      if (search) { search.focus(); search.select(); }
      return;
    }
    if (e.ctrlKey && e.key === 'z' && !e.shiftKey) { e.preventDefault(); UI.send('undo'); return; }
    if ((e.ctrlKey && e.key === 'y') || (e.ctrlKey && e.key === 'z' && e.shiftKey)) { e.preventDefault(); UI.send('redo'); return; }
    if (e.altKey && e.key >= '1' && e.key <= '8') {
      e.preventDefault();
      const idx = parseInt(e.key, 10) - 1;
      const sc = Store.state.scenes[idx];
      if (sc) UI.send('recallScene', { index: idx });
      else { Store.patch({ sceneSel: idx, msg: '空场景槽 ' + (idx + 1) + '：点「存储」把当前音色存入' }); }
      return;
    }
    return;
  }
  if (typing) return;
  switch (e.key) {
    case '1': UI.abPress('A'); break;
    case '2': UI.abPress('B'); break;
    case 't': case 'T': UI.openTuner(); break;
    case 'm': case 'M': UI.send('setOutput', { key: 'mute', value: !Store.state.output.mute }); break;
    case 'Escape':
      if (!document.getElementById('tuner-overlay').classList.contains('hidden')) UI.closeTuner();
      else document.querySelectorAll('.modal-mask').forEach(m => m.remove());
      break;
  }
});

/* EXE 封装桥（WebView2 宿主注入 window.namfxBridge）：
 * windowControl('min'|'max'|'close') 等 —— 前端只负责调用，宿主实现。 */
window.namfxBridge = window.namfxBridge || null;

boot();
