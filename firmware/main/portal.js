'use strict';
const $ = id => document.getElementById(id);
const form = $('setup'), statusBox = $('status'), result = $('result'), button = $('save');
let originalSSID = '', hasPassword = false, cityGroups = [], scanning = false;
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));

function option(text, value, secure) {
  const item = new Option(text, value);
  if (secure !== undefined) item.dataset.secure = String(secure);
  return item;
}

function fillCities(province, selected = '') {
  const group = cityGroups.find(item => item.name === province);
  $('city').replaceChildren(option('请选择城市', ''));
  if (!group) return;
  for (const city of group.cities) $('city').append(option(city.name, city.id));
  if (selected) $('city').value = selected;
  else if (group.cities.length === 1) $('city').value = group.cities[0].id;
}

function loadCities(groups, saved) {
  cityGroups = groups;
  $('province').replaceChildren(option('请选择省份', ''));
  for (const group of groups) $('province').append(option(group.name, group.name));
  const matches = [];
  if (saved) {
    for (const group of groups) for (const city of group.cities) {
      if (city.id === saved || [city.name, ...(city.aliases || [])].some(name => name.toLowerCase() === saved.toLowerCase()))
        matches.push({group, city});
    }
  }
  if (matches.length === 1) {
    $('province').value = matches[0].group.name;
    fillCities(matches[0].group.name, matches[0].city.id);
  } else if (saved) {
    // Preserve older/custom settings when a name cannot identify a unique city.
    $('province').append(option('保留当前城市', '__saved__'));
    $('province').value = '__saved__';
    $('city').replaceChildren(option('已保存：' + saved, saved));
    $('city').value = saved;
  } else fillCities('');
}
$('province').onchange = () => fillCities($('province').value);
$('brightness').oninput = () => $('brightnessValue').textContent = $('brightness').value;
$('night_brightness').oninput = () => $('nightBrightnessValue').textContent = $('night_brightness').value;
$('auto_dim').onchange = () => $('nightBrightnessRow').hidden = !$('auto_dim').checked;
$('manual_wifi').onchange = () => {
  const manual = $('manual_wifi').checked;
  $('manual_ssid').hidden = !manual;
  $('manual_ssid').required = manual;
  $('ssid').disabled = manual;
  $('ssid').required = !manual;
};
$('manual_ssid').oninput = () => {
  if ($('manual_ssid').value !== originalSSID) $('open_wifi').checked = false;
};
$('ssid').onchange = () => {
  const item = $('ssid').selectedOptions[0];
  if (item && item.dataset.secure !== undefined) $('open_wifi').checked = item.dataset.secure === 'false';
};

async function scanWifi() {
  if (scanning) return;
  scanning = true;
  $('scan').disabled = true;
  $('scan').textContent = '正在扫描…';
  $('wifiHint').textContent = '正在查找附近的 2.4 GHz 网络，请稍候。';
  try {
    let url = '/api/wifi?start=1', data;
    for (let attempt = 0; attempt < 25; ++attempt) {
      const response = await fetch(url, {cache:'no-store'});
      if (!response.ok) throw Error('扫描请求失败');
      data = await response.json();
      if (data.status === 'ready') break;
      await pause(650);
      url = data.status === 'scanning' ? '/api/wifi' : '/api/wifi?start=1';
    }
    if (!data || data.status !== 'ready') throw Error('网络正忙，请稍后重新扫描');
    const networks = data.networks || [];
    // Preserve the user's latest selection, including changes made during scanning.
    const selected = $('ssid').value || originalSSID;
    $('ssid').replaceChildren(option('请选择 Wi-Fi', ''));
    const seen = new Set();
    for (const network of networks) {
      if (!network.ssid || seen.has(network.ssid)) continue;
      seen.add(network.ssid);
      const strength = network.rssi >= -55 ? '强' : network.rssi >= -70 ? '中' : '弱';
      $('ssid').append(option(network.ssid + ' · ' + strength + (network.secure ? '' : ' · 无密码'), network.ssid, network.secure));
    }
    if (selected && !seen.has(selected)) $('ssid').append(option(selected + ' · 已保存，暂未扫描到', selected));
    if (selected) $('ssid').value = selected;
    $('ssid').onchange();
    $('wifiHint').textContent = networks.length ? '请选择网络，支持中文名称。找不到时可重新扫描或手动输入。' : '暂未找到网络，可重新扫描或手动输入隐藏网络。';
  } catch (error) {
    $('wifiHint').textContent = error.message + '；也可以手动输入网络名称。';
  } finally {
    scanning = false;
    $('scan').disabled = false;
    $('scan').textContent = '重新扫描';
  }
}
$('scan').onclick = scanWifi;

async function load() {
  try {
    const [response, cityResponse] = await Promise.all([
      fetch('/api/state', {cache:'no-store'}), fetch('/api/cities', {cache:'no-store'})
    ]);
    if (!response.ok || !cityResponse.ok) throw Error('读取失败');
    const state = new URLSearchParams(await response.text());
    originalSSID = state.get('ssid') || '';
    hasPassword = state.get('has_password') === '1';
    $('open_wifi').checked = !!originalSSID && !hasPassword;
    $('ssid').replaceChildren(option('请选择 Wi-Fi', ''));
    if (originalSSID) {
      $('ssid').append(option(originalSSID + ' · 已保存', originalSSID));
      $('ssid').value = originalSSID;
    }
    $('manual_ssid').value = originalSSID;
    loadCities(await cityResponse.json(), state.get('city') || '');
    for (const name of ['brightness','day','dusk','night']) $(name).value = state.get(name) || '';
    $('auto_dim').checked = state.get('auto_dim') !== '0';
    $('night_brightness').value = state.get('night_brightness') || String(Math.min(Number(state.get('brightness')) || 25, 8));
    $('night_brightness').oninput();
    $('auto_dim').onchange();
    if (hasPassword) $('passwordHint').textContent = '已保存密码；留空保留原密码，修改网络时请填写新密码。';
    if (state.get('has_key') === '1') $('keyHint').textContent = '已保存天气私钥；留空保留，填写新值则替换。';
    statusBox.textContent = state.get('online') === '1' ?
      `设备已联网 · ${state.get('synced') === '1' ? '时间已同步' : '正在校时'}` : '请完成首次网络设置';
    $('weatherStatus').textContent = '实况：' + (state.get('weather') || '等待更新') + '；预报：' + (state.get('forecast') || '等待更新');
    $('brightness').oninput();
    button.disabled = form.dataset.updating === '1';
    await scanWifi();
  } catch (error) {
    statusBox.textContent = '无法读取设备，请确认手机与副屏连接到同一网络后刷新。';
  }
}

form.onsubmit = async event => {
  event.preventDefault();
  if (form.dataset.updating === '1') { result.textContent = '正在升级固件，请等待设备重启后再修改设置。'; return; }
  result.textContent = '';
  const values = new FormData(form);
  const ssid = $('manual_wifi').checked ? $('manual_ssid').value : $('ssid').value;
  values.set('ssid', ssid);
  values.set('auto_dim', $('auto_dim').checked ? '1' : '0');
  values.set('night_brightness', $('night_brightness').value);
  const password = values.get('password') || '';
  if (!ssid) { result.textContent = '请先选择 Wi-Fi。'; return; }
  if (new TextEncoder().encode(ssid).length > 32) { result.textContent = 'Wi-Fi 名称不能超过 32 字节。'; return; }
  if (!$('city').value) { result.textContent = '请选择省份和城市。'; return; }
  if (!/^\d{1,3}$/.test($('night_brightness').value) || Number($('night_brightness').value) < 1 || Number($('night_brightness').value) > 100) {
    result.textContent = '夜间亮度应为 1～100%。'; return;
  }
  if (!$('open_wifi').checked && ((!hasPassword || ssid !== originalSSID) && password.length < 8 || password && password.length < 8)) {
    result.textContent = '请填写至少 8 位 Wi-Fi 密码，或勾选开放网络。'; return;
  }
  if (!(values.get('day') < values.get('dusk') && values.get('dusk') < values.get('night'))) {
    result.textContent = '请按白天、黄昏、夜间的顺序设置开始时间。'; return;
  }
  button.disabled = true;
  form.dataset.saving = '1';
  button.textContent = '正在保存…';
  try {
    const response = await fetch('/api/save', {method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded','X-Case-Setup':'1'}, body:new URLSearchParams(values)});
    const message = await response.text();
    if (!response.ok) throw Error(message);
    result.textContent = message + '\n以后修改设置，可连接同一路由器后访问 casescreen.local。';
    button.textContent = '已保存';
  } catch (error) {
    delete form.dataset.saving;
    result.textContent = '保存未完成：' + error.message;
    button.disabled = false;
    button.textContent = '保存并重启';
  }
};
load();
