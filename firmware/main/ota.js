(() => {
  'use strict';
  const el = id => document.getElementById(id);
  let limit = 0, busy = false;
  async function loadFirmware() {
    try {
      const response = await fetch('/api/firmware', {cache:'no-store'});
      if (!response.ok) throw Error('读取失败');
      const info = await response.json();
      limit = info.max_size;
      el('firmwareInfo').textContent = 'ComputerScreen · 当前版本：' + info.version +
        (info.validating ? ' · 正在验证新版本，请约 30 秒后刷新' : info.restarting ? ' · 正在重启' : '');
      el('upgrade').disabled = info.validating || info.restarting || !limit;
    } catch (error) {
      el('firmwareInfo').textContent = '无法读取版本，请连接副屏网络后刷新页面。';
    }
  }
  el('upgrade').onclick = async () => {
    if (busy) return;
    if (el('setup').dataset.saving === '1') { el('upgradeStatus').textContent = '设置正在保存，请等待设备重启后再升级。'; return; }
    const file = el('firmwareFile').files[0];
    const status = el('upgradeStatus'), progress = el('upgradeProgress');
    if (!file || !/\.bin$/i.test(file.name)) { status.textContent = '请先选择 .bin 升级文件。'; return; }
    if (!limit || file.size < 320 || file.size > limit) { status.textContent = '文件大小不正确，请选择本项目的 OTA 升级文件。'; return; }
    busy = true;
    el('setup').dataset.updating = '1';
    el('upgrade').disabled = true;
    el('save').disabled = true;
    el('firmwareFile').disabled = true;
    try {
      const bytes = new Uint8Array(await file.slice(0, 288).arrayBuffer());
      const view = new DataView(bytes.buffer);
      const text = (start, size) => String.fromCharCode(...bytes.slice(start, start + size)).split('\0')[0];
      if (bytes.length !== 288 || bytes[0] !== 0xe9 || view.getUint16(12, true) !== 9 || bytes[23] !== 1 ||
          view.getUint32(32, true) !== 0xabcd5432 || !['ComputerScreen','case_display'].includes(text(80,32)) || !text(48,32).endsWith('-ota'))
        throw Error('不是兼容的副屏 OTA 升级文件，请检查文件。');
      progress.hidden = false;
      progress.value = 0;
      status.textContent = '正在上传，请保持供电和网络连接…';
      const message = await new Promise((resolve, reject) => {
        const xhr = new XMLHttpRequest();
        xhr.open('POST', '/api/update');
        xhr.setRequestHeader('Content-Type', 'application/octet-stream');
        xhr.setRequestHeader('X-ComputerScreen-Setup', '1');
        xhr.timeout = 330000;
        xhr.upload.onprogress = event => {
          if (!event.lengthComputable) return;
          const percent = Math.min(100, Math.floor(event.loaded * 100 / event.total));
          progress.value = percent;
          status.textContent = percent === 100 ? '上传完成，正在校验固件，请稍候…' : '正在上传 ' + percent + '%，请保持供电…';
        };
        xhr.onload = () => xhr.status >= 200 && xhr.status < 300 ? resolve(xhr.responseText) : reject(Error(xhr.responseText || '设备未接受升级文件'));
        xhr.onerror = () => reject(Error('连接中断，请等待设备恢复后刷新，确认版本再决定是否重试。'));
        xhr.ontimeout = () => reject(Error('等待超时，请稍后刷新页面确认设备版本。'));
        xhr.send(file);
      });
      progress.value = 100;
      status.textContent = message;
      el('upgrade').textContent = '已提交升级，请稍后刷新';
      // Keep controls disabled until reload so an accepted update cannot race a settings save.
      return;
    } catch (error) {
      status.textContent = error.message;
      el('upgrade').disabled = false;
      el('save').disabled = false;
      el('firmwareFile').disabled = false;
      busy = false;
      delete el('setup').dataset.updating;
    }
  };
  loadFirmware();
})();
