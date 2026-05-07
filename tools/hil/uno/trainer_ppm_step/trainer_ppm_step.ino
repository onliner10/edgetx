/*
 * Minimal trainer-jack PPM step test for Arduino Uno R4 Minima.
 *
 * Wiring:
 *   D2  -> TX16S trainer tip through the existing divider/protection network
 *   GND -> TX16S trainer sleeve
 *
 * Behavior:
 *   - Emits a stable 8-channel PPM frame at ~22.5 ms.
 *   - All channels sit at 1500 us for 2 seconds.
 *   - CH1 steps to 1700 us for 2 seconds.
 *   - Repeats forever.
 *
 * Use the radio's Channel Monitor to confirm the trainer input path.
 */

#include <Arduino.h>

static const uint8_t PIN_PPM_OUT = 2;
static const uint8_t PPM_CHANNELS = 8;
static const uint16_t PPM_FRAME_US = 22500;
static const uint16_t PPM_MARK_US = 300;
static const uint16_t PPM_MIN_US = 800;
static const uint16_t PPM_MAX_US = 2200;
static const bool PPM_ACTIVE_HIGH = false;

static volatile uint16_t ppmOut[PPM_CHANNELS] = {
  1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500
};
static bool ppmMark = false;
static uint8_t ppmSlot = 0;
static uint32_t ppmDueUs = 0;
static uint32_t programStartMs = 0;

static void setChannels(uint16_t ch1)
{
  noInterrupts();
  ppmOut[0] = ch1;
  for (uint8_t i = 1; i < PPM_CHANNELS; i++) {
    ppmOut[i] = 1500;
  }
  interrupts();
}

static uint16_t syncIntervalUs()
{
  uint32_t sum = 0;
  for (uint8_t i = 0; i < PPM_CHANNELS; i++) {
    sum += ppmOut[i];
  }
  if (sum + 1000 > PPM_FRAME_US) {
    return 1000;
  }
  return (uint16_t)(PPM_FRAME_US - sum);
}

static void servicePpmOutput()
{
  uint32_t now = micros();
  if ((int32_t)(now - ppmDueUs) < 0) {
    return;
  }

  if (!ppmMark) {
    digitalWrite(PIN_PPM_OUT, PPM_ACTIVE_HIGH ? HIGH : LOW);
    ppmMark = true;
    ppmDueUs = now + PPM_MARK_US;
    return;
  }

  digitalWrite(PIN_PPM_OUT, PPM_ACTIVE_HIGH ? LOW : HIGH);
  ppmMark = false;

  bool channelSlot = ppmSlot < PPM_CHANNELS;
  uint16_t interval = channelSlot ? ppmOut[ppmSlot] : syncIntervalUs();
  ppmSlot++;
  if (ppmSlot > PPM_CHANNELS) {
    ppmSlot = 0;
  }
  if (channelSlot) {
    if (interval < PPM_MIN_US) {
      interval = PPM_MIN_US;
    }
    if (interval > PPM_MAX_US) {
      interval = PPM_MAX_US;
    }
  }
  if (interval < PPM_MARK_US + 500) {
    interval = PPM_MARK_US + 500;
  }
  ppmDueUs = now + interval - PPM_MARK_US;
}

void setup()
{
  pinMode(PIN_PPM_OUT, OUTPUT);
  digitalWrite(PIN_PPM_OUT, PPM_ACTIVE_HIGH ? LOW : HIGH);
  programStartMs = millis();
  ppmDueUs = micros() + 1000;
  setChannels(1500);
}

void loop()
{
  uint32_t elapsedMs = millis() - programStartMs;
  if (elapsedMs < 2000) {
    setChannels(1500);
  } else if (elapsedMs < 4000) {
    setChannels(1700);
  } else {
    programStartMs = millis();
  }

  servicePpmOutput();
}
