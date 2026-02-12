#ifndef UTILS_H
#define UTILS_H

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdint>
#include <string>

uint32_t crc32(const uint8_t* data, size_t size);
std::string hexStr(uint8_t* data, int len);
std::wstring Win32ErrorToString(DWORD error);
void fill_output_report_checksum(uint8_t* outputData,size_t len);

#endif