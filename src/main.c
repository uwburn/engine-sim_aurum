#include <stdbool.h>

#include "aurum/core.h"
#include "aurum/tone.h"

au_printer_t serial_printer = au_serial_build_printer();

void setup();
void loop();

int main(void) {
  au_init();

  setup();

  while (1) {
    loop();
  }

  return 0;
}

// BOARD CONFIG
#define CFG_PIN 2
#define THROTTLE_PIN AU_A0

#define CYL_1_PIN 8
#define CYL_2_PIN 9
#define CYL_3_PIN 10
#define CYL_4_PIN 11

#define AUDIO_PIN 12

#define WELCOME_FREQ_STEP 500


// ENGINE PARAMETERS
#define MAX_CYLINDERS 4
#define MIN_RPM 500
#define RPM_PER_THROTTLE 12
#define MAX_RPM (MIN_RPM + 1023UL * RPM_PER_THROTTLE)

#define FIRE_DURATION 2
#define FIRE_FREQUENCY 250


// DEBUG
#define DEBUG 0
#define DEBUG_PERIOD 250000UL

typedef struct {
  uint8_t cylinders;
  uint16_t firing_angles[MAX_CYLINDERS];
  const char *name;
} engine_cfg_t;


// ENGINE MAPS
const engine_cfg_t off_cfg = {
  .cylinders = 0,
  .firing_angles = { },
  .name = "Off"
};

const engine_cfg_t mono_cfg = {
  .cylinders = 1,
  .firing_angles = { 0 },
  .name = "Mono"
};

const engine_cfg_t i2_360_cfg = {
  .cylinders = 2,
  .firing_angles = { 0, 36000 },
  .name = "I2 - 360°"
};

const engine_cfg_t i2_180_cfg = {
  .cylinders = 2,
  .firing_angles = { 0, 18000 },
  .name = "I2 - 180°"
};

const engine_cfg_t i2_270_cfg = {
  .cylinders = 2,
  .firing_angles = { 0, 27000 },
  .name = "I2 - 270°"
};

const engine_cfg_t i2_285_cfg = {
  .cylinders = 2,
  .firing_angles = { 0, 28500 },
  .name = "I2 - 285°"
};

const engine_cfg_t i3_120_cfg = {
  .cylinders = 3,
  .firing_angles = { 0, 24000, 48000 },
  .name = "I3 - 120°"
};

const engine_cfg_t i3_tplane_cfg = {
  .cylinders = 3,
  .firing_angles = { 0, 18000, 27000 },
  .name = "I3 - T-Plane"
};

const engine_cfg_t i4_screamer_cfg = {
  .cylinders = 4,
  .firing_angles = { 0, 54000, 18000, 36000 },
  .name = "I4 - Screamer"
};

const engine_cfg_t i4_crossplane_cfg = {
  .cylinders = 4,
  .firing_angles = { 0, 45000, 27000, 54000 },
  .name = "I4 - Crossplane"
};

const engine_cfg_t i4_big_bang_cfg = {
  .cylinders = 4,
  .firing_angles = { 0, 36000, 36000, 0 },
  .name = "I4 - Big-bang"
};

#define TOTAL_CFGS 11

const engine_cfg_t *const cfgs[TOTAL_CFGS] = {
  &off_cfg,
  &mono_cfg,
  &i2_360_cfg,
  &i2_180_cfg,
  &i2_270_cfg,
  &i2_285_cfg,
  &i3_120_cfg,
  &i3_tplane_cfg,
  &i4_screamer_cfg,
  &i4_crossplane_cfg,
  &i4_big_bang_cfg
};


// STATE
typedef struct {
  bool fired;
  uint32_t fire_end_time;
} cylinder_state_t;


uint8_t prev_cfg_signal = 0;
uint8_t cfg_index = 0;
const engine_cfg_t *cfg = &off_cfg;
uint32_t crank_phase = 0;
uint32_t prev_crank_phase = 0;
cylinder_state_t cylinders_state[MAX_CYLINDERS];
uint32_t last_update = 0;
uint32_t last_serial = 0;

void welcome();
void cylinders_off();
void reset_cycles();
uint32_t angle_to_phase(uint16_t angle);

void setup() {
  au_serial_begin(115200, AU_SERIAL_8N1);

  au_pin_mode(CFG_PIN, AU_INPUT_PULLUP);

  for (int i = 0; i < MAX_CYLINDERS; ++i) {
    au_pin_mode(CYL_1_PIN + i, AU_OUTPUT);
  }

  au_pin_mode(AUDIO_PIN, AU_OUTPUT);

  welcome();

  cylinders_off();
  reset_cycles();
}

void loop() {
  // Cfg switch
  uint8_t cfg_signal = !au_digital_read(CFG_PIN);
  if (cfg_signal && !prev_cfg_signal) {
    cfg_index = (cfg_index + 1) % TOTAL_CFGS;
    cfg = cfgs[cfg_index];

    crank_phase = 0;
    prev_crank_phase = 0;
    last_update = au_micros();
    cylinders_off();
    reset_cycles();
    au_no_tone(AUDIO_PIN);

    au_print_str(&serial_printer, "Switched engine config to ");
    au_println_str(&serial_printer, cfg->name);
  }
  prev_cfg_signal = cfg_signal;

  // Engine speed
  uint16_t throttle = au_analog_read(THROTTLE_PIN);
  uint32_t rpm = MIN_RPM + (uint32_t)throttle * RPM_PER_THROTTLE;
  if (rpm > MAX_RPM) {
    rpm = MAX_RPM;
  }

  // Update crank position
  uint32_t now = au_micros();
  uint32_t dt = now - last_update;
  last_update = now;

  // Phase increment
  // One 720° cycle takes: 120000000 / RPM microseconds
  // One cycle corresponds to: 2^32 phase units
  // increment = dt * RPM * 2^32 / 120000000
  uint32_t phase_inc = ((uint64_t)dt * rpm * 4294967296ULL) / 120000000ULL;
  prev_crank_phase = crank_phase;
  crank_phase += phase_inc;
  bool wrapped = crank_phase < prev_crank_phase;

  // Firings
  for (int i = 0; i < cfg->cylinders; ++i) {
    if (wrapped) {
      cylinders_state[i].fired = false;
    }

    if ((int32_t)(now - cylinders_state[i].fire_end_time) < 0) {
      au_digital_write(CYL_1_PIN + i, AU_LOW);
    }

    if (cylinders_state[i].fired) {
      continue;
    }

    uint32_t fire_phase = angle_to_phase(cfg->firing_angles[i]);

    if (crank_phase >= fire_phase) {
      au_digital_write(CYL_1_PIN + i, AU_HIGH);
      au_tone(AUDIO_PIN, FIRE_FREQUENCY, FIRE_DURATION);
      cylinders_state[i].fired = true;
      cylinders_state[i].fire_end_time = now + FIRE_DURATION * 1000UL;
    }
  }

  // Debug
  if (DEBUG && now - last_serial >= DEBUG_PERIOD) {
    last_serial = now;

    uint32_t angle = ((uint64_t)crank_phase * 720ULL) >> 32;

    au_print_str(&serial_printer, "RPM=");
    au_print_uint(&serial_printer, rpm);

    au_print_str(&serial_printer, " angle=");
    au_print_uint(&serial_printer, angle);

    au_print_str(&serial_printer, " phase=");
    au_println_uint(&serial_printer, crank_phase);
  }
}

void welcome() {
  au_println_str(&serial_printer, "Welcome to engine simulator!");
  au_println_str(&serial_printer, "");

  uint16_t f = WELCOME_FREQ_STEP;
  for (int i = 0; i < 4; ++i) {
    au_digital_write(8 + i, AU_HIGH);
    au_tone(12, f, 200);
    f += WELCOME_FREQ_STEP;
    au_delay(200);
  }

  for (int i = 0; i < 4; ++i) {
    au_digital_write(8 + i, AU_LOW);
  }

  au_delay(1000);
}

uint32_t angle_to_phase(uint16_t angle) {
  return ((uint64_t)angle << 32) / 72000ULL;
}

void cylinders_off() {
  for (int i = 0; i < cfg->cylinders; ++i) {
    au_digital_write(CYL_1_PIN + i, AU_LOW);
  }
}

void reset_cycles() {
  for (int i = 0; i < cfg->cylinders; ++i) {
    cylinders_state[i].fired = false;
    cylinders_state[i].fire_end_time = au_micros();
  }
}