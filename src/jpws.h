#pragma once

// stb_image by Sean Barrett (“nothings”).
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image/stb_image_resize2.h"
// https://github.com/nothings/stb

// This software is based in part on the work of the Independent JPEG Group.
#include <turbojpeg.h>
// https://github.com/libjpeg-turbo/libjpeg-turbo

#include <algorithm>
#include <array>
#include <set>
#include <filesystem>
#include <random>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "information.cpp"
#include "programArgs.cpp"
#include "fileChecks.cpp"
#include "profileVec.cpp"
#include "searchFunc.cpp"
#include "eraseSegments.cpp"
#include "resizeImage.cpp"
#include "writeFile.cpp"
#include "jpws.cpp"

template <typename T, size_t N>
uint32_t searchFunc(std::vector<uint8_t>&, uint32_t, const uint8_t, const std::array<T, N>&);

bool 
	hasValidFilename(const std::string&),
	writeFile(std::vector<uint8_t>&);

void
	validateFiles(const std::string&, const std::string&),
	eraseSegments(std::vector<uint8_t>&),
	resizeImage(std::vector<uint8_t>&, uint8_t, uint16_t, bool),
	displayInfo();

int jpws(const std::string&, const std::string&, ArgOption);
