#pragma once

#include <cstdint>

// ---------------------------------------------------------------------------
// Servo PWM output (Port B)
// ---------------------------------------------------------------------------
// M5Dial Port B exposes GND, 5V, G1 (GPIO1) and G1 (GPIO1, closest to 5V).
// The servo signal wire is connected to G1; power the servo from Port B's
// 5V/GND pins (fine for a single micro/standard servo; use a separate supply
// if the servo stalls or draws more current than the M5Dial's regulator
// can provide).
constexpr int SERVO_PWM_PIN = 1; // GPIO1 / Port B "G1"

constexpr int SERVO_PWM_FREQ_HZ = 50;     // Standard analog servo frequency
constexpr int SERVO_PWM_RESOLUTION_BITS = 12; // 4096 steps @ 50Hz -> ~4.9us/step
constexpr int SERVO_PULSE_MIN_US = 850;  // Pulse width at 0% (bottom limit)
constexpr int SERVO_PULSE_MAX_US = 1750; // Pulse width at 100% (top limit)

// ---------------------------------------------------------------------------
// INA219 current sensor (Port A, I2C)
// ---------------------------------------------------------------------------
// M5Dial Port A is I2C: SDA = GPIO13, SCL = GPIO15 (M5Unified's Ex_I2C bus,
// already wired to those pins for this board - see ina219_init()).
constexpr uint8_t INA219_I2C_ADDRESS = 0x40; // Default breakout address (A0/A1 tied low)

// Calibration inputs, per the INA219 datasheet's calibration procedure.
// Defaults match a standard breakout's onboard 0.1ohm shunt with a +-2A
// range; adjust to match your board/shunt if different.
constexpr float INA219_SHUNT_RESISTANCE_OHMS = 0.1f;
constexpr float INA219_MAX_EXPECTED_CURRENT_A = 2.0f;

constexpr uint32_t INA219_SAMPLE_INTERVAL_MS = 200; // How often to poll the sensor

// If every poll has failed (after ina219_read_current_ma()'s own internal
// retries) for this long, give up and show "--" instead of a stale reading.
// The same rule decides the sensor has failed, which blocks starting the
// automated test or stops one that's running.
constexpr uint32_t INA219_STALE_TIMEOUT_MS = 2000;

// Current colour bands, shared by the live mA readout, the test's arc bands
// and the end-of-travel STICKING/OK verdicts:
//   below CURRENT_AMBER_MA              -> green ("OK")
//   CURRENT_AMBER_MA .. CURRENT_RED_MA  -> amber ("STICKING")
//   CURRENT_RED_MA and above            -> red   ("STICKING")
constexpr float CURRENT_AMBER_MA = 200.0f;
constexpr float CURRENT_RED_MA = 400.0f;

// ---------------------------------------------------------------------------
// Automated test (started by the ring button)
// ---------------------------------------------------------------------------
// If the servo isn't already at the top limit when the test is started, it's
// sent there immediately and given this long to arrive before sweeping.
constexpr uint32_t TEST_PREPOSITION_MS = 1000;

// Travel top->bottom and bottom->top in TEST_STEP_COUNT equal steps (one
// coloured arc segment each). At each step the servo is sent straight to
// the next position, then:
//   pause TEST_STEP_SETTLE_MS -> reading -> pause TEST_STEP_GAP_MS -> next step
// The reading is the peak over the last TEST_STEP_READ_WINDOW_MS of the
// settle pause.
constexpr int TEST_STEP_COUNT = 5;
constexpr uint32_t TEST_STEP_SETTLE_MS = 1000;
constexpr uint32_t TEST_STEP_READ_WINDOW_MS = 200;
constexpr uint32_t TEST_STEP_GAP_MS = 100;

constexpr uint32_t TEST_DWELL_MS = 2000;        // Pause at each end of travel
constexpr uint32_t TEST_DWELL_SETTLE_MS = 1000; // Start of the pause ignored for the verdict

// How often to read the INA219 while a test is running, so each reading's
// peak catches short spikes. (Outside a test, INA219_SAMPLE_INTERVAL_MS.)
constexpr uint32_t TEST_CURRENT_POLL_MS = 20;

// ---------------------------------------------------------------------------
// Rotary encoder / outer ring
// ---------------------------------------------------------------------------
// If turning the ring clockwise makes the value go DOWN instead of up on your
// unit, flip this to true.
constexpr bool ENCODER_INVERT_DIRECTION = false;

// The M5Dial's encoder is a standard quadrature encoder with 16 detents per
// revolution at 4 quadrature counts per detent (64 counts/rev). One "detent"
// of rotation is therefore 4 raw counts from M5Dial.Encoder.read().
constexpr int ENCODER_COUNTS_PER_DETENT = 4;

// Acceleration curve: the % step applied per detent depends on how quickly
// the user is turning the dial (time between detents, in milliseconds).
// Tune these to taste.
struct EncoderAccelStep {
    uint32_t max_interval_ms; // if time-between-detents <= this, use this step
    float step_percent;
};
constexpr EncoderAccelStep ENCODER_ACCEL_TABLE[] = {
    {20, 40.0f},  // very fast flick (~3+ rev/sec)
    {50, 20.0f},  // fast spin (~2 rev/sec)
    {100, 10.0f}, // brisk turn (~1 rev/sec)
    {220, 5.0f},  // moderate turn (~0.5 rev/sec)
    {UINT32_MAX, 3.0f}, // slow, deliberate turn (fallback)
};

// ---------------------------------------------------------------------------
// Output percentage
// ---------------------------------------------------------------------------
constexpr float OUTPUT_PERCENT_MIN = 0.0f;
constexpr float OUTPUT_PERCENT_MAX = 100.0f;
constexpr float OUTPUT_PERCENT_STARTUP = OUTPUT_PERCENT_MAX; // top limit on power-on

// ---------------------------------------------------------------------------
// Display / UI
// ---------------------------------------------------------------------------
constexpr int DISPLAY_WIDTH = 240;
constexpr int DISPLAY_HEIGHT = 240;

// LVGL draw buffer: number of pixel rows buffered at a time (partial render).
constexpr int LVGL_DRAW_BUF_LINES = 40;
