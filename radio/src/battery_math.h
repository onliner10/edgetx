#pragma once

#include <cstdint>

namespace {

struct OCVPoint {
  uint16_t cv;   // per-cell centivolts (10mV)
  uint8_t pct;   // 0-100
};

// Conservative Li-Ion OCV curve (typical 18650).
// Under-reports slightly in mid-range to avoid false high-charge indication.
constexpr OCVPoint liionOcvTable[] = {
  { 330, 0 },
  { 350, 6 },
  { 360, 12 },
  { 370, 25 },
  { 380, 45 },
  { 390, 65 },
  { 400, 80 },
  { 410, 90 },
  { 420, 100 },
};

constexpr uint8_t liionOcvTableSize = 9;

}  // namespace

// Interpolate per-cell centivolts against the Li-Ion OCV table.
inline uint8_t batteryPctFromPerCellCv(uint16_t perCellCv)
{
  if (perCellCv <= liionOcvTable[0].cv)
    return liionOcvTable[0].pct;
  if (perCellCv >= liionOcvTable[liionOcvTableSize - 1].cv)
    return liionOcvTable[liionOcvTableSize - 1].pct;

  for (uint8_t i = 1; i < liionOcvTableSize; i++) {
    if (perCellCv < liionOcvTable[i].cv) {
      uint16_t range = liionOcvTable[i].cv - liionOcvTable[i - 1].cv;
      uint16_t offset = perCellCv - liionOcvTable[i - 1].cv;
      uint16_t pctRange = liionOcvTable[i].pct - liionOcvTable[i - 1].pct;
      return liionOcvTable[i - 1].pct +
             uint8_t((pctRange * offset) / range);
    }
  }

  return liionOcvTable[liionOcvTableSize - 1].pct;
}

// TX battery percentage from pack voltage in 100 mV units
// (g_vbat100mV). Uses 2-cell Li-Ion OCV curve.
// Returns 0..100.
inline uint8_t txBatteryPercent(uint8_t decivolts)
{
  if (decivolts == 0) return 0;
  uint16_t perCellCv = uint16_t(decivolts) * 10 / 2;
  return batteryPctFromPerCellCv(perCellCv);
}
