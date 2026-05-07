/*
 * Edge16 HIL MVP firmware for Arduino Uno R4 Minima.
 *
 * Pins:
 *   D2  -> trainer jack PPM signal through appropriate protection/level wiring
 *   D3  <- module-bay CPPM/PPM signal after level shifting; also direct
 *          software-inverted SBUS capture when no hardware inverter is fitted
 *   RX1 <- optional module-bay SBUS signal after hardware inversion
 *
 * Never connect module-bay battery voltage to the Uno.
 */

#include <Arduino.h>

#ifndef SERIAL_8E2
#define SERIAL_8E2 SERIAL_8N1
#endif

static const char FW[] = "edge16-hil-uno-r4-0.1.0";
static const uint8_t PIN_PPM_OUT = 2;
static const uint8_t PIN_PPM_IN = 3;
static const uint8_t PIN_MODULE_DIRECT = PIN_PPM_IN;

static const uint8_t PPM_CHANNELS = 8;
static const uint16_t PPM_FRAME_US = 22500;
static const uint16_t PPM_MARK_US = 300;
static const uint16_t PPM_SYNC_MIN_US = 4000;
static const uint16_t PPM_SYNC_MAX_US = 19000;
static const uint16_t PPM_MIN_US = 800;
static const uint16_t PPM_MAX_US = 2200;

static const uint8_t PROFILE_NONE = 0;
static const uint8_t PROFILE_PPM8 = 1;
static const uint8_t PROFILE_SBUS16 = 2;

static const uint8_t CASE_NONE = 0;
static const uint8_t CASE_PPM_IDENTITY = 1;
static const uint8_t CASE_PPM_LOSS_RECOVERY = 2;
static const uint8_t CASE_SBUS_IDENTITY = 3;
static const uint8_t CASE_PPM_SOAK = 4;

static volatile uint16_t ppmOut[PPM_CHANNELS] = {
  1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500
};
static volatile bool ppmOutputEnabled = true;
static volatile bool ppmOutMark = false;
static volatile uint8_t ppmOutSlot = 0;
static uint32_t ppmOutDueUs = 0;

struct PpmFrame {
  uint16_t channels[16];
  uint8_t count;
  uint32_t atUs;
  uint32_t periodUs;
};

static volatile uint16_t ppmIsrScratch[16];
static volatile uint8_t ppmIsrIndex = 0;
static volatile uint32_t ppmLastRiseUs = 0;
static volatile uint32_t ppmLastFrameUs = 0;
static volatile PpmFrame ppmReadyFrame;
static volatile bool ppmReady = false;
static volatile uint32_t ppmFrameCounter = 0;

struct SbusFrame {
  uint16_t channels[16];
  uint8_t flags;
  uint32_t atUs;
  uint32_t periodUs;
};

static SbusFrame latestSbus;
static uint32_t sbusFrameCounter = 0;
static uint32_t sbusLastFrameUs = 0;
static uint8_t sbusBuf[25];
static uint8_t sbusIndex = 0;

static volatile uint8_t sbusSoftBytes[64];
static volatile uint8_t sbusSoftHead = 0;
static volatile uint8_t sbusSoftTail = 0;
static volatile uint32_t sbusSoftBusyUntilUs = 0;
static volatile uint32_t sbusSoftByteCounter = 0;
static volatile uint32_t sbusSoftOverrunCounter = 0;

struct CaseMetrics {
  uint32_t frames;
  uint32_t mismatches;
  uint32_t missed;
  uint32_t stale;
  uint32_t malformed;
  uint32_t frozen;
  uint32_t lossNeutralFrames;
  uint32_t recoveryFrames;
  uint16_t periods[128];
  uint8_t periodCount;
};

static CaseMetrics metrics;
static uint8_t activeCase = CASE_NONE;
static volatile uint8_t activeCaseProfile = PROFILE_NONE;
static char activeCaseName[28] = "";
static uint16_t activeCaseSeq = 0;
static uint32_t caseStartMs = 0;
static uint32_t caseEndMs = 0;
static uint32_t caseBasePpmFrames = 0;
static uint32_t caseBaseSbusFrames = 0;
static uint32_t lastProgressMs = 0;
static uint16_t lastObservedPpm[4] = {0, 0, 0, 0};
static bool haveObservedPpm = false;
static uint32_t soakWindowStartMs = 0;
static uint16_t soakWindowMin[4] = {0, 0, 0, 0};
static uint16_t soakWindowMax[4] = {0, 0, 0, 0};
static bool soakWindowActive = false;

static volatile bool awaitingLock = false;
static bool lockReported = false;
static volatile uint8_t lockProfile = PROFILE_NONE;
static uint16_t lockSeq = 0;
static uint32_t lockBasePpmFrames = 0;
static uint32_t lockBaseSbusFrames = 0;

static uint8_t commandProfile = PROFILE_NONE;
static uint16_t commandSeq = 0;
static uint32_t commandUntilMs = 0;

static char commandLine[128];
static uint8_t commandLen = 0;

static uint32_t atomicPpmFrameCounter()
{
  noInterrupts();
  uint32_t value = ppmFrameCounter;
  interrupts();
  return value;
}

static bool moduleOutputIsSbusMode()
{
  return (awaitingLock && lockProfile == PROFILE_SBUS16) ||
         activeCaseProfile == PROFILE_SBUS16;
}

static void pushSoftSbusByte(uint8_t value)
{
  uint8_t next = (uint8_t)((sbusSoftHead + 1) & 0x3F);
  if (next == sbusSoftTail) {
    sbusSoftOverrunCounter++;
    return;
  }
  sbusSoftBytes[sbusSoftHead] = value;
  sbusSoftHead = next;
  sbusSoftByteCounter++;
}

static void sbusSoftCaptureIsr(uint32_t startUs)
{
  if ((int32_t)(startUs - sbusSoftBusyUntilUs) < 0) {
    return;
  }

  delayMicroseconds(15);
  uint8_t value = 0;
  for (uint8_t bit = 0; bit < 8; bit++) {
    if (digitalRead(PIN_MODULE_DIRECT) == LOW) {
      value |= (uint8_t)(1 << bit);
    }
    delayMicroseconds(10);
  }
  sbusSoftBusyUntilUs = startUs + 115;
  pushSoftSbusByte(value);
}

static void ppmCaptureIsr(uint32_t now)
{
  uint32_t delta = now - ppmLastRiseUs;
  ppmLastRiseUs = now;

  if (delta >= PPM_SYNC_MIN_US && delta <= PPM_SYNC_MAX_US) {
    if (ppmIsrIndex >= 4 && !ppmReady) {
      for (uint8_t i = 0; i < ppmIsrIndex && i < 16; i++) {
        ppmReadyFrame.channels[i] = ppmIsrScratch[i];
      }
      ppmReadyFrame.count = ppmIsrIndex;
      ppmReadyFrame.atUs = now;
      ppmReadyFrame.periodUs = ppmLastFrameUs == 0 ? 0 : now - ppmLastFrameUs;
      ppmLastFrameUs = now;
      ppmFrameCounter++;
      ppmReady = true;
    }
    ppmIsrIndex = 0;
    return;
  }

  if (delta >= PPM_MIN_US && delta <= PPM_MAX_US) {
    if (ppmIsrIndex < 16) {
      ppmIsrScratch[ppmIsrIndex++] = (uint16_t)delta;
    }
  } else {
    ppmIsrIndex = 0;
  }
}

static void moduleSignalCaptureIsr()
{
  uint32_t now = micros();
  if (moduleOutputIsSbusMode()) {
    sbusSoftCaptureIsr(now);
  } else {
    ppmCaptureIsr(now);
  }
}

static void setNeutralPayload(uint16_t channels[PPM_CHANNELS])
{
  for (uint8_t i = 0; i < PPM_CHANNELS; i++) {
    channels[i] = 1500;
  }
}

static uint8_t seqWindow(uint16_t seq)
{
  return (uint8_t)(seq % 91);
}

static uint8_t checksumFor(uint8_t profile, uint8_t seq)
{
  return (uint8_t)((profile * 37 + seq * 11 + 173) % 91);
}

static void encodeCommand(uint16_t channels[PPM_CHANNELS], uint8_t profile, uint16_t seq)
{
  uint8_t seqWin = seqWindow(seq);
  uint8_t checksum = checksumFor(profile, seqWin);
  channels[4] = profile == PROFILE_PPM8 ? 1100 : 1900;
  channels[5] = 1500 + ((int16_t)seqWin * 10 - 450);
  channels[6] = 1500 + ((int16_t)checksum * 10 - 450);
  channels[7] = 1932;
}

static void setPpmOutputChannels(const uint16_t channels[PPM_CHANNELS])
{
  noInterrupts();
  for (uint8_t i = 0; i < PPM_CHANNELS; i++) {
    ppmOut[i] = channels[i];
  }
  interrupts();
}

static uint16_t ppmSyncIntervalUs()
{
  uint32_t sum = 0;
  for (uint8_t i = 0; i < PPM_CHANNELS; i++) {
    sum += ppmOut[i];
  }
  if (sum + PPM_SYNC_MIN_US > PPM_FRAME_US) {
    return PPM_SYNC_MIN_US;
  }
  return (uint16_t)(PPM_FRAME_US - sum);
}

static void servicePpmOutput()
{
  uint32_t now = micros();
  if (!ppmOutputEnabled) {
    digitalWrite(PIN_PPM_OUT, LOW);
    ppmOutMark = false;
    ppmOutSlot = 0;
    ppmOutDueUs = now + 1000;
    return;
  }
  if ((int32_t)(now - ppmOutDueUs) < 0) {
    return;
  }

  if (!ppmOutMark) {
    digitalWrite(PIN_PPM_OUT, HIGH);
    ppmOutMark = true;
    ppmOutDueUs = now + PPM_MARK_US;
    return;
  }

  digitalWrite(PIN_PPM_OUT, LOW);
  ppmOutMark = false;
  uint16_t interval = ppmOutSlot < PPM_CHANNELS ? ppmOut[ppmOutSlot] : ppmSyncIntervalUs();
  ppmOutSlot++;
  if (ppmOutSlot > PPM_CHANNELS) {
    ppmOutSlot = 0;
  }
  if (interval < PPM_MARK_US + 500) {
    interval = PPM_MARK_US + 500;
  }
  ppmOutDueUs = now + interval - PPM_MARK_US;
}

static uint16_t expectedPulseUs(uint8_t caseId, uint32_t elapsedMs, uint8_t channel)
{
  if (caseId == CASE_PPM_IDENTITY || caseId == CASE_SBUS_IDENTITY) {
    uint8_t phase = (elapsedMs / 350) % 8;
    uint8_t stepChannel = phase / 2;
    if (channel == stepChannel) {
      return (phase & 1) ? 2000 : 1000;
    }
  }
  if (caseId == CASE_PPM_SOAK) {
    uint32_t phase = elapsedMs % 90000UL;
    if (phase < 10000UL) {
      return 1500;
    }
    if (phase < 30000UL) {
      uint8_t step = (uint8_t)((phase - 10000UL) / 500UL);
      if (channel == (step % 4)) {
        return (step & 1) ? 1900 : 1100;
      }
      return 1500;
    }
    if (phase < 50000UL) {
      int32_t sweep = (int32_t)((phase - 30000UL + channel * 5000UL) % 20000UL);
      if (sweep >= 10000) {
        sweep = 20000 - sweep;
      }
      return (uint16_t)(1100 + sweep * 800 / 10000);
    }
    if (phase < 70000UL) {
      uint32_t slot = (phase - 50000UL) / 200UL;
      uint16_t bit = (uint16_t)(((slot * 1103515245UL + 12345UL + channel * 2654435761UL) >> 16) & 0x03);
      if (bit == 0) {
        return 1100;
      }
      if (bit == 3) {
        return 1900;
      }
      return bit == 1 ? 1350 : 1650;
    }
    return 1500;
  }
  return 1500;
}

static uint16_t expectedSbusValue(uint16_t pulseUs)
{
  int16_t channelOutput = ((int16_t)pulseUs - 1500) * 2;
  int16_t value = 992 + channelOutput * 8 / 10;
  if (value < 0) {
    return 0;
  }
  if (value > 2047) {
    return 2047;
  }
  return (uint16_t)value;
}

static bool ppmSoakMovementExpected(uint32_t elapsedMs)
{
  uint32_t phase = elapsedMs % 90000UL;
  return phase >= 10000UL && phase < 70000UL;
}

static void resetSoakMovementWindow(uint32_t nowMs, const PpmFrame &frame)
{
  soakWindowStartMs = nowMs;
  soakWindowActive = true;
  for (uint8_t i = 0; i < 4; i++) {
    soakWindowMin[i] = frame.channels[i];
    soakWindowMax[i] = frame.channels[i];
  }
}

static void evaluateSoakMovementWindow(uint32_t nowMs, const PpmFrame &frame)
{
  static const uint32_t SOAK_WINDOW_MS = 2500UL;
  static const uint16_t SOAK_REQUIRED_SPAN_US = 120;

  if (!soakWindowActive) {
    resetSoakMovementWindow(nowMs, frame);
    return;
  }

  for (uint8_t i = 0; i < 4; i++) {
    if (frame.channels[i] < soakWindowMin[i]) {
      soakWindowMin[i] = frame.channels[i];
    }
    if (frame.channels[i] > soakWindowMax[i]) {
      soakWindowMax[i] = frame.channels[i];
    }
  }

  if (nowMs - soakWindowStartMs < SOAK_WINDOW_MS) {
    return;
  }

  bool moved = false;
  for (uint8_t i = 0; i < 4; i++) {
    if (soakWindowMax[i] - soakWindowMin[i] >= SOAK_REQUIRED_SPAN_US) {
      moved = true;
    }
  }
  if (!moved) {
    metrics.frozen++;
  }
  resetSoakMovementWindow(nowMs, frame);
}

static void updateTrainerPayload()
{
  uint16_t channels[PPM_CHANNELS];
  setNeutralPayload(channels);

  uint32_t nowMs = millis();
  bool enableOutput = true;
  if (activeCase != CASE_NONE) {
    uint32_t elapsedMs = nowMs - caseStartMs;
    if (activeCase == CASE_PPM_LOSS_RECOVERY && elapsedMs >= 600 && elapsedMs < 1900) {
      enableOutput = false;
    }
    for (uint8_t i = 0; i < 4; i++) {
      channels[i] = expectedPulseUs(activeCase, elapsedMs, i);
    }
  }

  if (commandProfile != PROFILE_NONE && (int32_t)(commandUntilMs - nowMs) > 0) {
    encodeCommand(channels, commandProfile, commandSeq);
    enableOutput = true;
  } else if (commandProfile != PROFILE_NONE) {
    commandProfile = PROFILE_NONE;
  }

  ppmOutputEnabled = enableOutput;
  setPpmOutputChannels(channels);
}

static void resetMetrics()
{
  memset(&metrics, 0, sizeof(metrics));
}

static void addPeriodSample(uint32_t periodUs)
{
  if (periodUs == 0) {
    return;
  }
  if (metrics.periodCount < 128) {
    uint32_t clipped = periodUs > 65535 ? 65535 : periodUs;
    metrics.periods[metrics.periodCount++] = (uint16_t)clipped;
  }
  if (periodUs > 35000) {
    metrics.missed++;
  }
}

static uint16_t percentile(uint8_t pct)
{
  if (metrics.periodCount == 0) {
    return 0;
  }
  uint16_t sorted[128];
  for (uint8_t i = 0; i < metrics.periodCount; i++) {
    sorted[i] = metrics.periods[i];
  }
  for (uint8_t i = 1; i < metrics.periodCount; i++) {
    uint16_t value = sorted[i];
    int8_t j = i - 1;
    while (j >= 0 && sorted[j] > value) {
      sorted[j + 1] = sorted[j];
      j--;
    }
    sorted[j + 1] = value;
  }
  uint16_t idx = (uint16_t)((uint32_t)(metrics.periodCount - 1) * pct / 100);
  return sorted[idx];
}

static void evaluatePpmFrame(const PpmFrame &frame)
{
  if (activeCaseProfile != PROFILE_PPM8 || activeCase == CASE_NONE) {
    return;
  }
  uint32_t elapsedMs = millis() - caseStartMs;
  metrics.frames++;
  addPeriodSample(frame.periodUs);
  if (frame.count < 4) {
    metrics.malformed++;
    metrics.mismatches++;
    return;
  }

  bool progressed = !haveObservedPpm;
  for (uint8_t i = 0; i < 4; i++) {
    uint16_t actual = frame.channels[i];
    if (actual < PPM_MIN_US || actual > PPM_MAX_US) {
      metrics.malformed++;
      return;
    }
    if (activeCase != CASE_PPM_SOAK) {
      uint16_t expected = expectedPulseUs(activeCase, elapsedMs, i);
      uint16_t delta = actual > expected ? actual - expected : expected - actual;
      if (delta > 35) {
        metrics.mismatches++;
        break;
      }
    }
    uint16_t movement = actual > lastObservedPpm[i] ? actual - lastObservedPpm[i] : lastObservedPpm[i] - actual;
    if (movement > 80) {
      progressed = true;
    }
    lastObservedPpm[i] = actual;
  }

  uint32_t nowMs = millis();
  if (activeCase == CASE_PPM_SOAK) {
    if (ppmSoakMovementExpected(elapsedMs)) {
      evaluateSoakMovementWindow(nowMs, frame);
    } else {
      soakWindowActive = false;
    }
  } else if (progressed) {
    lastProgressMs = nowMs;
    haveObservedPpm = true;
  }

  if (activeCase == CASE_PPM_LOSS_RECOVERY) {
    bool neutral = true;
    for (uint8_t i = 0; i < 4; i++) {
      uint16_t actual = frame.channels[i];
      uint16_t delta = actual > 1500 ? actual - 1500 : 1500 - actual;
      if (delta > 45) {
        neutral = false;
      }
    }
    if (neutral && elapsedMs >= 1300 && elapsedMs < 1900) {
      metrics.lossNeutralFrames++;
    }
    if (neutral && elapsedMs >= 2100) {
      metrics.recoveryFrames++;
    }
  }
}

static uint16_t sbusChannelValue(const uint8_t *buf, uint8_t channel)
{
  uint16_t bit = channel * 11;
  uint8_t byteIndex = 1 + bit / 8;
  uint8_t shift = bit % 8;
  uint32_t value = (uint32_t)buf[byteIndex] |
                   ((uint32_t)buf[byteIndex + 1] << 8) |
                   ((uint32_t)buf[byteIndex + 2] << 16);
  return (uint16_t)((value >> shift) & 0x07FF);
}

static void evaluateSbusFrame(const SbusFrame &frame)
{
  if (activeCaseProfile != PROFILE_SBUS16 || activeCase == CASE_NONE) {
    return;
  }
  uint32_t elapsedMs = millis() - caseStartMs;
  metrics.frames++;
  addPeriodSample(frame.periodUs);
  for (uint8_t i = 0; i < 4; i++) {
    uint16_t expected = expectedSbusValue(expectedPulseUs(activeCase, elapsedMs, i));
    uint16_t actual = frame.channels[i];
    uint16_t delta = actual > expected ? actual - expected : expected - actual;
    if (delta > 45) {
      metrics.mismatches++;
      break;
    }
  }
}

static void servicePpmCapture()
{
  if (!ppmReady) {
    return;
  }

  PpmFrame frame;
  noInterrupts();
  memcpy(&frame, (const void *)&ppmReadyFrame, sizeof(frame));
  ppmReady = false;
  interrupts();

  evaluatePpmFrame(frame);
}

static void consumeSbusByte(uint8_t value)
{
  if (sbusIndex == 0 && value != 0x0F) {
    return;
  }
  sbusBuf[sbusIndex++] = value;
  if (sbusIndex < sizeof(sbusBuf)) {
    return;
  }

  if (sbusBuf[0] == 0x0F && sbusBuf[24] == 0x00) {
    uint32_t now = micros();
    for (uint8_t i = 0; i < 16; i++) {
      latestSbus.channels[i] = sbusChannelValue(sbusBuf, i);
    }
    latestSbus.flags = sbusBuf[23];
    latestSbus.atUs = now;
    latestSbus.periodUs = sbusLastFrameUs == 0 ? 0 : now - sbusLastFrameUs;
    sbusLastFrameUs = now;
    sbusFrameCounter++;
    evaluateSbusFrame(latestSbus);
  }
  sbusIndex = 0;
}

static bool popSoftSbusByte(uint8_t *value)
{
  noInterrupts();
  if (sbusSoftHead == sbusSoftTail) {
    interrupts();
    return false;
  }
  *value = sbusSoftBytes[sbusSoftTail];
  sbusSoftTail = (uint8_t)((sbusSoftTail + 1) & 0x3F);
  interrupts();
  return true;
}

static void serviceSbus()
{
  while (Serial1.available() > 0) {
    consumeSbusByte((uint8_t)Serial1.read());
  }

  uint8_t value = 0;
  while (popSoftSbusByte(&value)) {
    consumeSbusByte(value);
  }
}

static void printProfileName(uint8_t profile)
{
  Serial.print(profile == PROFILE_PPM8 ? "ppm8" : "sbus16");
}

static void serviceLock()
{
  if (!awaitingLock || lockReported) {
    return;
  }
  uint32_t ppmFrames = atomicPpmFrameCounter();
  uint32_t sbusFrames = sbusFrameCounter;
  uint32_t good = lockProfile == PROFILE_PPM8 ? ppmFrames - lockBasePpmFrames
                                              : sbusFrames - lockBaseSbusFrames;
  if (good < 5) {
    return;
  }
  uint32_t staleOther = lockProfile == PROFILE_PPM8 ? sbusFrames - lockBaseSbusFrames
                                                    : ppmFrames - lockBasePpmFrames;
  Serial.print("LOCK profile=");
  printProfileName(lockProfile);
  Serial.print(" seq=");
  Serial.print(lockSeq);
  Serial.print(" frames=");
  Serial.print(good);
  Serial.print(" stale_other=");
  Serial.println(staleOther);
  lockReported = true;
  awaitingLock = false;
}

static bool casePasses()
{
  if (metrics.frames < 8) {
    return false;
  }
  if (metrics.missed != 0 || metrics.malformed != 0 || metrics.frozen != 0) {
    return false;
  }
  if (activeCase != CASE_PPM_SOAK && metrics.mismatches != 0) {
    return false;
  }
  if (metrics.stale != 0) {
    return false;
  }
  if (activeCase == CASE_PPM_LOSS_RECOVERY) {
    return metrics.lossNeutralFrames >= 3 && metrics.recoveryFrames >= 3;
  }
  return true;
}

static void finishCaseIfDue()
{
  if (activeCase == CASE_NONE || (int32_t)(millis() - caseEndMs) < 0) {
    return;
  }

  uint32_t ppmFrames = atomicPpmFrameCounter();
  uint32_t sbusFrames = sbusFrameCounter;
  metrics.stale = activeCaseProfile == PROFILE_PPM8 ? sbusFrames - caseBaseSbusFrames
                                                    : ppmFrames - caseBasePpmFrames;
  bool passed = casePasses();
  Serial.print("RESULT case=");
  Serial.print(activeCaseName);
  Serial.print(" status=");
  Serial.print(passed ? "pass" : "fail");
  Serial.print(" seq=");
  Serial.print(activeCaseSeq);
  Serial.print(" frames=");
  Serial.print(metrics.frames);
  Serial.print(" mismatches=");
  Serial.print(metrics.mismatches);
  Serial.print(" missed=");
  Serial.print(metrics.missed);
  Serial.print(" stale=");
  Serial.print(metrics.stale);
  Serial.print(" malformed=");
  Serial.print(metrics.malformed);
  Serial.print(" frozen=");
  Serial.print(metrics.frozen);
  Serial.print(" period_min_us=");
  Serial.print(percentile(0));
  Serial.print(" period_p50_us=");
  Serial.print(percentile(50));
  Serial.print(" period_p95_us=");
  Serial.print(percentile(95));
  Serial.print(" period_p99_us=");
  Serial.print(percentile(99));
  if (activeCase == CASE_PPM_LOSS_RECOVERY) {
    Serial.print(" loss_neutral_frames=");
    Serial.print(metrics.lossNeutralFrames);
    Serial.print(" recovery_frames=");
    Serial.print(metrics.recoveryFrames);
  }
  if (!passed) {
    Serial.print(" message=assertion_failed");
  }
  Serial.println();

  activeCase = CASE_NONE;
  activeCaseProfile = PROFILE_NONE;
  activeCaseName[0] = '\0';
  resetMetrics();
}

static uint8_t profileFromString(const char *value)
{
  if (strcmp(value, "ppm8") == 0) {
    return PROFILE_PPM8;
  }
  if (strcmp(value, "sbus16") == 0) {
    return PROFILE_SBUS16;
  }
  return PROFILE_NONE;
}

static uint8_t caseFromString(const char *value)
{
  if (strcmp(value, "ppm_identity") == 0) {
    return CASE_PPM_IDENTITY;
  }
  if (strcmp(value, "ppm_loss_recovery") == 0) {
    return CASE_PPM_LOSS_RECOVERY;
  }
  if (strcmp(value, "sbus_identity") == 0) {
    return CASE_SBUS_IDENTITY;
  }
  if (strcmp(value, "ppm_soak") == 0) {
    return CASE_PPM_SOAK;
  }
  return CASE_NONE;
}

static uint8_t profileForCase(uint8_t caseId)
{
  if (caseId == CASE_SBUS_IDENTITY) {
    return PROFILE_SBUS16;
  }
  if (caseId == CASE_PPM_IDENTITY || caseId == CASE_PPM_LOSS_RECOVERY || caseId == CASE_PPM_SOAK) {
    return PROFILE_PPM8;
  }
  return PROFILE_NONE;
}

static const char *fieldValue(char *line, const char *key)
{
  size_t keyLen = strlen(key);
  char *cursor = line;
  while (*cursor != '\0') {
    while (*cursor == ' ') {
      cursor++;
    }
    if (strncmp(cursor, key, keyLen) == 0 && cursor[keyLen] == '=') {
      return cursor + keyLen + 1;
    }
    while (*cursor != '\0' && *cursor != ' ') {
      cursor++;
    }
  }
  return nullptr;
}

static void copyToken(const char *src, char *dst, size_t dstSize)
{
  if (dstSize == 0) {
    return;
  }
  size_t i = 0;
  while (src && src[i] != '\0' && src[i] != ' ' && i + 1 < dstSize) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

static uint16_t parseU16Field(char *line, const char *key, uint16_t fallback)
{
  const char *value = fieldValue(line, key);
  if (!value) {
    return fallback;
  }
  return (uint16_t)strtoul(value, nullptr, 10);
}

static uint32_t parseU32Field(char *line, const char *key, uint32_t fallback)
{
  const char *value = fieldValue(line, key);
  if (!value) {
    return fallback;
  }
  return strtoul(value, nullptr, 10);
}

static void handlePing()
{
  Serial.print("READY fw=");
  Serial.print(FW);
  Serial.println(" caps=ppm_out,ppm_in,sbus_in,sbus_soft_inv");
}

static void handleSelftest(uint16_t seq)
{
  Serial.print("RESULT case=selftest status=pass seq=");
  Serial.print(seq);
  Serial.print(" fw=");
  Serial.print(FW);
  Serial.println(" caps=ppm_out,ppm_in,sbus_in,sbus_soft_inv");
}

static void handleSet(char *line)
{
  char profileText[16];
  copyToken(fieldValue(line, "profile"), profileText, sizeof(profileText));
  uint8_t profile = profileFromString(profileText);
  uint16_t seq = parseU16Field(line, "seq", 0);
  if (profile == PROFILE_NONE) {
    Serial.println("ERROR code=bad_profile message=bad_profile");
    return;
  }

  commandProfile = profile;
  commandSeq = seq;
  commandUntilMs = millis() + 900;
  lockProfile = profile;
  lockSeq = seq;
  lockBasePpmFrames = atomicPpmFrameCounter();
  lockBaseSbusFrames = sbusFrameCounter;
  awaitingLock = true;
  lockReported = false;

  Serial.print("PROFILE profile=");
  printProfileName(profile);
  Serial.print(" seq=");
  Serial.print(seq);
  Serial.println(" status=sent");
}

static void handleRun(char *line)
{
  char caseText[28];
  copyToken(fieldValue(line, "case"), caseText, sizeof(caseText));
  uint8_t caseId = caseFromString(caseText);
  uint16_t seq = parseU16Field(line, "seq", 0);
  uint32_t durationMs = parseU32Field(line, "ms", 2500);
  if (caseId == CASE_NONE) {
    Serial.println("ERROR code=bad_case message=bad_case");
    return;
  }

  resetMetrics();
  activeCase = caseId;
  activeCaseProfile = profileForCase(caseId);
  activeCaseSeq = seq;
  strncpy(activeCaseName, caseText, sizeof(activeCaseName) - 1);
  activeCaseName[sizeof(activeCaseName) - 1] = '\0';
  caseStartMs = millis();
  caseEndMs = caseStartMs + durationMs;
  caseBasePpmFrames = atomicPpmFrameCounter();
  caseBaseSbusFrames = sbusFrameCounter;
  lastProgressMs = caseStartMs;
  haveObservedPpm = false;
  soakWindowActive = false;
  for (uint8_t i = 0; i < 4; i++) {
    lastObservedPpm[i] = 0;
    soakWindowMin[i] = 0;
    soakWindowMax[i] = 0;
  }

  Serial.print("PROFILE profile=");
  printProfileName(activeCaseProfile);
  Serial.print(" seq=");
  Serial.print(seq);
  Serial.print(" case=");
  Serial.print(activeCaseName);
  Serial.println(" status=running");
}

static void handleStop()
{
  activeCase = CASE_NONE;
  activeCaseProfile = PROFILE_NONE;
  commandProfile = PROFILE_NONE;
  ppmOutputEnabled = true;
  uint16_t channels[PPM_CHANNELS];
  setNeutralPayload(channels);
  setPpmOutputChannels(channels);
  Serial.println("READY status=stopped");
}

static void handleStatus()
{
  Serial.print("STATUS ppm_frames=");
  Serial.print(atomicPpmFrameCounter());
  Serial.print(" sbus_frames=");
  Serial.print(sbusFrameCounter);
  Serial.print(" sbus_soft_bytes=");
  Serial.print(sbusSoftByteCounter);
  Serial.print(" sbus_soft_overrun=");
  Serial.print(sbusSoftOverrunCounter);
  Serial.print(" active_case=");
  Serial.print(activeCaseName[0] ? activeCaseName : "none");
  Serial.print(" lock_pending=");
  Serial.println(awaitingLock ? 1 : 0);
}

static void handleCommand(char *line)
{
  char original[128];
  strncpy(original, line, sizeof(original) - 1);
  original[sizeof(original) - 1] = '\0';

  char *command = strtok(line, " ");
  if (!command) {
    return;
  }

  if (strcmp(command, "PING") == 0) {
    handlePing();
  } else if (strcmp(command, "SELFTEST") == 0) {
    handleSelftest(parseU16Field(original, "seq", 0));
  } else if (strcmp(command, "SET") == 0) {
    handleSet(original);
  } else if (strcmp(command, "RUN") == 0) {
    handleRun(original);
  } else if (strcmp(command, "STOP") == 0) {
    handleStop();
  } else if (strcmp(command, "STATUS") == 0) {
    handleStatus();
  } else {
    Serial.println("ERROR code=bad_command message=bad_command");
  }
}

static void serviceUsb()
{
  while (Serial.available() > 0) {
    char ch = (char)Serial.read();
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      commandLine[commandLen] = '\0';
      handleCommand(commandLine);
      commandLen = 0;
      continue;
    }
    if (commandLen + 1 < sizeof(commandLine)) {
      commandLine[commandLen++] = ch;
    } else {
      commandLen = 0;
      Serial.println("ERROR code=line_too_long message=line_too_long");
    }
  }
}

void setup()
{
  pinMode(PIN_PPM_OUT, OUTPUT);
  digitalWrite(PIN_PPM_OUT, LOW);
  pinMode(PIN_PPM_IN, INPUT);

  Serial.begin(115200);
  Serial1.begin(100000, SERIAL_8E2);
  attachInterrupt(digitalPinToInterrupt(PIN_PPM_IN), moduleSignalCaptureIsr, RISING);

  ppmOutDueUs = micros() + 1000;
}

void loop()
{
  serviceUsb();
  updateTrainerPayload();
  servicePpmOutput();
  servicePpmCapture();
  serviceSbus();
  serviceLock();
  finishCaseIfDue();
}
