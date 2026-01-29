#pragma once

#include "EmulationTargetPDO.hpp"

namespace ViGEm::Bus::Targets
{
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

    protected:
        void ProcessPendingNotification(WDFQUEUE Queue) override;
        void DmfDeviceModulesAdd(PVOID DmfModuleInit) override;
        
    private:
        static const int DS5_DESCRIPTOR_SIZE = 0x00E3;
        
		static PCWSTR _deviceDescription;
        
        
    };
}
