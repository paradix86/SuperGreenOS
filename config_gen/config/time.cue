package config

modules time: _CORE_MODULE

// RAM field: the clock is persisted to NVS by the time module itself (every
// few minutes, key "TIME"), not on every tick, to spare the flash.
modules time fields time: _INT32 & _HTTP_RW & {
  name: "time"
  default: 0
  write_cb: true
}
