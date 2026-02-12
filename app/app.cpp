// app.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#define WIN32_LEAN_AND_MEAN
#include <iomanip>
#include <Windows.h>
#include <ViGEm/Client.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <bitset>
#include <fstream>
#include <vector>
#include <atomic>
#include <mutex>
#include <csignal>
#include <hidapi/hidapi.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

using namespace std;

namespace
{
	hid_device *device = nullptr;
}

//
// DS5 Audio OUT format: 4-channel, 16-bit PCM, 48000 Hz
//
static constexpr WORD  WAV_CHANNELS    = 4;
static constexpr DWORD WAV_SAMPLE_RATE = 48000;
static constexpr WORD  WAV_BITS        = 16;
static constexpr WORD  WAV_BLOCK_ALIGN = WAV_CHANNELS * (WAV_BITS / 8); // 8 bytes
static constexpr DWORD WAV_BYTE_RATE   = WAV_SAMPLE_RATE * WAV_BLOCK_ALIGN;

// 音频录制状态
static mutex              g_audioMutex;
static mutex              g_hidMutex;
static vector<UCHAR>      g_audioData;
static atomic<bool>       g_running{ true };

//
// 写WAV文件（在程序退出时调用）
//
static void WriteWavFile(const char* filename)
{
	lock_guard<mutex> lock(g_audioMutex);

	if (g_audioData.empty())
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

	const DWORD dataSize = static_cast<DWORD>(g_audioData.size());
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
	file.write(reinterpret_cast<const char*>(g_audioData.data()), dataSize);

	file.close();

	const double durationSec = static_cast<double>(dataSize) / WAV_BYTE_RATE;
	cout << "[WAV] Saved " << filename 
	          << " (" << dataSize << " bytes, "
	          << fixed << setprecision(2) << durationSec << "s)"
	          << endl;
}

//
// Ctrl+C 信号处理：停止录制并写WAV
//
static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
{
	if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_CLOSE_EVENT || ctrlType == CTRL_BREAK_EVENT)
	{
		if (device)
		{
			cout << "Closing HIDAPI..." << endl;
			lock_guard<mutex> lock(g_hidMutex);
			hid_close(device);
			hid_exit();
		}
		cout << "\n[App] Stopping... writing WAV file." << endl;
		g_running = false;
		WriteWavFile("DS5_audio_out.wav");
		ExitProcess(0);
		return TRUE;
	}
	return FALSE;
}

static string hexStr(unsigned char* data, int len)
{
	stringstream ss;
	ss << hex;
	for (int i = 0; i < len; ++i)
		ss << setw(2) << setfill('0') << static_cast<int>(data[i]) << ' ';
	return ss.str();
}

static wstring Win32ErrorToString(DWORD error)
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

static uint32_t crc32(const uint8_t* data, size_t size) {
	uint32_t crc = ~0xEADA2D49;  // 0xA2 seed

	while (size--) {
		crc ^= *data++;
		for (unsigned i = 0; i < 8; i++)
			crc = ((crc >> 1) ^ (0xEDB88320 & -(static_cast<int>(crc & 1))));
	}

	return ~crc;
}

int main()
{
	// 注册 Ctrl+C 处理，退出时保存WAV
	SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
	
	if (hid_init() < 0)
	{
		wcout << "HIDAPI Init Fail" << L"\n";
		return -1;
	}
	device = hid_open(0x054C, 0x0CE6, L"143a9ae438ec");
	if (!device)
	{
		wcerr << "打开设备失败" << L"\n";
		hid_exit();
		return -1;
	}
	wcout << "连接设备成功" << L"\n";
	
	wchar_t manufacture_name[256];
	if (hid_get_manufacturer_string(device, manufacture_name, 256) >= 0)
	{
		wcout << L"ManufactureName: " << manufacture_name << L"\n";
	}
	else
	{
		wcerr << "ManufactureName 获取失败" << L"\n";
	}
	
	wchar_t product_name[256];
	if (hid_get_product_string(device, product_name, 256) >= 0)
	{
		wcout << L"ProductName: " << product_name << L"\n";
	}else
	{
		wcerr << "ProductName 获取失败" << L"\n";
	}
	
	wchar_t serial_number[256];
	if (hid_get_serial_number_string(device, serial_number, 256) >= 0)
	{
		wcout << L"SerialNumber: " << serial_number << L"\n";
	}else
	{
		wcerr << "SerialNumber 获取失败" << L"\n";
	}
	
	hid_set_nonblocking(device,1);
	
	const auto client = vigem_alloc();

	auto error = vigem_connect(client);

	const auto ds = vigem_target_DS5_alloc();

	// busenum.cpp -> Bus_PlugInDevice
	error = vigem_target_add(client, ds);
	if (!VIGEM_SUCCESS(error))
	{
		DWORD win32 = ::GetLastError();
		wcerr << L"vigem_target_add failed. GetLastError="
				   << win32 << L" (" << Win32ErrorToString(win32) << L")\n";
	}

	// 启动音频接收线程
	thread audioThread([&client, &ds]()
	{
		DS5_AUDIO_BUFFER audioBuf;
		ULONG audioPacketCount = 0;

		while (g_running)
		{
			auto err = vigem_target_DS5_await_audio_data_timeout(client, ds, 500, &audioBuf);

			if (VIGEM_SUCCESS(err))
			{
				audioPacketCount++;

				// 将音频数据追加到全局缓冲区
				{
					lock_guard<mutex> lock(g_audioMutex);
					g_audioData.insert(g_audioData.end(),
					                   audioBuf.AudioData,
					                   audioBuf.AudioData + audioBuf.AudioDataLength);
				}

				if (audioPacketCount % 100 == 0)
				{
					double sec;
					{
						lock_guard<mutex> lock(g_audioMutex);
						sec = static_cast<double>(g_audioData.size()) / WAV_BYTE_RATE;
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
	});
	audioThread.detach();
	thread proxyThread([&client, &ds](){
		unsigned char buf[78];
		while (g_running)
		{
			int read = 0;
			{
				lock_guard<mutex> lock(g_hidMutex);
				read = hid_read(device, buf, sizeof(buf));
			}
			if (read > 1)
			{
				switch (buf[0])
				{
				case 0x31:
					{
						// cout << "Receive Input Report: " << hexStr(buf,78) << endl;
						DS5_REPORT report;
						RtlZeroMemory(&report, sizeof(DS5_REPORT));
						RtlCopyMemory(&report,buf + 2,sizeof(DS5_REPORT));
			
						auto error = vigem_target_DS5_update(client, ds, report);
						if (!VIGEM_SUCCESS(error))
						{
							cerr << "[App] Failed to send DS5 report." << endl;
						}
						break;
					}
				case 0x01:
					{
						cout << "Receive 0x01 Input Report: " << hexStr(buf,63) << endl;
					}
				}
			}
			else if (read < 0)
			{
				lock_guard<mutex> lock(g_hidMutex);
				cerr << "HID READ ERROR: " << hid_read_error(device) << endl;
			}
		}
	});
	proxyThread.detach();

	cout << "[App] Recording audio to DS5_audio_out.wav. Press 'k' to send report, Ctrl+C to stop." << endl;

	DS5_OUTPUT_BUFFER out;
	int outputSeq = 0;
	uint8_t outputData[78];

	while (TRUE) 
	{
		// 检测按键 'k' 发送报文
		if (GetAsyncKeyState('K') & 0x8000)
		{
			DS5_REPORT report;
			RtlZeroMemory(&report, sizeof(DS5_REPORT));

			// --- Sticks (0x00-0xFF, 0x80 = center) ---
			report.bThumbLX = 0x7e;
			report.bThumbLY = 0x81;
			report.bThumbRX = 0x80;
			report.bThumbRY = 0x7f;

			// --- Triggers ---
			report.bTriggerL = 0x00;
			report.bTriggerR = 0x00;

			// --- Sequence number ---
			report.bSeqNo = 0x59;

			// --- Byte 7: DPad + face buttons ---
			report.DPad = 0x8;            // DS5_BUTTON_DPAD_NONE
			report.ButtonSquare = 0;
			report.ButtonCross = 0;
			report.ButtonCircle = 0;
			report.ButtonTriangle = 0;

			// --- Byte 8: shoulder/trigger/menu/thumbstick ---
			report.ButtonL1 = 0;
			report.ButtonR1 = 0;
			report.ButtonL2 = 0;
			report.ButtonR2 = 0;
			report.ButtonCreate = 0;
			report.ButtonOptions = 0;
			report.ButtonL3 = 0;
			report.ButtonR3 = 0;

			// --- Byte 9: special buttons ---
			report.ButtonHome = 0;
			report.ButtonPad = 0;
			report.ButtonMute = 0;
			report.UNK1 = 0;
			report.ButtonLeftFunction = 0;
			report.ButtonRightFunction = 0;
			report.ButtonLeftPaddle = 0;
			report.ButtonRightPaddle = 0;

			// --- Byte 10 ---
			report.bUNK2 = 0x00;

			// --- Counter ---
			report.ulUNKCounter = 0x787A7C40;

			// --- IMU: Gyroscope ---
			report.wAngularVelocityX = 0x006B;   // 107
			report.wAngularVelocityZ = 0x0007;   // 7
			report.wAngularVelocityY = 0x0004;   // 4

			// --- IMU: Accelerometer ---
			report.wAccelerometerX = 0x0139;      // 313
			report.wAccelerometerY = 0x25BC;      // 9660
			report.wAccelerometerZ = (SHORT)0xEB57;// -5289

			// --- Timestamps & Temperature ---
			report.ulSensorTimestamp = 0x016BFF84;
			report.bTemperature = 0x07;

			// --- Touch data (both fingers not touching) ---
			report.sCurrentTouch.Finger[0].Index = 0;
			report.sCurrentTouch.Finger[0].NotTouching = 1;
			report.sCurrentTouch.Finger[0].FingerX = 0;
			report.sCurrentTouch.Finger[0].FingerY = 0;
			report.sCurrentTouch.Finger[1].Index = 0;
			report.sCurrentTouch.Finger[1].NotTouching = 1;
			report.sCurrentTouch.Finger[1].FingerX = 0;
			report.sCurrentTouch.Finger[1].FingerY = 0;
			report.sCurrentTouch.bTimestamp = 0x00;

			// --- Trigger feedback ---
			report.TriggerRightStopLocation = 9;
			report.TriggerRightStatus = 0;
			report.TriggerLeftStopLocation = 9;
			report.TriggerLeftStatus = 0;

			// --- Host timestamp ---
			report.ulHostTimestamp = 0x00000000;

			// --- Active trigger effects ---
			report.TriggerRightEffect = 0;
			report.TriggerLeftEffect = 0;

			// --- Device timestamp ---
			report.ulDeviceTimeStamp = 0x016C1A97;

			// --- Power ---
			report.PowerPercent = 9;       // ~90%
			report.PowerState = 2;

			// --- Byte 53: plugged devices ---
			report.PluggedHeadphones = 0;
			report.PluggedMic = 0;
			report.MicMuted = 0;
			report.PluggedUsbData = 1;
			report.PluggedUsbPower = 0;
			report.UsbPowerOnBT = 0;
			report.DockDetect = 0;
			report.PluggedUnk = 0;

			// --- Byte 54 ---
			report.PluggedExternalMic = 0;
			report.HapticLowPassFilter = 0;
			report.PluggedUnk3 = 0;

			// --- AES-CMAC (8 bytes) ---
			report.bAesCmac[0] = 0x8A;
			report.bAesCmac[1] = 0x01;
			report.bAesCmac[2] = 0x58;
			report.bAesCmac[3] = 0x45;
			report.bAesCmac[4] = 0x74;
			report.bAesCmac[5] = 0x2E;
			report.bAesCmac[6] = 0x75;
			report.bAesCmac[7] = 0x3D;

			error = vigem_target_DS5_update(client, ds, report);
			if (VIGEM_SUCCESS(error))
				cout << "[App] DS5 report sent successfully." << endl;
			else
				cerr << "[App] Failed to send DS5 report." << endl;

			// 防止连续触发
			Sleep(200);
		}

		//error = vigem_target_DS5_await_output_report(client, ds5, &out);
		error = vigem_target_DS5_await_output_report_timeout(client, ds, 100, &out);
		
		if (VIGEM_SUCCESS(error))
		{
			cout << "Receive Output Report: " << endl;
			// cout << hexStr(out.Buffer, sizeof(DS5_OUTPUT_BUFFER)) << endl;
			
			RtlZeroMemory(outputData, sizeof(outputData));
			outputData[0] = 0x31;
			outputData[1] = outputSeq << 4;
			if (++outputSeq == 256)
			{
				outputSeq = 0;
			}
			outputData[2] = 0x10;
			RtlCopyMemory(outputData + 3, out.Buffer + 1, sizeof(DS5_OUTPUT_BUFFER) - 1);
			uint32_t crc = crc32(outputData, sizeof(outputData) - 4);
			outputData[sizeof(outputData) - 4] = (crc >> 0) & 0xFF;
			outputData[sizeof(outputData) - 3] = (crc >> 8) & 0xFF;
			outputData[sizeof(outputData) - 2] = (crc >> 16) & 0xFF;
			outputData[sizeof(outputData) - 1] = (crc >> 24) & 0xFF;
			
			// cout << "Send Output Report: ";
			// cout << hexStr(outputData, sizeof(outputData)) << endl;
			
			int writeResult = 0;
			{
				lock_guard<mutex> lock(g_hidMutex);
				writeResult = hid_write(device, outputData, sizeof(outputData));
			}
			if (writeResult < 0)
			{
				lock_guard<mutex> lock(g_hidMutex);
				cerr << "hid_write failed: " << hid_read_error(device) << endl;
			}
		}
		else if (error != VIGEM_ERROR_TIMED_OUT)
		{
			auto win32 = GetLastError();

			wcerr << L"vigem await output report failed. GetLastError="
					   << win32 << L" (" << Win32ErrorToString(win32) << L")\n";
		}
	}
}

/*int main()
{
	SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
	if (hid_init() < 0)
	{
		wcout << "HIDAPI Init Fail" << L"\n";
		return -1;
	}
	
	device = hid_open(0x054C, 0x0CE6, NULL);
	if (!device)
	{
		wcerr << "打开设备失败" << L"\n";
		hid_exit();
		return -1;
	}
	wcout << "连接设备成功" << L"\n";
	
	wchar_t manufacture_name[256];
	if (hid_get_manufacturer_string(device, manufacture_name, 256) >= 0)
	{
		wcout << L"ManufactureName: " << manufacture_name << L"\n";
	}
	else
	{
		wcerr << "ManufactureName 获取失败" << L"\n";
	}
	
	wchar_t product_name[256];
	if (hid_get_product_string(device, product_name, 256) >= 0)
	{
		wcout << L"ProductName: " << product_name << L"\n";
	}else
	{
		wcerr << "ProductName 获取失败" << L"\n";
	}
	
	wchar_t serial_number[256];
	if (hid_get_serial_number_string(device, serial_number, 256) >= 0)
	{
		wcout << L"SerialNumber: " << serial_number << L"\n";
	}else
	{
		wcerr << "SerialNumber 获取失败" << L"\n";
	}
	
	hid_set_nonblocking(device,1);
	
	unsigned char buf[78];
	while (true)
	{
		if (hid_read(device, buf, sizeof(buf)) > 0)
		{
			cout << hexStr(buf, 78) << endl;
		}
	}
}*/