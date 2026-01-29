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

#pragma comment(lib, "setupapi.lib")

static std::string hexStr(unsigned char* data, int len)
{
	std::stringstream ss;
	ss << std::hex;
	for (int i = 0; i < len; ++i)
		ss << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]) << ' ';
	return ss.str();
}

static std::wstring Win32ErrorToString(DWORD error)
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
		return L"FormatMessageW failed. error=" + std::to_wstring(fmErr) +
			   L", original error=" + std::to_wstring(error);
	}

	std::wstring msg(buffer, len);
	::LocalFree(buffer);

	// 去掉系统消息末尾常见的 \r\n
	while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n'))
		msg.pop_back();

	return msg;
}

int main()
{
	const auto client = vigem_alloc();

	auto error = vigem_connect(client);

	const auto ds = vigem_target_ds5_alloc();

	// busenum.cpp -> Bus_PlugInDevice
	error = vigem_target_add(client, ds);
	if (!VIGEM_SUCCESS(error))
	{
		DWORD win32 = ::GetLastError();
		std::wcerr << L"vigem_target_add failed. GetLastError="
				   << win32 << L" (" << Win32ErrorToString(win32) << L")\n";
	}

	// // 设备就绪，可发送报告
	// XUSB_REPORT report = {0};
	// report.wButtons = XUSB_GAMEPAD_A;
	// vigem_target_x360_send_report(target, report);
	
	// DS4_OUTPUT_BUFFER out;

	while (TRUE) 
	{
		/*//error = vigem_target_ds4_await_output_report(client, ds4, &out);
		error = vigem_target_ds4_await_output_report_timeout(client, ds, 100, &out);
		
		if (VIGEM_SUCCESS(error))
		{
			std::cout << hexStr(out.Buffer, sizeof(DS4_OUTPUT_BUFFER)) << std::endl;
		}
		else if (error != VIGEM_ERROR_TIMED_OUT)
		{
			auto win32 = GetLastError();

			auto t = 0;
		}*/
	}
}
