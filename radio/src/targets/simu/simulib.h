/*
 * EdgeTX
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#if !defined(_SIMULIB_H_)
#define _SIMULIB_H_

#include <stdint.h>
#include <stdbool.h>
#include <string>

void simuInit();
void simuStart(bool tests = false);
void simuStop();
void simuStartupComplete();
bool simuStartupCompleted();
bool simuIsRunning();
bool simuIsShuttingDown();
void simuWaitStartupComplete();
void simuMain();

void simuSetSwitch(uint8_t swtch, int8_t state);
void simuSetKey(uint8_t key, bool state);
void simuSetTrim(uint8_t trim, bool state);

void simuFatfsSetPaths(const char* sdPath, const char* settingsPath);
std::string simuFatfsGetRealPath(const std::string& p);

void simuRotaryEncoderEvent(int32_t steps);

std::string simuGetAudioHistory(int maxLines = 200);

void simuTrace(const char* text);

void simuLogAudioEvent(const char* fmt, ...);

uint32_t simuLcdGetWidth();
uint32_t simuLcdGetHeight();
uint32_t simuLcdGetDepth();
uint32_t simuLcdCopy(uint8_t* buf, uint32_t maxLen);
void simuLcdNotify();

void simuTouchDown(int16_t x, int16_t y);
void simuTouchUp();

void simuQueueAudio(const uint8_t* data, uint32_t len);

void simuSetTelemetryStreaming(uint8_t streaming);

#endif
