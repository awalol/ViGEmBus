#pragma once

#include "EmulationTargetPDO.hpp"
#include <ViGEm/km/BusShared.h>

namespace ViGEm::Bus::Targets
{
    typedef struct _MAC2_ADDRESS
    {
        UCHAR Vendor0;
        UCHAR Vendor1;
        UCHAR Vendor2;
        UCHAR Nic0;
        UCHAR Nic1;
        UCHAR Nic2;
    } MAC2_ADDRESS, *PMAC2_ADDRESS;

    constexpr unsigned char get_low_byte(USHORT value)
    {
        return value & 0xFF;
    }

    constexpr unsigned char get_high_byte(USHORT value)
    {
        return (value >> 8) & 0xFF;
    }

    class EmulationTargetDS5 : public Core::EmulationTargetPDO
    {
    public:
        EmulationTargetDS5(ULONG Serial, LONG SessionId, USHORT VendorId = 0x054C, USHORT ProductId = 0x0CE6);
        NTSTATUS PdoPrepareDevice(
            PWDFDEVICE_INIT DeviceInit,
            PUNICODE_STRING DeviceId,
            PUNICODE_STRING DeviceDescription) override;
        NTSTATUS PdoPrepareHardware() override;
        NTSTATUS PdoInitContext() override;
        NTSTATUS UsbGetDeviceDescriptorType(PUSB_DEVICE_DESCRIPTOR pDescriptor) override;
        NTSTATUS UsbClassInterface(PURB Urb) override;
        NTSTATUS UsbGetDescriptorFromInterface(PURB Urb) override;
        NTSTATUS UsbSelectInterface(PURB Urb) override;
        NTSTATUS UsbGetStringDescriptorType(PURB Urb) override;
        NTSTATUS UsbBulkOrInterruptTransfer(_URB_BULK_OR_INTERRUPT_TRANSFER* pTransfer, WDFREQUEST Request) override;
        NTSTATUS UsbControlTransfer(PURB Urb) override;
        void GetConfigurationDescriptorType(PUCHAR Buffer, ULONG Length) override;
        NTSTATUS SelectConfiguration(PURB Urb) override;
        void AbortPipe() override;
        NTSTATUS SubmitReportImpl(PVOID NewReport) override;
        
		VOID SetOutputReportNotifyModule(DMFMODULE Module);

    private:
        static EVT_WDF_TIMER PendingUsbRequestsTimerFunc;

        static VOID GenerateRandomMacAddress(PMAC2_ADDRESS Address);

    protected:
        void ProcessPendingNotification(WDFQUEUE Queue) override;
        void DmfDeviceModulesAdd(PVOID DmfModuleInit) override;

    private:
        static const int DS5_DESCRIPTOR_SIZE = 0x00E3;

        static PCWSTR _deviceDescription;
        static const int DS5_QUEUE_FLUSH_PERIOD = 0x05;
        static const int DS5_REPORT_SIZE = 0x40;

        // USB Audio
        static const int AUDIO_REQUEST_GET_CUR = 0x81;
        static const int AUDIO_REQUEST_SET_CUR = 0x01;
        static const int AUDIO_REQUEST_MUTE_CONTROL = 0x81;
        static const int AUDIO_MUTE_CONTROL = 0x01;
        static const int AUDIO_VOLUME_CONTROL = 0x02;

        static const int AUDIO_REQUEST_GET_MIN = 0x82;
        static const int AUDIO_REQUEST_GET_MAX = 0x83;
        static const int AUDIO_REQUEST_GET_RES = 0x84;

        static const int DS5_PRODUCT_NAME_LENGTH = 0x3C;

        static const int DS5_OUTPUT_BUFFER_OFFSET = 0x04;
        static const int DS5_OUTPUT_BUFFER_LENGTH = 0x05;

        //
        // HID Input Report buffer
        // URB_INTERRUPT in?
        //
        UCHAR _Report[DS5_REPORT_SIZE];

        //
        // Output report cache
        //
        DS5_OUTPUT_REPORT _OutputReport;

        //
        // Timer for dispatching interrupt transfer
        //
        WDFTIMER _PendingUsbInRequestsTimer;

        //
        // Auto-generated MAC address of the target device
        //
        MAC2_ADDRESS _TargetMacAddress;
        
        //
        // Memory for full output report request
        // 
        DS5_AWAIT_OUTPUT _AwaitOutputCache;
        
        //
        // User-mode notification on new output report
        // 
        DMFMODULE _OutputReportNotify;
    };
}
