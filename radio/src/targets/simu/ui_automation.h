#pragma once

#include <cstdint>
#include <string>

namespace SimuUiAutomation
{

void menuTick();

bool requestSnapshot(std::string& json, std::string& error,
                     uint32_t timeoutMs = 1000);

bool requestAction(const std::string& id, const std::string& action,
                   std::string& extra, std::string& error,
                   uint32_t timeoutMs = 1000);

}
