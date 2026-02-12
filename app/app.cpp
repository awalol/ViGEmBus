// app.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <iostream>

#include "audio_handler.h"
#include "hid_handler.h"

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

using namespace std;

jthread proxyThread;
jthread outputThread;
jthread audioThread;

//
// Ctrl+C 信号处理
//
static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
{
	if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_CLOSE_EVENT || ctrlType == CTRL_BREAK_EVENT)
	{
		audioThread.request_stop();
		audioThread.join();
		outputThread.request_stop();
		outputThread.join();
		proxyThread.request_stop();
		proxyThread.join();
		
		ExitProcess(0);
		return TRUE;
	}
	return FALSE;
}

int main()
{
	// 注册 Ctrl+C 处理，退出时保存WAV
	SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
	
	const int initResult = hid_handler::hid_handler_init();
	if (initResult != 0)
	{
		return initResult;
	}

	// 启动线程
	proxyThread = jthread(hid_handler::proxy_thread);
	outputThread = jthread(hid_handler::output_report_thread);
	audioThread = jthread(audio_handler::audio_thread);

	cout << "[App] Recording audio to DS5_audio_out.wav. Press 'k' to send report, Ctrl+C to stop." << endl;
	
	proxyThread.join();
	outputThread.join();
	audioThread.join();
	
	return 0;
}

// 测试用例
/*#include "utils.h"
int main()
{
	// SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
	if (hid_init() < 0)
	{
		wcout << "HIDAPI Init Fail" << L"\n";
		return -1;
	}
	
	auto device = hid_open(0x054C, 0x0CE6, NULL);
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
	
	uint8_t buf[78];
	while (true)
	{
		if (hid_read(device, buf, sizeof(buf)) > 0)
		{
			cout << hexStr(buf, 78) << endl;
		}
	}
}*/