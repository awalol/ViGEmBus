#pragma once
#include <mutex>
#include <hidapi/hidapi.h>

#include "ViGEm/Client.h"

class hid_handler
{
public:
    static int hid_handler_init();
    static void proxy_thread(std::stop_token stoken);
    static void output_report_thread(std::stop_token stoken);

    // HIDAPI 连接的设备
    static hid_device* hid_device;
    // ViGEmClient
    static PVIGEM_CLIENT vigem_client;
    // ViGEm DS5 Target
    static PVIGEM_TARGET vigem_ds;
};
