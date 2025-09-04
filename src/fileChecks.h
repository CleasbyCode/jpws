#pragma once

#include "programArgs.h"
#include <vector>
#include <cstdint>

bool hasValidImageExtension(const std::string&);
bool hasValidScriptExtension(const std::string&);
bool hasValidFilename(const std::string&);
void validateImageFile(std::string&, uintmax_t&, std::vector<uint8_t>&);
void validateScriptFile(std::string&, std::vector<uint8_t>&);
