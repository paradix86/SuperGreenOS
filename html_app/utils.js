const URL = 'http://192.168.1.11'
const DEBUG = false
const REQUEST_RETRIES = 3

const globalStatus = {
  root: null,
  message: null,
  retry: null,
  retryAction: null,
}

function initGlobalStatus() {
  globalStatus.root = document.getElementById('global_status')
  globalStatus.message = document.getElementById('global_status_message')
  globalStatus.retry = document.getElementById('global_status_retry')
  if (!globalStatus.root || !globalStatus.message || !globalStatus.retry) {
    return
  }
  globalStatus.retry.addEventListener('click', () => {
    if (!globalStatus.retryAction) {
      return
    }
    const action = globalStatus.retryAction
    action()
  })
  clearGlobalStatus()
}

function clearGlobalStatus() {
  if (!globalStatus.root || !globalStatus.message || !globalStatus.retry) {
    return
  }
  globalStatus.root.setAttribute('class', 'global_status hidden')
  globalStatus.message.innerText = ''
  globalStatus.retryAction = null
  globalStatus.retry.style.display = 'none'
}

function setGlobalStatus(message, level, retryAction) {
  if (!globalStatus.root || !globalStatus.message || !globalStatus.retry) {
    return
  }
  const statusLevel = level || 'error'
  globalStatus.root.setAttribute('class', `global_status ${statusLevel}`)
  globalStatus.message.innerText = message
  globalStatus.retryAction = retryAction || null
  globalStatus.retry.style.display = globalStatus.retryAction ? 'inline-block' : 'none'
}

function toHttpError(status, responseText, operation) {
  return {
    status: status,
    responseText: responseText,
    operation: operation,
  }
}

function formatHttpError(error) {
  if (!error) {
    return 'unknown error'
  }
  if (error.status) {
    return `HTTP ${error.status}`
  }
  return 'network error'
}

function notifyConnectionStatus(online) {
  if (typeof window.onConnectionStatusChanged == 'function') {
    window.onConnectionStatusChanged(online)
  }
}

function notifyLastSync(syncDate) {
  if (typeof window.onConnectionSync == 'function') {
    window.onConnectionSync(syncDate)
  }
}

const schedule_promise = (n, retries) => {
  let loading_param_promise = Promise.resolve(),
      promises = []
  return function(req_func, ret_func) {
    let resolve, reject
    const p = new Promise((res, rej) => {resolve = res; reject = rej})
    loading_param_promise.then(async () => {
      let error
      for (let i = 0; i < retries; ++i) {
        try {
          resolve(await req_func())
          return
        } catch(e) {
          error = e
          if (e.status == 404) {
            break
          }
          ret_func && ret_func(e, i + 1)
        }
      }
      reject(error)
    })
    promises.push(p)
    if (promises.length >= n) {
      loading_param_promise = Promise.all(promises).catch((e) => {
        console.log('promise.all', e)
      })
      promises = []
    }

    return p
  }
}
const fetchQueue = schedule_promise(3, REQUEST_RETRIES)

function queueRequest(label, req_func, options) {
  const opts = options || {}
  const silent = opts.silent === true
  const allowGlobalRetry = opts.allowGlobalRetry !== false
  const customRetryAction = typeof opts.retryAction == 'function' ? opts.retryAction : null
  return fetchQueue(
    req_func,
    (error, attempt) => {
      if (silent) {
        return
      }
      if (attempt < REQUEST_RETRIES && (!error || error.status !== 404)) {
        setGlobalStatus(`${label}: retry ${attempt}/${REQUEST_RETRIES}...`, 'warning')
      }
    })
    .then((value) => {
      notifyConnectionStatus(true)
      notifyLastSync(new Date())
      if (!silent) {
        clearGlobalStatus()
      }
      return value
    })
    .catch((error) => {
      if (!error || error.status === 0) {
        notifyConnectionStatus(false)
      }
      if (!silent) {
        const message = `${label} failed (${formatHttpError(error)}).`
        if (allowGlobalRetry) {
          const retryAction = customRetryAction || (() => {
            queueRequest(label, req_func, opts).catch(() => {})
          })
          setGlobalStatus(message, 'error', retryAction)
        } else {
          setGlobalStatus(message, 'error')
        }
      }
      throw error
    })
}

const fetchConfig = async function() {
  return queueRequest('Loading config', () => new Promise(function(resolve, reject) {
    const r = new XMLHttpRequest()
    r.open('GET', DEBUG ? '/config.json' : '/fs/config.json', true)
    r.onreadystatechange = function () {
      if (r.readyState != 4) return
      if (r.status != 200) {
        reject(toHttpError(r.status, r.responseText, 'fetchConfig'))
        return
      }
      try {
        resolve(JSON.parse(r.responseText))
      } catch (e) {
        reject(toHttpError(r.status, 'invalid json', 'fetchConfig'))
      }
    }
    r.onerror = () => reject(toHttpError(0, 'xhr error', 'fetchConfig'))
    r.send()
  }))
}

const fetchParam = async function(type, paramName, options) {
  return queueRequest(`Loading ${paramName}`, () => new Promise(function(resolve, reject) {
    const r = new XMLHttpRequest()
    r.open('GET', `${DEBUG ? URL : ''}/${type}?k=${paramName}`, true)
    r.onreadystatechange = function () {
      if (r.readyState != 4) return
      if (r.status != 200) {
        reject(toHttpError(r.status, r.responseText, 'fetchParam'))
        return
      }
      if (type == 'i') {
        resolve(parseInt(r.responseText, 10))
      } else {
        resolve(r.responseText)
      }
    }
    r.onerror = () => reject(toHttpError(0, 'xhr error', 'fetchParam'))
    r.send()
  }), options)
}

const updateParam = async function(type, paramName, value, options) {
  const opts = Object.assign({ allowGlobalRetry: false }, options || {})
  return queueRequest(`Updating ${paramName}`, () => new Promise(function(resolve, reject) {
    const r = new XMLHttpRequest()
    r.open('POST', `${DEBUG ? URL : ''}/${type}?k=${paramName}&v=${encodeURIComponent(value)}`, true)
    r.onreadystatechange = function () {
      if (r.readyState != 4) return
      if (r.status != 200) {
        reject(toHttpError(r.status, r.responseText, 'updateParam'))
        return
      }
      resolve(r.responseText)
    }
    r.onerror = () => reject(toHttpError(0, 'xhr error', 'updateParam'))
    r.send()
  }), opts)
}
