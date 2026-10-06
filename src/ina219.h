#pragma once

// INA219 current sensor on Port A (I2C), used to monitor servo supply
// current. Uses M5Unified's Ex_I2C bus, which is already wired to Port A's
// pins for the M5Dial.

// Configures the sensor's calibration + measurement mode. Returns true if
// the sensor acknowledged and the calibration write was verified.
bool ina219_init();

// True if ina219_init() succeeded. When false, ina219_read_current_ma()
// always returns 0.
bool ina219_is_available();

// Performs a live I2C read of the Current register, retrying internally on
// a transient bus error. On success, writes the result in milliamps to
// *out_ma and returns true (a small negative reading is normal ADC noise at
// near-zero load, not a failure - only a real I2C error returns false).
// Blocks for the duration of the I2C transaction(s).
bool ina219_read_current_ma(float *out_ma);
