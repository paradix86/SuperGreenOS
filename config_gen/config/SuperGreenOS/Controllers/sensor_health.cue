package config

modules sensor_health: _MODULE & {
  init_priority: 35
}

modules sensor_health fields enabled: _INT8 & _NVS & _HTTP_RW & {
  nvs key: "SH_EN"
  default: 1
}

modules sensor_health fields period_s: _UINT16 & _NVS & _HTTP_RW & {
  nvs key: "SH_PER"
  default: 60
}

modules sensor_health fields warmup_samples: _UINT8 & _NVS & _HTTP_RW & {
  nvs key: "SH_WARM"
  default: 3
}

modules sensor_health fields stuck_samples: _UINT8 & _NVS & _HTTP_RW & {
  nvs key: "SH_STUCK"
  default: 5
}

modules sensor_health fields status: _INT8 & _HTTP & {
  default: 0
}

modules sensor_health fields last_alert: _STRING & _HTTP & {
  default: ""
}
