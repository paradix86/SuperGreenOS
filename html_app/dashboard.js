// Dashboard tab: read-only overview of the controller and its boxes.
//
// Everything here is *read* from the controller (one /mqttdiag JSON plus the
// /i and /s keys of the enabled boxes); the only writes are the four
// sensor_health_* settings behind an explicit Save button. Trend sparklines
// are sampled while this page is open and kept in this browser's
// localStorage: the controller has no history storage.

const DASH_REFRESH_MS = 30000
const DASH_SLOW_REFRESH_MS = 300000
const DASH_HISTORY_KEY = 'supergreen.dashboard.v1'
const DASH_HISTORY_MAX = 360
const DASH_LOW_HEAP_BYTES = 12288

const DASH_WIFI_CONNECTED = 3
const DASH_SENSOR_HEALTH_LABELS = { 0: 'Unknown', 1: 'OK', 3: 'Warning' }
const DASH_RESET_LABELS = { 1: 'Power-on', 3: 'Software', 4: 'Panic', 5: 'Interrupt watchdog', 6: 'Task watchdog', 7: 'Other watchdog', 8: 'Deep sleep', 9: 'Brownout' }
const DASH_OTA_LABELS = { 0: 'Idle', 1: 'In progress', 2: 'Disabled', 3: 'Failed' }
const DASH_TIMER_LABELS = { 0: 'Manual', 1: 'On/Off', 2: 'Season' }

const DASH_BOX_FAST = ['temp', 'humi', 'vpd', 'co2', 'weight', 'timer_output', 'led_dim', 'fan_duty', 'fan_ref', 'blower_duty', 'blower_ref', 'watering_power', 'watering_left', 'watering_last']
const DASH_BOX_SLOW = ['timer_type', 'on_hour', 'on_min', 'off_hour', 'off_min', 'watering_period', 'watering_duration', 'started_at', 'duration_days', 'fan_ref_source', 'fan_ref_min', 'fan_ref_max', 'blower_ref_source', 'blower_ref_min', 'blower_ref_max']
const DASH_TIMER_ONOFF = 1
const DASH_HEALTH_SETTINGS = ['sensor_health_enabled', 'sensor_health_period_s', 'sensor_health_warmup_samples', 'sensor_health_stuck_samples']

const dashboard = {
  root: null,
  timer: null,
  busy: false,
  slowAt: 0,
  slow: { boxes: {}, leds: null },
  history: null,
  showDisabled: false,
}

function dashKey(name) {
  return config.keys.find((k) => k.name == name)
}

function dashArrayLen(name) {
  const key = config.keys.find((k) => k.array && k.array.name == name)
  return key ? key.array.len : 0
}

// null when the key does not exist on this firmware or the read failed:
// the dashboard renders "-" instead of breaking the whole refresh
async function dashRead(name) {
  const key = dashKey(name)
  if (!key) {
    return null
  }
  try {
    return await fetchParam(key.type.charAt(0), key.caps_name, { silent: true, allowGlobalRetry: false })
  } catch (e) {
    return null
  }
}

async function dashReadMany(names) {
  const values = await Promise.all(names.map(dashRead))
  const out = {}
  names.forEach((name, i) => { out[name] = values[i] })
  return out
}

async function dashReadBox(i, params) {
  const values = await dashReadMany(params.map((p) => `box_${i}_${p}`))
  const out = {}
  params.forEach((p) => { out[p] = values[`box_${i}_${p}`] })
  return out
}

function dashEsc(text) {
  return `${text == null ? '' : text}`.replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]))
}

function dashNum(value, digits, unit) {
  if (value == null || !Number.isFinite(Number(value))) {
    return '-'
  }
  return `${Number(value).toFixed(digits || 0)}${unit || ''}`
}

function dashPad(value) {
  return `${value}`.padStart(2, '0')
}

function dashDuration(seconds) {
  if (seconds == null || !Number.isFinite(seconds) || seconds < 0) {
    return '-'
  }
  const d = Math.floor(seconds / 86400)
  const h = Math.floor((seconds % 86400) / 3600)
  const m = Math.floor((seconds % 3600) / 60)
  if (d > 0) {
    return `${d}d ${h}h`
  }
  if (h > 0) {
    return `${h}h ${dashPad(m)}m`
  }
  if (m > 0) {
    return `${m}m`
  }
  return `${Math.floor(seconds)}s`
}

function dashAgo(epochSeconds) {
  if (!epochSeconds || epochSeconds <= 0) {
    return 'never'
  }
  const delta = Math.floor(Date.now() / 1000) - epochSeconds
  if (delta < 0) {
    return new Date(epochSeconds * 1000).toLocaleString()
  }
  return `${dashDuration(delta)} ago`
}

function dashChip(label, level) {
  return `<span class="chip ${level || ''}">${dashEsc(label)}</span>`
}

function dashRow(label, value) {
  return `<div class="dash_row"><span>${dashEsc(label)}</span><strong>${value}</strong></div>`
}

function dashBar(label, value) {
  const pct = value == null ? 0 : Math.max(0, Math.min(100, Number(value)))
  return `<div class="dash_bar"><span>${dashEsc(label)}</span><i><b style="width:${pct}%"></b></i><strong>${dashNum(value, 0, '%')}</strong></div>`
}

function dashIndirLabel(name, value) {
  const key = dashKey(name)
  if (!key || !key.indir || value == null) {
    return null
  }
  const index = key.indir.values.indexOf(Number(value))
  return index >= 0 ? key.indir.helpers[index] : null
}

// "fan follows: SHT21 humidity on port #1 · 60 (55..65)" plus a marker showing
// where the reference sits inside its min..max band: this is what decides the
// duty, so a wrong band is visible at a glance. VPD sources are kPa * 100.
function dashRefBand(label, sourceLabel, ref, min, max) {
  if (!sourceLabel) {
    return ''
  }
  const isVpd = /vpd/i.test(sourceLabel)
  const fmt = (v) => (v == null ? '-' : dashNum(isVpd ? v / 100 : v, isVpd ? 2 : 0))
  let marker = ''
  if (ref != null && min != null && max != null && Number(max) > Number(min)) {
    const pct = Math.max(0, Math.min(100, ((Number(ref) - Number(min)) / (Number(max) - Number(min))) * 100))
    marker = `<i><b style="left:${pct.toFixed(0)}%"></b></i>`
  }
  return `<div class="dash_ref"><span>${dashEsc(label)} follows: ${dashEsc(sourceLabel)} · ${fmt(ref)} (${fmt(min)}..${fmt(max)})</span>${marker}</div>`
}

// next on/off switch of an On/Off timer, from the phone clock
function dashNextTimerEvent(day, slow) {
  if (Number(slow.timer_type) != DASH_TIMER_ONOFF || slow.on_hour == null || slow.off_hour == null) {
    return ''
  }
  const now = new Date()
  const nowMin = now.getHours() * 60 + now.getMinutes()
  const target = day
    ? Number(slow.off_hour) * 60 + Number(slow.off_min || 0)
    : Number(slow.on_hour) * 60 + Number(slow.on_min || 0)
  let wait = target - nowMin
  if (wait <= 0) {
    wait += 1440
  }
  return `${day ? 'lights off' : 'lights on'} at ${dashPad(Math.floor(target / 60))}:${dashPad(target % 60)} (in ${dashDuration(wait * 60)})`
}

function dashCollectAlerts(diag, health, boxes) {
  const alerts = []
  if (diag) {
    if (diag.wifi_status != DASH_WIFI_CONNECTED) alerts.push(['bad', 'Wi-Fi not connected'])
    if (diag.mqtt_connected != 1) alerts.push(['warn', 'MQTT broker not connected'])
    if (!diag.time_valid) alerts.push(['warn', 'Clock not synced: timers may be off'])
    if (diag.ota_status == 3) alerts.push(['warn', 'Last OTA failed'])
    if (Number(diag.heap_free) < DASH_LOW_HEAP_BYTES) alerts.push(['bad', `Free heap is low right now (${dashNum(diag.heap_free / 1024, 1, ' KB')})`])
    if ([4, 5, 6, 7, 9].indexOf(Number(diag.reset_reason)) >= 0) alerts.push(['bad', `Last reboot was a ${DASH_RESET_LABELS[diag.reset_reason].toLowerCase()}`])
  } else {
    alerts.push(['warn', '/mqttdiag not available: controller health unknown'])
  }
  if (Number(health.sensor_health_status) == 3) alerts.push(['warn', `Sensor health: ${health.sensor_health_last_alert || 'warning'}`])
  Object.keys(boxes).forEach((i) => {
    const b = boxes[i]
    if (b.enabled == 1 && Number(b.watering_power) > 0 && Number(b.watering_left) == 0) alerts.push(['warn', `Box ${Number(i) + 1}: watering has no cycles left`])
    if (b.enabled == 1 && b.temp == null && b.humi == null) alerts.push(['warn', `Box ${Number(i) + 1}: no sensor readings`])
  })
  return alerts
}

function dashRenderAlerts(alerts) {
  if (alerts.length == 0) {
    return `<div class="dash_alerts">${dashChip('All good', 'ok')}</div>`
  }
  return `<div class="dash_alerts">${alerts.map((a) => dashChip(a[1], a[0])).join('')}</div>`
}

// --- history / sparklines ---------------------------------------------------

function dashLoadHistory() {
  let stored = null
  try {
    stored = JSON.parse(window.localStorage.getItem(DASH_HISTORY_KEY))
  } catch (e) {
    stored = null
  }
  if (!stored || !Array.isArray(stored.at) || !stored.box || typeof stored.box != 'object') {
    return { at: [], box: {} }
  }
  return stored
}

function dashSaveHistory() {
  try {
    window.localStorage.setItem(DASH_HISTORY_KEY, JSON.stringify(dashboard.history))
  } catch (e) {
    // private mode or quota exceeded: trends simply do not persist
  }
}

function dashPushHistory(boxes) {
  const h = dashboard.history
  h.at.push(Date.now())
  Object.keys(boxes).forEach((i) => {
    const series = h.box[i] || (h.box[i] = { temp: [], humi: [], vpd: [] })
    ;['temp', 'humi', 'vpd'].forEach((p) => {
      while (series[p].length < h.at.length - 1) {
        series[p].push(null)
      }
      series[p].push(boxes[i][p])
    })
  })
  const overflow = h.at.length - DASH_HISTORY_MAX
  if (overflow > 0) {
    h.at.splice(0, overflow)
    Object.keys(h.box).forEach((i) => {
      Object.keys(h.box[i]).forEach((p) => {
        h.box[i][p].splice(0, Math.max(0, h.box[i][p].length - DASH_HISTORY_MAX))
      })
    })
  }
  dashSaveHistory()
}

function dashSparkline(values, scale) {
  const points = (values || []).map((v) => (v == null ? null : Number(v) * (scale || 1)))
  const known = points.filter((v) => v != null && Number.isFinite(v))
  if (known.length < 2) {
    return '<span class="dash_muted">collecting...</span>'
  }
  const min = Math.min.apply(null, known)
  const max = Math.max.apply(null, known)
  const span = max - min || 1
  const path = points.map((v, i) => {
    if (v == null || !Number.isFinite(v)) {
      return ''
    }
    const x = (i / (points.length - 1)) * 100
    const y = 26 - ((v - min) / span) * 24
    return `${x.toFixed(1)},${y.toFixed(1)}`
  }).filter((p) => p).join(' ')
  return `<svg class="spark" viewBox="0 0 100 28" preserveAspectRatio="none"><polyline points="${path}"/></svg><span class="spark_range">${dashNum(min, 1)} .. ${dashNum(max, 1)}</span>`
}

// --- rendering ---------------------------------------------------------------

function dashRenderController(diag, health) {
  if (!diag) {
    return '<div class="dash_muted">/mqttdiag not available on this firmware.</div>'
  }
  const wifiOk = diag.wifi_status == DASH_WIFI_CONNECTED
  const mqttOk = diag.mqtt_connected == 1
  const heapMin = Number(diag.heap_min_free)
  const heapLow = Number.isFinite(heapMin) && heapMin < DASH_LOW_HEAP_BYTES
  const resetLabel = DASH_RESET_LABELS[diag.reset_reason] || `Reason ${diag.reset_reason}`
  const resetBad = [4, 5, 6, 7, 9].indexOf(Number(diag.reset_reason)) >= 0
  const healthStatus = Number(health.sensor_health_status)
  const healthLabel = DASH_SENSOR_HEALTH_LABELS[healthStatus] || `Status ${healthStatus}`
  const heapExtra = diag.heap_min_free_at != null
    ? ` · at ${dashDuration(Number(diag.heap_min_free_at))} · ${diag.heap_low_events} low`
    : ''
  return `
    <div class="dash_chips">
      ${dashChip(wifiOk ? 'Wi-Fi connected' : 'Wi-Fi down', wifiOk ? 'ok' : 'bad')}
      ${dashChip(mqttOk ? 'MQTT connected' : 'MQTT down', mqttOk ? 'ok' : 'warn')}
      ${dashChip(diag.time_valid ? 'Clock synced' : 'Clock not synced', diag.time_valid ? 'ok' : 'warn')}
      ${dashChip(`OTA ${DASH_OTA_LABELS[diag.ota_status] || diag.ota_status}`, diag.ota_status == 0 ? 'ok' : 'warn')}
    </div>
    ${dashRow('Uptime', dashDuration(Number(diag.uptime_s)))}
    ${dashRow('Free heap', `${dashNum(diag.heap_free / 1024, 1, ' KB')} <span class="dash_muted">min ${dashNum(heapMin / 1024, 1, ' KB')}${dashEsc(heapExtra)}</span>`)}
    ${heapLow ? '<div class="dash_note warn">Heap dipped below 12 KB since boot: keep an eye on it.</div>' : ''}
    ${dashRow('Restarts', dashEsc(diag.n_restarts))}
    ${dashRow('Last reset', dashChip(resetLabel, resetBad ? 'bad' : 'ok'))}
    ${diag.reset_history ? dashRow('Reset history', `<span class="dash_small">${dashEsc(`${diag.reset_history}`.split(',').map((r) => DASH_RESET_LABELS[r] || r).join(', '))}</span>`) : ''}
    ${diag.nvs_used != null ? dashRow('NVS entries', `${dashEsc(diag.nvs_used)} used / ${dashEsc(diag.nvs_free)} free`) : ''}
    ${dashRow('Broker', `<span class="dash_small">${dashEsc(diag.broker_url)}</span>`)}
    ${dashRow('Sensor health', `${dashChip(healthLabel, healthStatus == 1 ? 'ok' : healthStatus == 3 ? 'warn' : '')} <span class="dash_small">${dashEsc(health.sensor_health_last_alert || 'no alert')}</span>`)}
  `
}

function dashRenderBox(i, fast, slow, history) {
  const enabled = fast.enabled == 1
  const day = Number(fast.timer_output) > 0
  const schedule = slow.on_hour != null
    ? `${dashPad(slow.on_hour)}:${dashPad(slow.on_min || 0)} → ${dashPad(slow.off_hour)}:${dashPad(slow.off_min || 0)}`
    : '-'
  const timerLabel = DASH_TIMER_LABELS[slow.timer_type] || '-'
  let season = ''
  if (Number(slow.started_at) > 0) {
    const dayNumber = Math.floor((Date.now() / 1000 - Number(slow.started_at)) / 86400) + 1
    season = dashRow('Season', `day ${dayNumber}${slow.duration_days ? ` of ${slow.duration_days}` : ''}`)
  }
  let watering = 'off'
  if (Number(fast.watering_power) > 0) {
    const left = Number(fast.watering_left)
    const leftLabel = left < 0 ? 'unlimited' : left == 0 ? 'no cycles left' : `${left} cycle(s) left`
    watering = `${dashNum(fast.watering_power, 0, '%')} every ${dashDuration(Number(slow.watering_period))} for ${dashDuration(Number(slow.watering_duration))}, ${leftLabel}`
  }
  const series = history.box[i] || { temp: [], humi: [], vpd: [] }
  const fanSource = dashIndirLabel(`box_${i}_fan_ref_source`, slow.fan_ref_source)
  const blowerSource = dashIndirLabel(`box_${i}_blower_ref_source`, slow.blower_ref_source)
  return `
    <div class="dash_card ${enabled ? '' : 'disabled'}">
      <div class="dash_card_title">
        <h3>Box ${i + 1}</h3>
        <div class="dash_chips">
          ${enabled ? dashChip(day ? `Day · ${dashNum(fast.timer_output, 0, '%')}` : 'Night', day ? 'day' : 'night') : dashChip('Disabled', '')}
          ${dashChip(`${timerLabel} ${schedule}`, '')}
        </div>
      </div>
      <div class="dash_readings">
        <div><b>${dashNum(fast.temp, 0)}</b><span>°C</span></div>
        <div><b>${dashNum(fast.humi, 0)}</b><span>% RH</span></div>
        <div><b>${dashNum(fast.vpd == null ? null : fast.vpd / 100, 2)}</b><span>kPa VPD</span></div>
        ${Number(fast.co2) > 0 ? `<div><b>${dashNum(fast.co2, 0)}</b><span>ppm CO₂</span></div>` : ''}
        ${fast.weight != null && Number(fast.weight) != 0 ? `<div><b>${dashNum(fast.weight, 0)}</b><span>weight</span></div>` : ''}
      </div>
      <div class="dash_sparks">
        <div><span>Temp 3h</span>${dashSparkline(series.temp)}</div>
        <div><span>RH 3h</span>${dashSparkline(series.humi)}</div>
        <div><span>VPD 3h</span>${dashSparkline(series.vpd, 0.01)}</div>
      </div>
      ${enabled ? `<div class="dash_small dash_muted">${dashEsc(dashNextTimerEvent(day, slow))}</div>` : ''}
      ${dashBar('LED', fast.led_dim)}
      ${dashBar('Fan', fast.fan_duty)}
      ${dashRefBand('fan', fanSource, fast.fan_ref, slow.fan_ref_min, slow.fan_ref_max)}
      ${dashBar('Blower', fast.blower_duty)}
      ${dashRefBand('blower', blowerSource, fast.blower_ref, slow.blower_ref_min, slow.blower_ref_max)}
      ${dashRow('Watering', `<span class="dash_small">${dashEsc(watering)}</span>`)}
      ${Number(fast.watering_power) > 0 ? dashRow('Last watering', dashAgo(Number(fast.watering_last))) : ''}
      ${season}
    </div>
  `
}

function dashRenderLeds(leds) {
  if (leds.length == 0) {
    return ''
  }
  const rows = leds.map((led, i) => `
    <tr>
      <td>LED ${i + 1}</td>
      <td>${led.box == null ? '-' : `Box ${Number(led.box) + 1}`}</td>
      <td>${dashNum(led.duty, 0, '%')}</td>
      <td>${dashNum(led.dim, 0, '%')}</td>
    </tr>
  `).join('')
  return `
    <div class="dash_card">
      <div class="dash_card_title"><h3>LEDs</h3></div>
      <table class="dash_table">
        <thead><tr><th>Channel</th><th>Box</th><th>Duty</th><th>Dim</th></tr></thead>
        <tbody>${rows}</tbody>
      </table>
    </div>
  `
}

function dashFillHealthSettings(settings) {
  const form = dashboard.root && dashboard.root.querySelector('#dash_health_form')
  if (!form || form.dataset.dirty == '1') {
    return
  }
  form.querySelector('[name=sensor_health_enabled]').checked = Number(settings.sensor_health_enabled) == 1
  ;['sensor_health_period_s', 'sensor_health_warmup_samples', 'sensor_health_stuck_samples'].forEach((name) => {
    form.querySelector(`[name=${name}]`).value = settings[name] == null ? '' : settings[name]
  })
  form.hidden = settings.sensor_health_enabled == null
}

async function dashSaveHealthSettings(form) {
  const button = form.querySelector('button')
  button.disabled = true
  try {
    const wanted = {
      sensor_health_enabled: form.querySelector('[name=sensor_health_enabled]').checked ? 1 : 0,
      sensor_health_period_s: parseInt(form.querySelector('[name=sensor_health_period_s]').value, 10),
      sensor_health_warmup_samples: parseInt(form.querySelector('[name=sensor_health_warmup_samples]').value, 10),
      sensor_health_stuck_samples: parseInt(form.querySelector('[name=sensor_health_stuck_samples]').value, 10),
    }
    if (!(wanted.sensor_health_period_s >= 5) || !(wanted.sensor_health_warmup_samples >= 0) || !(wanted.sensor_health_stuck_samples >= 1)) {
      setGlobalStatus('Sensor health: period >= 5 s, warmup >= 0, stuck samples >= 1.', 'error')
      return
    }
    const current = await dashReadMany(DASH_HEALTH_SETTINGS)
    let changed = 0
    for (const name of DASH_HEALTH_SETTINGS) {
      if (Number(current[name]) == wanted[name]) {
        continue
      }
      await updateParam('i', dashKey(name).caps_name, wanted[name], { silent: true, allowGlobalRetry: false })
      changed += 1
    }
    form.dataset.dirty = '0'
    setGlobalStatus(changed ? `Sensor health settings saved (${changed} key(s)).` : 'Sensor health settings unchanged.', 'warning')
    setTimeout(() => clearGlobalStatus(), 2500)
  } catch (e) {
    setGlobalStatus('Sensor health settings: save failed.', 'error')
  } finally {
    button.disabled = false
  }
}

// --- refresh loop ----------------------------------------------------------

async function dashRefresh() {
  if (dashboard.busy || !dashboard.root) {
    return
  }
  dashboard.busy = true
  const root = dashboard.root
  try {
    const now = Date.now()
    const slowDue = now - dashboard.slowAt >= DASH_SLOW_REFRESH_MS
    const diag = await fetchJson('/mqttdiag', { silent: true, allowGlobalRetry: false }).catch(() => null)
    const health = await dashReadMany(['sensor_health_status', 'sensor_health_last_alert'])
    if (slowDue) {
      dashFillHealthSettings(await dashReadMany(DASH_HEALTH_SETTINGS))
    }

    const nBoxes = dashArrayLen('box')
    const boxes = {}
    for (let i = 0; i < nBoxes; ++i) {
      const enabled = await dashRead(`box_${i}_enabled`)
      if (enabled != 1 && !dashboard.showDisabled) {
        boxes[i] = { enabled: enabled }
        continue
      }
      boxes[i] = Object.assign({ enabled: enabled }, await dashReadBox(i, DASH_BOX_FAST))
      if (slowDue || !dashboard.slow.boxes[i]) {
        dashboard.slow.boxes[i] = await dashReadBox(i, DASH_BOX_SLOW)
      }
    }

    if (slowDue || !dashboard.slow.leds) {
      const ledBoxes = []
      for (let i = 0; i < dashArrayLen('led'); ++i) {
        ledBoxes.push(await dashRead(`led_${i}_box`))
      }
      dashboard.slow.leds = ledBoxes
    }
    const leds = []
    for (let i = 0; i < dashboard.slow.leds.length; ++i) {
      const values = await dashReadMany([`led_${i}_duty`, `led_${i}_dim`])
      leds.push({ box: dashboard.slow.leds[i], duty: values[`led_${i}_duty`], dim: values[`led_${i}_dim`] })
    }
    if (slowDue) {
      dashboard.slowAt = now
    }

    if (dashboard.root !== root) {
      return // the tab was left while we were reading
    }

    const sampled = {}
    Object.keys(boxes).forEach((i) => {
      if (boxes[i].enabled == 1) {
        sampled[i] = boxes[i]
      }
    })
    dashPushHistory(sampled)

    root.querySelector('#dash_alerts').innerHTML = dashRenderAlerts(dashCollectAlerts(diag, health, boxes))
    root.querySelector('#dash_controller').innerHTML = dashRenderController(diag, health)
    root.querySelector('#dash_boxes').innerHTML = Object.keys(boxes)
      .filter((i) => boxes[i].enabled == 1 || dashboard.showDisabled)
      .map((i) => dashRenderBox(Number(i), boxes[i], dashboard.slow.boxes[i] || {}, dashboard.history))
      .join('') || '<div class="dash_muted">No enabled box. Tick "show disabled boxes" to see them all.</div>'
    root.querySelector('#dash_leds').innerHTML = dashRenderLeds(leds)
    root.querySelector('#dash_updated').innerText = `Updated ${new Date().toLocaleTimeString()} · trends sampled while this page is open`
  } finally {
    dashboard.busy = false
  }
}

function stopDashboard() {
  if (dashboard.timer) {
    clearInterval(dashboard.timer)
    dashboard.timer = null
  }
  dashboard.root = null
}

function renderDashboard() {
  stopDashboard()
  dashboard.history = dashLoadHistory()
  dashboard.slowAt = 0
  const root = document.createElement('div')
  root.setAttribute('class', 'dash')
  root.innerHTML = `
    <div class="dash_toolbar">
      <label><input id="dash_show_disabled" type="checkbox" ${dashboard.showDisabled ? 'checked' : ''}> show disabled boxes</label>
      <button id="dash_refresh" type="button">Refresh now</button>
      <span id="dash_updated" class="dash_muted">Loading...</span>
    </div>
    <div id="dash_alerts"></div>
    <div class="dash_grid">
      <div class="dash_card">
        <div class="dash_card_title"><h3>Controller</h3></div>
        <div id="dash_controller"><div class="dash_muted">Loading...</div></div>
        <form id="dash_health_form" class="dash_form" hidden>
          <span class="dash_form_title">Sensor health (firmware)</span>
          <label class="inline"><input type="checkbox" name="sensor_health_enabled"> enabled</label>
          <label>period (s) <input type="number" name="sensor_health_period_s" min="5" step="1"></label>
          <label>warmup samples <input type="number" name="sensor_health_warmup_samples" min="0" step="1"></label>
          <label>stuck samples <input type="number" name="sensor_health_stuck_samples" min="1" step="1"></label>
          <button type="submit">Save</button>
        </form>
      </div>
      <div>
        <div id="dash_boxes" class="dash_boxes"></div>
        <div id="dash_leds" class="dash_leds"></div>
      </div>
    </div>
  `
  root.querySelector('#dash_show_disabled').addEventListener('change', (e) => {
    dashboard.showDisabled = e.target.checked
    dashRefresh().catch(() => {})
  })
  root.querySelector('#dash_refresh').addEventListener('click', () => {
    dashRefresh().catch(() => {})
  })
  const form = root.querySelector('#dash_health_form')
  form.addEventListener('input', () => { form.dataset.dirty = '1' })
  form.addEventListener('submit', (e) => {
    e.preventDefault()
    dashSaveHealthSettings(form).catch(() => {})
  })
  dashboard.root = root
  dashRefresh().catch(() => {})
  dashboard.timer = setInterval(() => {
    dashRefresh().catch(() => {})
  }, DASH_REFRESH_MS)
  return root
}
