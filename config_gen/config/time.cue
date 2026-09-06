package config

modules time: _CORE_MODULE

// RAM field: the clock is persisted to NVS by the time module itself (every
// few minutes, key "TIME"), not on every tick, to spare the flash.
modules time fields time: _INT32 & _HTTP_RW & {
  name: "time"
  default: 0
  write_cb: true
}

// POSIX TZ string applied with setenv("TZ")/tzset(), e.g. "CET-1CEST,M3.5.0,M10.5.0/3"
// for Italy. Empty (the default) keeps the historical behaviour: schedules in UTC.
modules time fields tz: _STRING & _NVS & _HTTP_RW & {
  name: "time_tz"
  remote: false
  nvs key: "TIME_TZ"
  default: ""
  write_cb: true
}
