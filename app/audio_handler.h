#pragma once
#include <atomic>
#include <stop_token>

#include "ViGEm/Client.h"

class audio_handler
{
public:
    static void audio_thread(std::stop_token stop_token);
    static void write_wav_file(const char* filename);
private:
};
