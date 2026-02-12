#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "hid_handler.h"
#include <iostream>
#include <mutex>
#include <hidapi/hidapi.h>
#include <ViGEm/Client.h>
#include "utils.h"

using namespace std;

static mutex hidMutex;

hid_device* hid_handler::hid_device = nullptr;
PVIGEM_CLIENT hid_handler::vigem_client = nullptr;
PVIGEM_TARGET hid_handler::vigem_ds = nullptr;

int hid_handler::hid_handler_init()
{
    // 打开蓝牙DualSense设备
    if (hid_init() < 0)
    {
        wcout << "HIDAPI Init Fail" << L"\n";
        return 1;
    }
    hid_device = hid_open(0x054C, 0x0CE6, L"143a9ae438ec");
    if (!hid_device)
    {
        wcerr << "打开设备失败" << L"\n";
        hid_exit();
        return 1;
    }
    wcout << "连接设备成功" << L"\n";

    // 获取制造商名字，虚拟设备故意使用旧的名字便于HidHide识别
    wchar_t manufacture_name[256];
    if (hid_get_manufacturer_string(hid_device, manufacture_name, 256) >= 0)
    {
        wcout << L"ManufactureName: " << manufacture_name << L"\n";
    }
    else
    {
        wcerr << "ManufactureName 获取失败" << L"\n";
    }

    // 获取产品名称
    wchar_t product_name[256];
    if (hid_get_product_string(hid_device, product_name, 256) >= 0)
    {
        wcout << L"ProductName: " << product_name << L"\n";
    }
    else
    {
        wcerr << "ProductName 获取失败" << L"\n";
    }

    // 获取序列号
    wchar_t serial_number[256];
    if (hid_get_serial_number_string(hid_device, serial_number, 256) >= 0)
    {
        wcout << L"SerialNumber: " << serial_number << L"\n";
    }
    else
    {
        wcerr << "SerialNumber 获取失败" << L"\n";
    }

    hid_set_nonblocking(hid_device,1);

    // 初始化 ViGEmBus 虚拟设备 
	const auto client = vigem_alloc();
	auto error = vigem_connect(client);
	const auto ds = vigem_target_DS5_alloc();

	vigem_client = client;
	vigem_ds = ds;

    // busenum.cpp -> Bus_PlugInDevice
    error = vigem_target_add(client, ds);
    if (!VIGEM_SUCCESS(error))
    {
        DWORD win32 = ::GetLastError();
        wcerr << L"vigem_target_add failed. GetLastError="
            << win32 << L" (" << Win32ErrorToString(win32) << L")\n";
        return 1;
    }

    return 0;
}

void hid_handler::proxy_thread(stop_token stoken)
{
    uint8_t buf[78];
    while (!stoken.stop_requested())
    {
        int read;
        {
            scoped_lock lock(hidMutex);
            read = hid_read(hid_device, buf, sizeof(buf));
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
                    RtlCopyMemory(&report, buf + 2, sizeof(DS5_REPORT));

                    auto error = vigem_target_DS5_update(vigem_client, vigem_ds, report);
                    if (!VIGEM_SUCCESS(error))
                    {
                        cerr << "[App] Failed to send DS5 report." << endl;
                    }
                    break;
                }
            case 0x01:
                {
                    cout << "Receive 0x01 Input Report: " << hexStr(buf, 63) << endl;
                    break;
                }
            default:
                break;
            }
        }
        else if (read == 1)
        {
            scoped_lock lock(hidMutex);
            wcerr << "HID READ ERROR: " << hid_read_error(hid_device) << endl;
        }
    	Sleep(4);
    }
    if (hid_device)
    {
        cout << "Closing HIDAPI..." << endl;
        scoped_lock lock(hidMutex);
        hid_close(hid_device);
    	vigem_target_free(vigem_ds);
    	vigem_free(vigem_client);
    	if (hid_exit() == -1)
    	{
    		// 奇怪，不加这个就会导致退出时间很长
    		cerr << "Failed to exit HIDAPI." << endl;
    	}
    }
}

void hid_handler::output_report_thread(stop_token stoken)
{
	VIGEM_ERROR error;
	DS5_OUTPUT_BUFFER out;
	int outputSeq = 0;
	uint8_t outputData[78];
    while (!stoken.stop_requested()) 
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

			error = vigem_target_DS5_update(vigem_client, vigem_ds, report);
			if (VIGEM_SUCCESS(error))
				cout << "[App] DS5 report sent successfully." << endl;
			else
				cerr << "[App] Failed to send DS5 report." << endl;

			// 防止连续触发
			Sleep(200);
		}

    	// error = vigem_target_DS5_await_output_report(vigem_client, vigem_ds, &out);
		error = vigem_target_DS5_await_output_report_timeout(vigem_client, vigem_ds, 100, &out);
		
		if (VIGEM_SUCCESS(error))
		{
			cout << "Receive Output Report" << endl;
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
			fill_output_report_checksum(outputData,sizeof(outputData));
			
			// cout << "Send Output Report: ";
			// cout << hexStr(outputData, sizeof(outputData)) << endl;
			
			int writeResult = 0;
			{
				scoped_lock lock(hidMutex);
				writeResult = hid_write(hid_device, outputData, sizeof(outputData));
			}
			if (writeResult < 0)
			{
				scoped_lock lock(hidMutex);
				wcerr << "hid_write failed: " << hid_read_error(hid_device) << endl;
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