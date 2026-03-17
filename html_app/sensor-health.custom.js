(function() {
  function sensorHealthCard() {
    return document.getElementById("sensor_health_summary")
      ? document.getElementById("sensor_health_summary").closest(".automation_card")
      : null;
  }

  function ensureBackendControls() {
    var card = sensorHealthCard();
    if (!card || document.getElementById("sensor_health_backend_status")) {
      return;
    }

    var controls = card.querySelector(".automation_controls");
    if (!controls) {
      return;
    }

    var backendBlock = document.createElement("div");
    backendBlock.setAttribute("class", "automation_controls");
    backendBlock.innerHTML =
      '<div id="sensor_health_backend_status" class="automation_muted">Backend status: loading...</div>' +
      '<label>Backend period (s) <input id="sensor_health_period_s" type="number" step="1" min="5" value="60" /></label>' +
      '<label>Backend warmup samples <input id="sensor_health_warmup_samples" type="number" step="1" min="0" value="3" /></label>';

    controls.parentNode.insertBefore(backendBlock, controls.nextSibling);
  }

  function fieldByName(name) {
    return typeof L === "function" ? L([name, name.toUpperCase()]) : null;
  }

  async function readFieldValue(field) {
    if (!field || typeof s !== "function") {
      return null;
    }
    return s(field.type.charAt(0), field.caps_name, { silent: true, allowGlobalRetry: false });
  }

  function statusMeta(status) {
    switch (Number(status)) {
      case 1:
        return { badge: "ok", label: "OK" };
      case 3:
        return { badge: "warn", label: "WARN" };
      default:
        return { badge: "warmup", label: "UNKNOWN" };
    }
  }

  async function refreshSensorHealthBackend() {
    ensureBackendControls();

    var statusRoot = document.getElementById("sensor_health_backend_status");
    if (!statusRoot) {
      return;
    }

    var statusField = fieldByName("sensor_health_status");
    var alertField = fieldByName("sensor_health_last_alert");
    var enabledField = fieldByName("sensor_health_enabled");
    var periodField = fieldByName("sensor_health_period_s");
    var warmupField = fieldByName("sensor_health_warmup_samples");
    var stuckField = fieldByName("sensor_health_stuck_samples");

    if (!statusField || !alertField) {
      statusRoot.innerText = "Backend status: unavailable";
      return;
    }

    try {
      var values = await Promise.all([
        readFieldValue(statusField),
        readFieldValue(alertField),
        readFieldValue(enabledField),
        readFieldValue(periodField),
        readFieldValue(warmupField),
        readFieldValue(stuckField)
      ]);

      var meta = statusMeta(values[0]);
      statusRoot.innerHTML =
        'Backend status: <span class="health_badge ' + meta.badge + '">' + meta.label + "</span> " +
        '<span class="automation_muted">' + (values[1] || "ok") + "</span>";

      var enabledInput = document.getElementById("sensor_health_enabled");
      var periodInput = document.getElementById("sensor_health_period_s");
      var warmupInput = document.getElementById("sensor_health_warmup_samples");
      var stuckInput = document.getElementById("sensor_health_stuck_samples");

      if (enabledInput && values[2] !== null) {
        enabledInput.checked = Number(values[2]) !== 0;
      }
      if (periodInput && values[3] !== null) {
        periodInput.value = values[3];
      }
      if (warmupInput && values[4] !== null) {
        warmupInput.value = values[4];
      }
      if (stuckInput && values[5] !== null) {
        stuckInput.value = values[5];
      }
    } catch (err) {
      statusRoot.innerText = "Backend status: failed to load";
    }
  }

  function backendSaveBound() {
    var saveBtn = document.getElementById("sensor_health_save");
    return saveBtn && saveBtn.dataset.backendBound === "1";
  }

  function bindSensorHealthBackendSave() {
    var saveBtn = document.getElementById("sensor_health_save");
    if (!saveBtn || backendSaveBound()) {
      return;
    }

    saveBtn.dataset.backendBound = "1";
    saveBtn.addEventListener("click", async function() {
      if (typeof ce !== "function") {
        return;
      }

      var changed = 0;
      changed += await ce("sensor_health_enabled", document.getElementById("sensor_health_enabled").checked ? 1 : 0) ? 1 : 0;
      changed += await ce("sensor_health_period_s", Math.max(5, Math.round(Number(document.getElementById("sensor_health_period_s").value || 60)))) ? 1 : 0;
      changed += await ce("sensor_health_warmup_samples", Math.max(0, Math.round(Number(document.getElementById("sensor_health_warmup_samples").value || 3)))) ? 1 : 0;
      changed += await ce("sensor_health_stuck_samples", Math.max(3, Math.round(Number(document.getElementById("sensor_health_stuck_samples").value || 5)))) ? 1 : 0;

      await refreshSensorHealthBackend();
      if (typeof n === "function" && typeof t === "function") {
        n("Sensor health backend saved" + (changed ? " (" + changed + " key(s) updated)." : "."), "warning");
        setTimeout(function() { t(); }, 2000);
      }
    });
  }

  function setupSensorHealthBackendUi() {
    ensureBackendControls();
    bindSensorHealthBackendSave();
    refreshSensorHealthBackend().catch(function() {});
  }

  var originalXe = typeof xe === "function" ? xe : null;
  if (originalXe) {
    xe = async function() {
      var result = await originalXe.apply(this, arguments);
      setupSensorHealthBackendUi();
      return result;
    };
  }

  var originalOnload = window.onload;
  window.onload = function() {
    if (typeof originalOnload === "function") {
      originalOnload();
    }
    setTimeout(setupSensorHealthBackendUi, 250);
  };
})();
