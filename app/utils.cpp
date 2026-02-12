#include "utils.h"

#include <iomanip>
#include <ios>
#include <string>
#include <sstream>

using namespace std;

uint32_t crc32(const uint8_t* data, size_t size) {
    uint32_t crc = ~0xEADA2D49;  // 0xA2 seed

    while (size--) {
        crc ^= *data++;
        for (unsigned i = 0; i < 8; i++)
            crc = ((crc >> 1) ^ (0xEDB88320 & -(static_cast<int>(crc & 1))));
    }

    return ~crc;
}

void fill_output_report_checksum(uint8_t* outputData,size_t len)
{
    uint32_t crc = crc32(outputData, len - 4);
    outputData[len - 4] = (crc >> 0) & 0xFF;
    outputData[len - 3] = (crc >> 8) & 0xFF;
    outputData[len - 2] = (crc >> 16) & 0xFF;
    outputData[len - 1] = (crc >> 24) & 0xFF;
}

string hexStr(uint8_t* data, int len)
{
    stringstream ss;
    ss << hex;
    for (int i = 0; i < len; ++i)
        ss << setw(2) << setfill('0') << static_cast<int>(data[i]) << ' ';
    return ss.str();
}


wstring Win32ErrorToString(DWORD error)
{
    if (error == 0)
        return L"No error (0)";

    LPWSTR buffer = nullptr;

    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER
                | FORMAT_MESSAGE_FROM_SYSTEM
                | FORMAT_MESSAGE_IGNORE_INSERTS;

    DWORD len = ::FormatMessageW(
        flags,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr
    );

    if (len == 0)
    {
        DWORD fmErr = ::GetLastError();
        return L"FormatMessageW failed. error=" + to_wstring(fmErr) +
               L", original error=" + to_wstring(error);
    }

    wstring msg(buffer, len);
    ::LocalFree(buffer);

    // 去掉系统消息末尾常见的 \r\n
    while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n'))
        msg.pop_back();

    return msg;
}