let config = {}
let arrays = {}
let modules = {}
let headerTimer = null
let importInProgress = false

const WIFI_STATUS_LABELS = {
  1: 'Disconnected',
  2: 'Connecting',
  3: 'Connected',
  4: 'Failed',
  5: 'AP mode',
}
const HEADER_REFRESH_MS = 15000

function formatClock(syncDate) {
  if (!syncDate || Number.isNaN(syncDate.getTime())) {
    return '-'
  }
  return syncDate.toLocaleTimeString()
}

function setConnectionBadge(online) {
  const status = document.getElementById('connection_status')
  if (!status) {
    return
  }
  status.innerText = online ? 'Online' : 'Offline'
  status.setAttribute('class', `connection_status ${online ? 'online' : 'offline'}`)
}

function setConnectionLastSync(syncDate) {
  const label = document.getElementById('connection_last_sync')
  if (!label) {
    return
  }
  label.innerText = `Last sync: ${formatClock(syncDate)}`
}

function isSensitiveField(field) {
  const key = `${field.name} ${field.caps_name}`.toLowerCase()
  return key.indexOf('reboot') >= 0 ||
    key.indexOf('ota_start') >= 0 ||
    key.indexOf('auth') >= 0 ||
    key.indexOf('signing') >= 0
}

function confirmSensitiveChange(field) {
  const label = field.name || field.caps_name
  return window.confirm(`Sensitive key "${label}" will be updated. Continue?`)
}

function downloadJson(filename, payload) {
  const blob = new Blob([JSON.stringify(payload, null, 2)], { type: 'application/json' })
  const url = window.URL.createObjectURL(blob)
  const link = document.createElement('a')
  link.href = url
  link.download = filename
  document.body.appendChild(link)
  link.click()
  document.body.removeChild(link)
  window.URL.revokeObjectURL(url)
}

async function exportCurrentConfig() {
  const btn = document.getElementById('export_config_btn')
  if (!btn || !config.keys || config.keys.length == 0) {
    return
  }
  btn.disabled = true
  setGlobalStatus('Exporting config...', 'warning')
  try {
    const values = {}
    const failed = []
    for (const key of config.keys) {
      try {
        values[key.caps_name] = await fetchParam(key.type.charAt(0), key.caps_name, { silent: true })
      } catch (e) {
        failed.push(key.caps_name)
      }
    }
    const payload = {
      schema: 'supergreenos.config.v1',
      device: config.name || '',
      exported_at: new Date().toISOString(),
      values: values,
    }
    const timestamp = new Date().toISOString().replace(/[:.]/g, '-')
    downloadJson(`supergreenos-config-${timestamp}.json`, payload)
    if (failed.length > 0) {
      setGlobalStatus(`Export completed with ${failed.length} read failures.`, 'warning')
    } else {
      setGlobalStatus('Export completed.', 'warning')
      setTimeout(() => clearGlobalStatus(), 2500)
    }
  } finally {
    btn.disabled = false
  }
}

function parseImportValues(payload) {
  if (!payload || typeof payload != 'object') {
    return null
  }
  if (payload.values && typeof payload.values == 'object') {
    return payload.values
  }
  return payload
}

function readFileAsText(file) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader()
    reader.onload = () => resolve(reader.result)
    reader.onerror = () => reject(new Error('Unable to read selected file'))
    reader.readAsText(file)
  })
}

async function applyImportedConfig(importValues) {
  const keyByCaps = new Map()
  const keyByName = new Map()
  config.keys.filter((k) => k.write).forEach((k) => {
    keyByCaps.set(k.caps_name, k)
    keyByName.set(k.name, k)
  })

  const pendingByCaps = new Map()
  const invalid = []
  Object.keys(importValues).forEach((rawKey) => {
    const key = keyByCaps.get(rawKey) || keyByName.get(rawKey)
    if (!key) {
      return
    }
    const normalized = normalizeFieldValue(key, importValues[rawKey])
    if (!normalized.valid) {
      invalid.push(key.name)
      return
    }
    pendingByCaps.set(key.caps_name, { field: key, value: normalized.value })
  })

  const changes = Array.from(pendingByCaps.values())
  if (changes.length == 0) {
    setGlobalStatus('Import file has no writable keys for this controller.', 'warning')
    return
  }

  const sensitive = changes.filter((c) => isSensitiveField(c.field))
  if (sensitive.length > 0) {
    const names = sensitive.slice(0, 6).map((c) => c.field.name).join(', ')
    const suffix = sensitive.length > 6 ? ', ...' : ''
    if (!window.confirm(`Import includes ${sensitive.length} sensitive key(s): ${names}${suffix}. Continue?`)) {
      setGlobalStatus('Import cancelled.', 'warning')
      return
    }
  }

  setGlobalStatus(`Importing ${changes.length} key(s)...`, 'warning')
  const failed = []
  for (const change of changes) {
    try {
      await updateParam(change.field.type.charAt(0), change.field.caps_name, change.value, { silent: true, allowGlobalRetry: false })
    } catch (e) {
      failed.push(change.field.name)
    }
  }

  await start()
  if (failed.length > 0 || invalid.length > 0) {
    setGlobalStatus(`Import done with errors (failed: ${failed.length}, invalid: ${invalid.length}).`, 'warning')
  } else {
    setGlobalStatus(`Import completed (${changes.length} key(s)).`, 'warning')
    setTimeout(() => clearGlobalStatus(), 2500)
  }
}

async function importConfigFromFile(file) {
  if (!file || importInProgress) {
    return
  }
  importInProgress = true
  try {
    const text = await readFileAsText(file)
    const payload = JSON.parse(text)
    const values = parseImportValues(payload)
    if (!values) {
      setGlobalStatus('Invalid import file format.', 'error')
      return
    }
    await applyImportedConfig(values)
  } catch (e) {
    setGlobalStatus(`Import failed (${e && e.message ? e.message : 'invalid file'}).`, 'error')
  } finally {
    importInProgress = false
  }
}

function initAppTools() {
  const exportBtn = document.getElementById('export_config_btn')
  if (exportBtn && exportBtn.dataset.bound != '1') {
    exportBtn.addEventListener('click', () => {
      exportCurrentConfig().catch(() => {
        setGlobalStatus('Export failed.', 'error')
      })
    })
    exportBtn.dataset.bound = '1'
  }

  const importBtn = document.getElementById('import_config_btn')
  const importInput = document.getElementById('import_config_input')
  if (importBtn && importInput && importBtn.dataset.bound != '1') {
    importBtn.addEventListener('click', () => importInput.click())
    importInput.addEventListener('change', (event) => {
      const file = event.target.files && event.target.files[0]
      importConfigFromFile(file).catch(() => {
        setGlobalStatus('Import failed.', 'error')
      })
      event.target.value = ''
    })
    importBtn.dataset.bound = '1'
  }

  setConnectionBadge(false)
  setConnectionLastSync(null)
}

function moduleTitle(text) {
  const title = document.createElement('h3')
  title.setAttribute('class', 'module_title')
  title.innerText = text
  return title
}

function arrayTitle(text, onShowHide) {
  const body = document.createElement('div')
  body.setAttribute('class', 'array_title')
  const title = document.createElement('h2')
  title.innerText = text
  body.appendChild(title)
  const expand = document.createElement('a')
  expand.innerText = 'show/hide'
  expand.setAttribute('href', 'javascript:void(0)')
  expand.addEventListener('click', onShowHide)
  body.appendChild(expand)
  return body
}

function parseIntegerValue(rawValue) {
  const value = `${rawValue}`.trim()
  if (!/^-?[0-9]+$/.test(value)) {
    return { valid: false, error: 'Integer expected' }
  }
  return { valid: true, value: parseInt(value, 10) }
}

function normalizeFieldValue(field, rawValue) {
  if (field.type == 'integer') {
    return parseIntegerValue(rawValue)
  }
  return { valid: true, value: `${rawValue}` }
}

function findKeyByNameOrCaps(candidates) {
  return config.keys.find((k) => candidates.indexOf(k.name) >= 0 || candidates.indexOf(k.caps_name) >= 0)
}

function setHeaderValue(id, value) {
  const el = document.getElementById(id)
  if (!el) {
    return
  }
  el.innerText = value
}

async function refreshHeaderRuntime() {
  const wifiStatusKey = findKeyByNameOrCaps(['wifi_status', 'WIFI_STATUS'])
  const wifiIpKey = findKeyByNameOrCaps(['wifi_ip', 'WIFI_IP'])
  const buildKey = findKeyByNameOrCaps(['ota_timestamp', 'OTA_TIMESTAMP'])

  if (buildKey) {
    try {
      const build = await fetchParam(buildKey.type.charAt(0), buildKey.caps_name, { silent: true })
      setHeaderValue('header_fw_build', `${build}`)
    } catch (e) {
      setHeaderValue('header_fw_build', '-')
    }
  } else {
    setHeaderValue('header_fw_build', '-')
  }

  if (wifiStatusKey) {
    try {
      const status = await fetchParam(wifiStatusKey.type.charAt(0), wifiStatusKey.caps_name, { silent: true })
      setHeaderValue('header_wifi_status', WIFI_STATUS_LABELS[status] || `Unknown (${status})`)
    } catch (e) {
      setHeaderValue('header_wifi_status', 'Offline')
    }
  } else {
    setHeaderValue('header_wifi_status', '-')
  }

  if (wifiIpKey) {
    try {
      const ip = await fetchParam(wifiIpKey.type.charAt(0), wifiIpKey.caps_name, { silent: true })
      setHeaderValue('header_wifi_ip', ip || '-')
    } catch (e) {
      setHeaderValue('header_wifi_ip', '-')
    }
  } else {
    setHeaderValue('header_wifi_ip', '-')
  }
}

function initHeader() {
  setHeaderValue('header_fw_name', config.name || '-')
  if (headerTimer) {
    clearInterval(headerTimer)
    headerTimer = null
  }
  refreshHeaderRuntime()
  headerTimer = setInterval(refreshHeaderRuntime, HEADER_REFRESH_MS)
}

window.onConnectionStatusChanged = (online) => {
  setConnectionBadge(online)
}

window.onConnectionSync = (syncDate) => {
  setConnectionBadge(true)
  setConnectionLastSync(syncDate)
}

function renderField(title, field) {
  const body = document.createElement('div')
  const baseClass = field.write ? 'field' : 'field ro'
  const setStatus = (status) => {
    body.setAttribute('class', status ? `${baseClass} ${status}` : baseClass)
  }

  const label = document.createElement('label')
  label.innerText = title
  body.appendChild(label)

  const error = document.createElement('div')
  error.setAttribute('class', 'error')

  let setValue, currentValue
  let saveBtn = null
  let refreshBtn = null
  const setButtonsDisabled = (disabled) => {
    if (saveBtn) {
      saveBtn.disabled = disabled
    }
    if (refreshBtn) {
      refreshBtn.disabled = disabled
    }
  }
  const fetchField = () => {
    setStatus('loading')
    setButtonsDisabled(true)
    fetchParam(field.type.charAt(0), field.caps_name, { retryAction: fetchField })
    .then(v => {
      currentValue = v
      setValue(v)
      setStatus('')
      error.innerText = ''
    })
    .catch((e) => {
      setStatus('')
      error.innerText = `Failed to load ${field.name}`;
    })
    .finally(() => {
      setButtonsDisabled(false)
    })
  }

  const updateModifiedState = (rawValue) => {
    const normalized = normalizeFieldValue(field, rawValue)
    if (!normalized.valid) {
      setStatus('modified')
      error.innerText = normalized.error
      return false
    }
    error.innerText = ''
    if (`${currentValue}` != `${normalized.value}`) {
      setStatus('modified')
    } else {
      setStatus('')
    }
    return true
  }

  const submitValue = (rawValue) => {
    const normalized = normalizeFieldValue(field, rawValue)
    if (!normalized.valid) {
      setStatus('modified')
      error.innerText = normalized.error
      return
    }
    if (`${currentValue}` == `${normalized.value}`) {
      setStatus('')
      error.innerText = ''
      return
    }
    if (isSensitiveField(field) && !confirmSensitiveChange(field)) {
      return
    }
    setStatus('loading')
    setButtonsDisabled(true)
    updateParam(field.type.charAt(0), field.caps_name, normalized.value)
      .then(() => {
        error.innerText = ''
        fetchField()
      })
      .catch(() => {
        setStatus('modified')
        setButtonsDisabled(false)
        error.innerText = `Failed to update ${field.name}`
      })
  }

  if (field.write && !field.indir) {
    const input = document.createElement('input')
    input.setAttribute('class', 'value')
    input.addEventListener('keyup', (e) => {
      if (e.key == 'Enter') {
        submitValue(e.target.value)
      } else {
        updateModifiedState(e.target.value)
      }
    })
    body.appendChild(input)
    const actions = document.createElement('div')
    actions.setAttribute('class', 'field_actions')
    saveBtn = document.createElement('button')
    saveBtn.setAttribute('type', 'button')
    saveBtn.innerText = 'Save'
    saveBtn.addEventListener('click', () => submitValue(input.value))
    refreshBtn = document.createElement('button')
    refreshBtn.setAttribute('type', 'button')
    refreshBtn.innerText = 'Refresh'
    refreshBtn.addEventListener('click', fetchField)
    actions.appendChild(saveBtn)
    actions.appendChild(refreshBtn)
    body.appendChild(actions)
    setValue = (v) => {input.value = v}
  } else if (field.write && field.indir) {
    const input = document.createElement('select')
    input.setAttribute('class', 'value')
    input.addEventListener('change', (e) => {
      updateModifiedState(e.target.value)
    })
    const opt = document.createElement('option')
    opt.value = 0
    opt.innerText = 'Disabled'
    input.appendChild(opt)
    field.indir.values.forEach((i, index) => {
      const opt = document.createElement('option')
      opt.value = i
      opt.innerText = field.indir.helpers[index] || i
      input.appendChild(opt)
    })
    body.appendChild(input)
    const actions = document.createElement('div')
    actions.setAttribute('class', 'field_actions')
    saveBtn = document.createElement('button')
    saveBtn.setAttribute('type', 'button')
    saveBtn.innerText = 'Save'
    saveBtn.addEventListener('click', () => submitValue(input.value))
    refreshBtn = document.createElement('button')
    refreshBtn.setAttribute('type', 'button')
    refreshBtn.innerText = 'Refresh'
    refreshBtn.addEventListener('click', fetchField)
    actions.appendChild(saveBtn)
    actions.appendChild(refreshBtn)
    body.appendChild(actions)
    setValue = (v) => {input.value = v}
  } else {
    const value = document.createElement('div')
    value.setAttribute('class', 'value')
    value.setAttribute('id', field.name)
    body.appendChild(value)
    setValue = (v) => {value.innerText = v}
  }
  body.appendChild(error)
  setStatus('loading')
  fetchField()
  return body
}

function renderModule(title, module, in_array) {
  const body = document.createElement('div')
  body.setAttribute('class', 'module')

  if (title) {
    body.appendChild(moduleTitle(title))
  }

  const fields = document.createElement('div')
  fields.setAttribute('class', 'fields')
  Object.keys(module).sort((m1, m2) => {
    if (module[m1].write && !module[m2].write) {
      return 1
    } else if (module[m2].write && !module[m1].write) {
      return -1
    }
    return m1.localeCompare(m2)
  }).forEach(f => {
    const field = document.createElement('div')
    let name = f
    if (in_array) {
      name = f.split('_').splice(2).join('_')
    } else if (f.indexOf(`${module[f].module}_`) == 0) {
      name = f.replace(new RegExp(`^${module[f].module}_`), '')
    }
    field.appendChild(renderField(name, module[f]))
    fields.appendChild(field)
  })
  body.appendChild(fields)
  return body
}

function renderModules(modules, array) {
  const body = document.createElement('div')
  Object.keys(modules).sort((m1, m2) => {
    if (m1 == array) {
      return -1
    } else if (m2 == array) {
      return 1
    }
    return m1.localeCompare(m2)
  }).forEach(m => {
    body.appendChild(renderModule(m == array ? '' : m, modules[m], !!array))
  })
  return body
}

function renderPagination(n, onSelect) {
  const body = document.createElement('div')
  body.setAttribute('class', 'pagination')

  const links = []
  for (let i = 0; i < n; ++i) {
    const a = document.createElement('a')
    links.push(a)
    const isel = i
    a.innerText = i+1
    a.setAttribute('href', 'javascript:void(0)')
    a.addEventListener('click', () => {
      links.forEach(l => l.setAttribute('class', ''))
      a.setAttribute('class', 'selected')
      onSelect(isel)
    }, false);
    body.appendChild(a)
  }
  links[0].setAttribute('class', 'selected')
  return body
}

function renderArray(title, array) {
  let shown = false;
  const body = document.createElement('div')
  const container = document.createElement('div')
  container.setAttribute('class', 'array_container')

  const render = () => {
    const modules = document.createElement('div')
    modules.appendChild(renderModules(array[0].modules, title))
    container.appendChild(modules)

    container.appendChild(renderPagination(array.length, (i) => {
      while (modules.firstChild) modules.removeChild(modules.firstChild)
      modules.appendChild(renderModules(array[i].modules, title))
    }))
  }

  body.appendChild(arrayTitle(title, () => {
    shown = !shown
    if (shown) {
      render()
    } else {
      while (container.firstChild) container.removeChild(container.firstChild)
    }
  }))
  body.appendChild(container)
  return body
}

function renderParams(data) {
  const body = document.createElement('div')
  body.setAttribute('class', 'params')

  Object.keys(data).sort((d1, d2) => {
    if (data[d1].array) {
      return 1
    } else if (data[d2].array) {
      return -1
    }
    return d1.localeCompare(d2)
  }).forEach(d => {
    if (data[d].array) {
      body.appendChild(renderArray(d, data[d].array))
    } else {
      body.appendChild(renderModules(data[d].modules))
    }
  })
  return body
}

function renderTopMenuItem(id, title, onSelect) {
  const body = document.createElement('div')
  body.setAttribute('class', `menu_item_${id}`)
  body.innerText = title
  body.addEventListener('click', onSelect)
  return body
}

function renderTopMenu(onSelect) {
  const body = document.createElement('div')
  body.setAttribute('class', 'menu modules')
  body.appendChild(renderTopMenuItem('modules', config.name, () => {
    body.setAttribute('class', 'menu modules')
    onSelect('modules')
  }))
  body.appendChild(renderTopMenuItem('system', 'System', () => {
    body.setAttribute('class', 'menu system')
    onSelect('system')
  }))
  return body
}

async function start() {
  config = await fetchConfig()
  modules = config.keys.reduce((acc, k) => {
    const d = acc[k.core ? 'system' : 'modules']
    const n = (k.array ? k.array.name : k.module)
    if (k.array) {
      const i = parseInt(k.name.split('_')[1])
      d[n] = d[n] || {array: []}
      d[n].array[i] = d[n].array[i] || {modules: {}}
      d[n].array[i].modules[k.module] = d[n].array[i].modules[k.module] || {}
      d[n].array[i].modules[k.module][k.name] = k
    } else {
      d[n] = d[n] || {modules: {}}
      d[n].modules[k.module] = d[n].modules[k.module] || {}
      d[n].modules[k.module][k.name] = k
    }
    return acc
  }, {system: {}, modules: {}})

  initHeader()

  const root = document.getElementById('body')
  while (root.firstChild) root.removeChild(root.firstChild)

  const menu = renderTopMenu((s) => {
    while (params.firstChild) params.removeChild(params.firstChild)
    params.appendChild(renderParams(modules[s]))
  })
  root.appendChild(menu)

  const params = document.createElement('div')
  params.appendChild(renderParams(modules.modules))
  root.appendChild(params)
}

window.onload = () => {
  initGlobalStatus()
  initAppTools()
  start().catch(() => {
    setGlobalStatus('UI startup failed. Please retry.', 'error', () => {
      start().catch(() => {})
    })
  })
}
