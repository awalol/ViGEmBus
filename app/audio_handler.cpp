#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "audio_handler.h"
#include <iostream>
#include <mutex>
#include <vector>
#include <fstream>
#include <iomanip>

#include "hid_handler.h"
#include "ViGEm/Common.h"

using namespace std;
// 音频录制状态
static mutex audioMutex;
static vector<UCHAR> audioData;

//
// DS5 Audio OUT format: 4-channel, 16-bit PCM, 48000 Hz
//
static constexpr WORD  WAV_CHANNELS    = 4;
static constexpr DWORD WAV_SAMPLE_RATE = 48000;
static constexpr WORD  WAV_BITS        = 16;
static constexpr WORD  WAV_BLOCK_ALIGN = WAV_CHANNELS * (WAV_BITS / 8); // 8 bytes
static constexpr DWORD WAV_BYTE_RATE   = WAV_SAMPLE_RATE * WAV_BLOCK_ALIGN;

//
// 写WAV文件（在程序退出时调用）
//
void audio_handler::write_wav_file(const char* filename)
{
    scoped_lock lock(audioMutex);

    if (audioData.empty())
    {
        cout << "[WAV] No audio data captured, skipping WAV output." << endl;
        return;
    }

    ofstream file(filename, ios::binary);
    if (!file.is_open())
    {
        cerr << "[WAV] Failed to open " << filename << " for writing." << endl;
        return;
    }

    const DWORD dataSize = static_cast<DWORD>(audioData.size());
    const DWORD fileSize = 36 + dataSize;

    // RIFF header
    file.write("RIFF", 4);
    file.write(reinterpret_cast<const char*>(&fileSize), 4);
    file.write("WAVE", 4);

    // fmt  sub-chunk
    file.write("fmt ", 4);
    const DWORD fmtSize = 16;
    file.write(reinterpret_cast<const char*>(&fmtSize), 4);
    const WORD  audioFormat = 1; // PCM
    file.write(reinterpret_cast<const char*>(&audioFormat), 2);
    const WORD  channels = WAV_CHANNELS;
    file.write(reinterpret_cast<const char*>(&channels), 2);
    const DWORD sampleRate = WAV_SAMPLE_RATE;
    file.write(reinterpret_cast<const char*>(&sampleRate), 4);
    const DWORD byteRate = WAV_BYTE_RATE;
    file.write(reinterpret_cast<const char*>(&byteRate), 4);
    const WORD  blockAlign = WAV_BLOCK_ALIGN;
    file.write(reinterpret_cast<const char*>(&blockAlign), 2);
    const WORD  bitsPerSample = WAV_BITS;
    file.write(reinterpret_cast<const char*>(&bitsPerSample), 2);

    // data sub-chunk
    file.write("data", 4);
    file.write(reinterpret_cast<const char*>(&dataSize), 4);
    file.write(reinterpret_cast<const char*>(audioData.data()), dataSize);

    file.close();

    const double durationSec = static_cast<double>(dataSize) / WAV_BYTE_RATE;
    cout << "[WAV] Saved " << filename 
              << " (" << dataSize << " bytes, "
              << fixed << setprecision(2) << durationSec << "s)"
              << endl;
}

void audio_handler::audio_thread(stop_token stoken)
{
    DS5_AUDIO_BUFFER audioBuf;
    ULONG audioPacketCount = 0;

    while (!stoken.stop_requested())
    {
        auto err = vigem_target_DS5_await_audio_data_timeout(hid_handler::vigem_client, hid_handler::vigem_ds, 500, &audioBuf);

        if (VIGEM_SUCCESS(err))
        {
            audioPacketCount++;

            // 将音频数据追加到全局缓冲区
            {
                scoped_lock lock(audioMutex);
                audioData.insert(audioData.end(),
                                   audioBuf.AudioData,
                                   audioBuf.AudioData + audioBuf.AudioDataLength);
            }

            if (audioPacketCount % 100 == 0)
            {
                double sec;
                {
                    scoped_lock lock(audioMutex);
                    sec = static_cast<double>(audioData.size()) / WAV_BYTE_RATE;
                }
                cout << "[Audio] " << audioPacketCount << " packets received, "
                          << fixed << setprecision(2) << sec << "s recorded"
                          << endl;
            }
        }
        else if (err != VIGEM_ERROR_TIMED_OUT)
        {
            cerr << "[Audio] Error receiving audio data" << endl;
            break;
        }
    }
    cout << "[App] Stopping... writing WAV file." << endl;
    write_wav_file("DS5_audio_out.wav");
}