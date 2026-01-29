#include "Ds5Pdo.hpp"
#include "trace.h"
#include "Ds5Pdo.tmh"
#define NTSTRSAFE_LIB
#include <ntstrsafe.h>

PCWSTR ViGEm::Bus::Targets::EmulationTargetDS5::_deviceDescription = L"Virtual DualSense 5 Controller";

// 初始化
ViGEm::Bus::Targets::EmulationTargetDS5::EmulationTargetDS5(ULONG Serial, LONG SessionId, USHORT VendorId,
                                                            USHORT ProductId) : EmulationTargetPDO(
    Serial, SessionId, VendorId, ProductId)
{
    this->_TargetType = DualSense5Wired;
    this->_UsbConfigurationDescriptionSize = DS5_DESCRIPTOR_SIZE;

    //
    // Set PNP Capabilities
    // 
    this->_PnpCapabilities.SurpriseRemovalOK = WdfTrue;

    //
    // Set Power Capabilities
    // 
    this->_PowerCapabilities.DeviceState[PowerSystemWorking] = PowerDeviceD0;
    this->_PowerCapabilities.WakeFromD0 = WdfTrue;
}

// 创建设备阶段
NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::PdoPrepareDevice(PWDFDEVICE_INIT DeviceInit, PUNICODE_STRING DeviceId,
                                                                   PUNICODE_STRING DeviceDescription)
{
    NTSTATUS status;
    DECLARE_UNICODE_STRING_SIZE(buffer, _maxHardwareIdLength);

    // prepare device description
    status = RtlUnicodeStringInit(DeviceDescription, _deviceDescription);
    if (!NT_SUCCESS(status))
    {
        TraceError(
            TRACE_DS5,
            "RtlUnicodeStringInit failed with status %!STATUS!",
            status);
        return status;
    }

    // Set hardware IDs
    RtlUnicodeStringPrintf(&buffer, L"USB\\VID_%04X&PID_%04X&REV_0100", this->_VendorId, this->_ProductId);

    status = WdfPdoInitAddHardwareID(DeviceInit, &buffer);
    if (!NT_SUCCESS(status))
    {
        TraceError(
            TRACE_DS5,
            "WdfPdoInitAddHardwareID failed with status %!STATUS!",
            status);
        return status;
    }

    RtlUnicodeStringCopy(DeviceId, &buffer);

    RtlUnicodeStringPrintf(&buffer, L"USB\\VID_%04X&PID_%04X", this->_VendorId, this->_ProductId);

    status = WdfPdoInitAddHardwareID(DeviceInit, &buffer);
    if (!NT_SUCCESS(status))
    {
        TraceError(
            TRACE_DS5,
            "WdfPdoInitAddHardwareID failed with status %!STATUS!",
            status);
        return status;
    }
    
    // Set compatible IDs
    RtlUnicodeStringInit(&buffer, L"USB\\Class_03&SubClass_00&Prot_00");

    status = WdfPdoInitAddCompatibleID(DeviceInit, &buffer);
    if (!NT_SUCCESS(status))
    {
        TraceError(
            TRACE_DS5,
            "WdfPdoInitAddCompatibleID (#01) failed with status %!STATUS!",
            status);
        return status;
    }

    RtlUnicodeStringInit(&buffer, L"USB\\Class_03&SubClass_00");

    status = WdfPdoInitAddCompatibleID(DeviceInit, &buffer);
    if (!NT_SUCCESS(status))
    {
        TraceError(
            TRACE_DS5,
            "WdfPdoInitAddCompatibleID (#02) failed with status %!STATUS!",
            status);
        return status;
    }

    RtlUnicodeStringInit(&buffer, L"USB\\Class_03");

    status = WdfPdoInitAddCompatibleID(DeviceInit, &buffer);
    if (!NT_SUCCESS(status))
    {
        TraceError(
            TRACE_DS5,
            "WdfPdoInitAddCompatibleID (#03) failed with status %!STATUS!",
            status);
        return status;
    }
    
    return STATUS_SUCCESS;
}

NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::PdoPrepareHardware()
{
    return STATUS_SUCCESS;
}

NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::PdoInitContext()
{
    return STATUS_SUCCESS;
}

NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::UsbGetDeviceDescriptorType(PUSB_DEVICE_DESCRIPTOR pDescriptor)
{
    return STATUS_SUCCESS;
}

NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::UsbClassInterface(PURB Urb)
{
    return STATUS_SUCCESS;
}

NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::UsbGetDescriptorFromInterface(PURB Urb)
{
    return STATUS_SUCCESS;
}

NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::UsbSelectInterface(PURB Urb)
{
    return STATUS_SUCCESS;
}

NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::UsbGetStringDescriptorType(PURB Urb)
{
    return STATUS_SUCCESS;
}

NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::UsbBulkOrInterruptTransfer(_URB_BULK_OR_INTERRUPT_TRANSFER* pTransfer,
                                                                             WDFREQUEST Request)
{
    return STATUS_SUCCESS;
}

NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::UsbControlTransfer(PURB Urb)
{
    return STATUS_SUCCESS;
}

void ViGEm::Bus::Targets::EmulationTargetDS5::GetConfigurationDescriptorType(PUCHAR Buffer, ULONG Length)
{
}

NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::SelectConfiguration(PURB Urb)
{
    return STATUS_SUCCESS;
}

void ViGEm::Bus::Targets::EmulationTargetDS5::AbortPipe()
{
}

NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::SubmitReportImpl(PVOID NewReport)
{
    return STATUS_SUCCESS;
}

void ViGEm::Bus::Targets::EmulationTargetDS5::ProcessPendingNotification(WDFQUEUE Queue)
{
}

void ViGEm::Bus::Targets::EmulationTargetDS5::DmfDeviceModulesAdd(PVOID DmfModuleInit)
{
}
