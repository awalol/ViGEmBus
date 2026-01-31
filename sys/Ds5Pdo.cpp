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

NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::PdoInitContext()
{
    NTSTATUS status;

    // Initialize periodic timer
    WDF_TIMER_CONFIG timerConfig;
    WDF_TIMER_CONFIG_INIT_PERIODIC(
        &timerConfig,
        PendingUsbRequestsTimerFunc, // 处理 URB_INTERRUPT in ?
        DS5_QUEUE_FLUSH_PERIOD
    );

    // Timer object attributes
    WDF_OBJECT_ATTRIBUTES timerAttribs;
    WDF_OBJECT_ATTRIBUTES_INIT(&timerAttribs);

    // PDO is parent
    timerAttribs.ParentObject = this->_PdoDevice;

    do
    {
        // Create timer
        if (!NT_SUCCESS(status = WdfTimerCreate(
            &timerConfig,
            &timerAttribs,
            &this->_PendingUsbInRequestsTimer
        )))
        {
            TraceError(
                TRACE_DS5,
                "WdfTimerCreate failed with status %!STATUS!",
                status);
            break;
        }

        // Load/generate MAC address
        // USB 本身没有MAC地址这个概念，这里的MAC地址在后期会在报文 HID_REPORT_MAC_ADDRESSES_ID(0x12) 进行提供

        // HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\ViGEmBus\Parameters\Targets\
        // 先读取注册表，如果不存在再随机生成

        // 
        // TODO: tidy up this region
        // 

        WDFKEY keyParams, keyTargets, keyDS, keySerial;
        UNICODE_STRING keyName, valueName;

        if (!NT_SUCCESS(status = WdfDriverOpenParametersRegistryKey(
            WdfGetDriver(),
            STANDARD_RIGHTS_ALL,
            WDF_NO_OBJECT_ATTRIBUTES,
            &keyParams
        )))
        {
            TraceError(
                TRACE_DS5,
                "WdfDriverOpenParametersRegistryKey failed with status %!STATUS!",
                status);
            break;
        }

        RtlUnicodeStringInit(&keyName, L"Targets");

        if (!NT_SUCCESS(status = WdfRegistryCreateKey(
            keyParams,
            &keyName,
            KEY_ALL_ACCESS,
            REG_OPTION_NON_VOLATILE,
            nullptr,
            WDF_NO_OBJECT_ATTRIBUTES,
            &keyTargets
        )))
        {
            TraceError(
                TRACE_DS5,
                "WdfRegistryCreateKey failed with status %!STATUS!",
                status);
            break;
        }

        RtlUnicodeStringInit(&keyName, L"DualShock");

        if (!NT_SUCCESS(status = WdfRegistryCreateKey(
            keyTargets,
            &keyName,
            KEY_ALL_ACCESS,
            REG_OPTION_NON_VOLATILE,
            nullptr,
            WDF_NO_OBJECT_ATTRIBUTES,
            &keyDS
        )))
        {
            TraceError(
                TRACE_DS5,
                "WdfRegistryCreateKey failed with status %!STATUS!",
                status);
            break;
        }

        DECLARE_UNICODE_STRING_SIZE(serialPath, 4);
        RtlUnicodeStringPrintf(&serialPath, L"%04d", this->_SerialNo);

        if (!NT_SUCCESS(status = WdfRegistryCreateKey(
            keyDS,
            &serialPath,
            KEY_ALL_ACCESS,
            REG_OPTION_NON_VOLATILE,
            nullptr,
            WDF_NO_OBJECT_ATTRIBUTES,
            &keySerial
        )))
        {
            TraceError(
                TRACE_DS5,
                "WdfRegistryCreateKey failed with status %!STATUS!",
                status);
            break;
        }

        RtlUnicodeStringInit(&valueName, L"TargetMacAddress");

        status = WdfRegistryQueryValue(
            keySerial,
            &valueName,
            sizeof(MAC2_ADDRESS),
            &this->_TargetMacAddress,
            nullptr,
            nullptr
        );

        TraceEvents(TRACE_LEVEL_INFORMATION,
                    TRACE_DS5,
                    "MAC-Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                    this->_TargetMacAddress.Vendor0,
                    this->_TargetMacAddress.Vendor1,
                    this->_TargetMacAddress.Vendor2,
                    this->_TargetMacAddress.Nic0,
                    this->_TargetMacAddress.Nic1,
                    this->_TargetMacAddress.Nic2);

        if (status == STATUS_OBJECT_NAME_NOT_FOUND)
        {
            GenerateRandomMacAddress(&this->_TargetMacAddress);

            if (!NT_SUCCESS(status = WdfRegistryAssignValue(
                keySerial,
                &valueName,
                REG_BINARY,
                sizeof(MAC2_ADDRESS),
                static_cast<PVOID>(&this->_TargetMacAddress)
            )))
            {
                TraceError(
                    TRACE_DS5,
                    "WdfRegistryAssignValue failed with status %!STATUS!",
                    status);
                break;
            }
        }
        else if (!NT_SUCCESS(status))
        {
            TraceError(
                TRACE_DS5,
                "WdfRegistryQueryValue failed with status %!STATUS!",
                status);
            break;
        }

        WdfRegistryClose(keySerial);
        WdfRegistryClose(keyDS);
        WdfRegistryClose(keyTargets);
        WdfRegistryClose(keyParams);
    }
    while (FALSE);

    return status;
}

NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::PdoPrepareHardware()
{
    // 不知道是什么，先把手柄第一次0x01报文的内容放上去了
    // Set default HID input report (everything zero`d)
    UCHAR DefaultHidReport[DS5_REPORT_SIZE] =
    {
        0x01, 0x81, 0x83, 0x7f, 0x7f, 0x00, 0x00, 0x01,
        0x08, 0x00, 0x00, 0x00, 0x82, 0x98, 0xd9, 0xad,
        0xf8, 0xff, 0x03, 0x00, 0xf5, 0xff, 0x99, 0x06,
        0x6f, 0x1d, 0x1b, 0xf5, 0x73, 0x7d, 0x24, 0x00,
        0xfa, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
        0x00, 0x00, 0x09, 0x09, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x97, 0x46, 0x41, 0x00, 0x09, 0x08, 0x00,
        0xce, 0x34, 0xad, 0x3d, 0xc7, 0x91, 0x3c, 0x09
    };

    // Initialize HID reports to defaults
    RtlCopyBytes(this->_Report, DefaultHidReport, DS5_REPORT_SIZE);
    // RtlZeroMemory(&this->_OutputReport, sizeof(DS5_OUTPUT_REPORT)); //这个还没找到对应的报文是什么，看DS4那边是震动和灯条？

    // Start pending IRP queue flush timer
    WdfTimerStart(this->_PendingUsbInRequestsTimer, DS5_QUEUE_FLUSH_PERIOD);

    return STATUS_SUCCESS;
}

// GET DESCRIPTOR Response DEVICE
NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::UsbGetDeviceDescriptorType(PUSB_DEVICE_DESCRIPTOR pDescriptor)
{
    pDescriptor->bLength = 0x12;
    pDescriptor->bDescriptorType = USB_DEVICE_DESCRIPTOR_TYPE;
    pDescriptor->bcdUSB = 0x0200; // USB v2.0
    pDescriptor->bDeviceClass = 0x00; // per Interface
    pDescriptor->bDeviceSubClass = 0x00;
    pDescriptor->bDeviceProtocol = 0x00;
    pDescriptor->bMaxPacketSize0 = 0x40;
    pDescriptor->idVendor = this->_VendorId;
    pDescriptor->idProduct = this->_ProductId;
    pDescriptor->bcdDevice = 0x0100;
    pDescriptor->iManufacturer = 0x01;
    pDescriptor->iProduct = 0x02;
    pDescriptor->iSerialNumber = 0x00;
    pDescriptor->bNumConfigurations = 0x01;

    return STATUS_SUCCESS;
}

// GET DESCRIPTOR Response CONFIGURATION
VOID ViGEm::Bus::Targets::EmulationTargetDS5::GetConfigurationDescriptorType(PUCHAR Buffer, ULONG Length)
{
    UCHAR DS5DescriptorData[DS5_DESCRIPTOR_SIZE] =
    {
        // --- CONFIGURATION DESCRIPTOR ---
        0x09, // bLength
        0x02, // bDescriptorType (CONFIGURATION)
        0xE3, 0x00, // wTotalLength: 227
        0x04, // bNumInterfaces: 4
        0x01, // bConfigurationValue: 1
        0x00, // iConfiguration: 0
        0xC0, // bmAttributes: SELF-POWERED
        0xFA, // bMaxPower: 500mA (250 * 2mA)

        // --- INTERFACE DESCRIPTOR (0.0): Audio Control ---
        0x09, // bLength
        0x04, // bDescriptorType (INTERFACE)
        0x00, // bInterfaceNumber: 0
        0x00, // bAlternateSetting: 0
        0x00, // bNumEndpoints: 0
        0x01, // bInterfaceClass: Audio (0x01)
        0x01, // bInterfaceSubClass: Audio Control (0x01)
        0x00, // bInterfaceProtocol: 0x00
        0x00, // iInterface: 0

        // Class-specific AC Interface Header Descriptor
        0x0A, // bLength: 10
        0x24, // bDescriptorType: CS_INTERFACE (0x24)
        0x01, // bDescriptorSubtype: Header (0x01)
        0x00, 0x01, // bcdADC: 1.00
        0x49, 0x00, // wTotalLength: 73 (0x0049)
        0x02, // bInCollection: 2 (Streaming interfaces)
        0x01, // baInterfaceNr(1): 1
        0x02, // baInterfaceNr(2): 2

        // Input Terminal Descriptor (Terminal ID 1: USB Streaming)
        0x0C, // bLength: 12
        0x24, // bDescriptorType: CS_INTERFACE
        0x02, // bDescriptorSubtype: Input Terminal (0x02)
        0x01, // bTerminalID: 1
        0x01, 0x01, // wTerminalType: USB Streaming (0x0101)
        0x06, // bAssocTerminal: 6
        0x04, // bNrChannels: 4
        0x33, 0x00, // wChannelConfig: 0x0033 (L/R Front, L/R Surround)
        0x00, // iChannelNames: 0
        0x00, // iTerminal: 0

        // Feature Unit Descriptor (Unit ID 2, Source ID 1)
        0x0C, // bLength: 12
        0x24, // bDescriptorType: CS_INTERFACE
        0x06, // bDescriptorSubtype: Feature Unit (0x06)
        0x02, // bUnitID: 2
        0x01, // bSourceID: 1
        0x01, // bControlSize: 1
        0x03, // bmaControls(0): Master (Mute, Volume)
        0x00, // bmaControls(1): Ch 1
        0x00, // bmaControls(2): Ch 2
        0x00, // bmaControls(3): Ch 3
        0x00, // bmaControls(4): Ch 4
        0x00, // iFeature: 0

        // Output Terminal Descriptor (Terminal ID 3: Speaker)
        0x09, // bLength: 9
        0x24, // bDescriptorType: CS_INTERFACE
        0x03, // bDescriptorSubtype: Output Terminal (0x03)
        0x03, // bTerminalID: 3
        0x01, 0x03, // wTerminalType: Speaker (0x0301)
        0x04, // bAssocTerminal: 4
        0x02, // bSourceID: 2
        0x00, // iTerminal: 0

        // --- INTERFACE DESCRIPTOR (1.1): Audio Streaming ---
        0x09, // bLength
        0x04, // bDescriptorType (INTERFACE)
        0x01, // bInterfaceNumber: 1
        0x01, // bAlternateSetting: 1
        0x01, // bNumEndpoints: 1
        0x01, // bInterfaceClass: Audio
        0x02, // bInterfaceSubClass: Audio Streaming
        0x00, // bInterfaceProtocol
        0x00, // iInterface

        // AS General Descriptor
        0x07, // bLength: 7
        0x24, // bDescriptorType: CS_INTERFACE
        0x01, // bDescriptorSubtype: AS_GENERAL
        0x01, // bTerminalLink: 1
        0x01, // bDelay: 1 frame
        0x01, 0x00, // wFormatTag: PCM (0x0001)

        // Format Type Descriptor (PCM, 48000Hz)
        0x0B, // bLength: 11
        0x24, // bDescriptorType: CS_INTERFACE
        0x02, // bDescriptorSubtype: FORMAT_TYPE (0x02)
        0x01, // bFormatType: 1
        0x04, // bNrChannels: 4
        0x02, // bSubframeSize: 2
        0x10, // bBitResolution: 16
        0x01, // bSamFreqType: 1
        0x80, 0xBB, 0x00, // tSamFreq: 48000 (0x00BB80)

        // Endpoint Descriptor (Audio OUT)
        0x09, // bLength: 9
        0x05, // bDescriptorType (ENDPOINT)
        0x01, // bEndpointAddress: 0x01 (OUT)
        0x09, // bmAttributes: Isochronous, Adaptive
        0x88, 0x01, // wMaxPacketSize: 392
        0x04, // bInterval: 4
        0x00, // bRefresh
        0x00, // bSynchAddress

        // --- INTERFACE DESCRIPTOR (3.0): HID ---
        0x09, // bLength
        0x04, // bDescriptorType (INTERFACE)
        0x03, // bInterfaceNumber: 3
        0x00, // bAlternateSetting: 0
        0x02, // bNumEndpoints: 2
        0x03, // bInterfaceClass: HID (0x03)
        0x00, // bInterfaceSubClass: No Subclass
        0x00, // bInterfaceProtocol
        0x00, // iInterface

        // HID Descriptor
        0x09, // bLength: 9
        0x21, // bDescriptorType (HID)
        0x11, 0x01, // bcdHID: 1.11
        0x00, // bCountryCode
        0x01, // bNumDescriptors
        0x22, // bDescriptorType (Report)
        0x21, 0x01, // wDescriptorLength: 289 (0x0121)

        // Endpoint Descriptor (HID IN)
        0x07, // bLength
        0x05, // bDescriptorType (ENDPOINT)
        0x84, // bEndpointAddress: 0x84 (IN)
        0x03, // bmAttributes: Interrupt
        0x40, 0x00, // wMaxPacketSize: 64
        0x06, // bInterval: 6
    };

    RtlCopyBytes(Buffer, DS5DescriptorData, Length);
}

// SET CONFIGURATION Response
// DS5 好像没有这一环节?Response没内容，然后后面跟了两次解析不了且没内容的 7f?
NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::SelectConfiguration(PURB Urb)
{
    return STATUS_SUCCESS;
}

// URB_Function: URB_FUNCTION_CLASS_INTERFACE usb.function == 0x001b
NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::UsbClassInterface(PURB Urb)
{
    struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST* pRequest = &Urb->UrbControlVendorClassRequest;

    TraceVerbose(
        TRACE_USBPDO,
        ">> >> >> URB_FUNCTION_CLASS_INTERFACE");
    TraceVerbose(
        TRACE_USBPDO,
        ">> >> >> TransferFlags = 0x%X, Request = 0x%X, Value = 0x%X, Index = 0x%X, BufLen = %d",
        pRequest->TransferFlags,
        pRequest->Request,
        pRequest->Value,
        pRequest->Index,
        pRequest->TransferBufferLength);

    switch (pRequest->RequestTypeReservedBits)
    {
    case 0x21: // Audio
        {
            // Setup Data里面bRequest的值
            switch (pRequest->Request)
            {
            case AUDIO_REQUEST_GET_CUR:
                {
                    UCHAR channel = get_low_byte(pRequest->Value); // Channel Number
                    // Feature Unit Control Selector: MUTE_CONTROL (0x01) VOLUME_CONTROL (0x02)
                    UCHAR feature = get_high_byte(pRequest->Value);

                    TraceVerbose(TRACE_USBPDO,
                                 ">> >> >> >> GET_CUR(%d): %d",
                                 channel, feature);

                    switch (feature)
                    {
                    case AUDIO_MUTE_CONTROL:
                        {
                            UCHAR Response[] = {
                                0x00 // False
                            };
                            pRequest->TransferBufferLength = ARRAYSIZE(Response);
                            RtlCopyBytes(pRequest->TransferBuffer, Response, ARRAYSIZE(Response));
                            break;
                        }
                    case AUDIO_VOLUME_CONTROL:
                        {
                            UCHAR Response[] = {
                                0x00, 0x9c // -100.0 dB
                            };
                            pRequest->TransferBufferLength = ARRAYSIZE(Response);
                            RtlCopyBytes(pRequest->TransferBuffer, Response, ARRAYSIZE(Response));
                            break;
                        }
                    default:
                        break;
                    }
                    break;
                }
            case AUDIO_REQUEST_SET_CUR:
                {
                    UCHAR channel = get_low_byte(pRequest->Value);
                    // Feature Unit Control Selector: MUTE_CONTROL (0x01) VOLUME_CONTROL (0x02)
                    UCHAR feature = get_high_byte(pRequest->Value); // Channel Number

                    TraceVerbose(TRACE_USBPDO,
                                 ">> >> >> >> SET_CUR(%d): %d",
                                 channel, feature);

                    switch (feature)
                    {
                    case AUDIO_VOLUME_CONTROL:
                        {
                            UCHAR Response[] = {
                                0x00, 0x9c // -100.0 dB
                            };
                            pRequest->TransferBufferLength = ARRAYSIZE(Response);
                            RtlCopyBytes(pRequest->TransferBuffer, Response, ARRAYSIZE(Response));
                            break;
                        }
                    }
                    break;
                }
            case AUDIO_REQUEST_GET_MIN:
                {
                    UCHAR channel = get_low_byte(pRequest->Value);
                    // Feature Unit Control Selector: MUTE_CONTROL (0x01) VOLUME_CONTROL (0x02)
                    UCHAR feature = get_high_byte(pRequest->Value); // Channel Number

                    TraceVerbose(TRACE_USBPDO,
                                 ">> >> >> >> GET_MIN(%d): %d",
                                 channel, feature);

                    switch (feature)
                    {
                    case AUDIO_VOLUME_CONTROL:
                        {
                            UCHAR Response[] = {
                                0x00, 0x9c // -100.0 dB
                            };
                            pRequest->TransferBufferLength = ARRAYSIZE(Response);
                            RtlCopyBytes(pRequest->TransferBuffer, Response, ARRAYSIZE(Response));
                            break;
                        }
                    }
                    break;
                }
            case AUDIO_REQUEST_GET_MAX:
                {
                    UCHAR channel = get_low_byte(pRequest->Value);
                    // Feature Unit Control Selector: MUTE_CONTROL (0x01) VOLUME_CONTROL (0x02)
                    UCHAR feature = get_high_byte(pRequest->Value); // Channel Number

                    TraceVerbose(TRACE_USBPDO,
                                 ">> >> >> >> GET_MAX(%d): %d",
                                 channel, feature);

                    switch (feature)
                    {
                    case AUDIO_VOLUME_CONTROL:
                        {
                            UCHAR Response[] = {
                                0x00, 0x00 // 0.0 dB
                            };
                            pRequest->TransferBufferLength = ARRAYSIZE(Response);
                            RtlCopyBytes(pRequest->TransferBuffer, Response, ARRAYSIZE(Response));
                            break;
                        }
                    }
                    break;
                }
            case AUDIO_REQUEST_GET_RES:
                {
                    UCHAR channel = get_low_byte(pRequest->Value);
                    // Feature Unit Control Selector: MUTE_CONTROL (0x01) VOLUME_CONTROL (0x02)
                    UCHAR feature = get_high_byte(pRequest->Value); // Channel Number

                    TraceVerbose(TRACE_USBPDO,
                                 ">> >> >> >> GET_RES(%d): %d",
                                 channel, feature);

                    switch (feature)
                    {
                    case AUDIO_VOLUME_CONTROL:
                        {
                            UCHAR Response[] = {
                                0x7a, 0x00 // 0.4766 db
                            };
                            pRequest->TransferBufferLength = ARRAYSIZE(Response);
                            RtlCopyBytes(pRequest->TransferBuffer, Response, ARRAYSIZE(Response));
                            break;
                        }
                    }
                    break;
                }
            }
            break;
        }
    default :
        break;
    }

    return STATUS_SUCCESS;
}

// GET DESCRIPTOR Response HID Report
NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::UsbGetDescriptorFromInterface(PURB Urb)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    UCHAR DS5HidReportDescriptor[] =
    {
        0x05, 0x01, // Usage Page (Generic Desktop Ctrls)
        0x09, 0x05, // Usage (Game Pad)
        0xA1, 0x01, // Collection (Application)
        0x85, 0x01, //   Report ID (1)
        0x09, 0x30, //   Usage (X)
        0x09, 0x31, //   Usage (Y)
        0x09, 0x32, //   Usage (Z)
        0x09, 0x35, //   Usage (Rz)
        0x09, 0x33, //   Usage (Rx)
        0x09, 0x34, //   Usage (Ry)
        0x15, 0x00, //   Logical Minimum (0)
        0x26, 0xFF, 0x00, //   Logical Maximum (255)
        0x75, 0x08, //   Report Size (8)
        0x95, 0x06, //   Report Count (6)
        0x81, 0x02, //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
        0x06, 0x00, 0xFF, //   Usage Page (Vendor Defined 0xFF00)
        0x09, 0x20, //   Usage (0x20)
        0x95, 0x01, //   Report Count (1)
        0x81, 0x02, //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
        0x05, 0x01, //   Usage Page (Generic Desktop Ctrls)
        0x09, 0x39, //   Usage (Hat switch)
        0x15, 0x00, //   Logical Minimum (0)
        0x25, 0x07, //   Logical Maximum (7)
        0x35, 0x00, //   Physical Minimum (0)
        0x46, 0x3B, 0x01, //   Physical Maximum (315)
        0x65, 0x14, //   Unit (System: English Rotation, Length: Centimeter)
        0x75, 0x04, //   Report Size (4)
        0x95, 0x01, //   Report Count (1)
        0x81, 0x42, //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,Null State)
        0x65, 0x00, //   Unit (None)
        0x05, 0x09, //   Usage Page (Button)
        0x19, 0x01, //   Usage Minimum (0x01)
        0x29, 0x0F, //   Usage Maximum (0x0F)
        0x15, 0x00, //   Logical Minimum (0)
        0x25, 0x01, //   Logical Maximum (1)
        0x75, 0x01, //   Report Size (1)
        0x95, 0x0F, //   Report Count (15)
        0x81, 0x02, //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
        0x06, 0x00, 0xFF, //   Usage Page (Vendor Defined 0xFF00)
        0x09, 0x21, //   Usage (0x21)
        0x95, 0x0D, //   Report Count (13)
        0x81, 0x02, //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
        0x06, 0x00, 0xFF, //   Usage Page (Vendor Defined 0xFF00)
        0x09, 0x22, //   Usage (0x22)
        0x15, 0x00, //   Logical Minimum (0)
        0x26, 0xFF, 0x00, //   Logical Maximum (255)
        0x75, 0x08, //   Report Size (8)
        0x95, 0x34, //   Report Count (52)
        0x81, 0x02, //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
        0x85, 0x02, //   Report ID (2)
        0x09, 0x23, //   Usage (0x23)
        0x95, 0x2F, //   Report Count (47)
        0x91, 0x02, //   Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
        0x85, 0x05, //   Report ID (5)
        0x09, 0x33, //   Usage (0x33)
        0x95, 0x28, //   Report Count (40)
        0xB1, 0x02, //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
        0x85, 0x08, //   Report ID (8)
        0x09, 0x34, //   Usage (0x34)
        0x95, 0x2F, //   Report Count (47)
        0xB1, 0x02, //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
        0x85, 0x09, //   Report ID (9)
        0x09, 0x24, //   Usage (0x24)
        0x95, 0x13, //   Report Count (19)
        0xB1, 0x02, //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
        0x85, 0x0A, //   Report ID (10)
        0x09, 0x25, //   Usage (0x25)
        0x95, 0x1A, //   Report Count (26)
        0xB1, 0x02, //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
        0x85, 0x0B, 0x09, 0x41, 0x95, 0x29, 0xB1, 0x02, // ID 0B (新增项)
        0x85, 0x0C, 0x09, 0x42, 0x95, 0x29, 0xB1, 0x02, // ID 0C (新增项)
        0x85, 0x20, //   Report ID (32)
        0x09, 0x26, //   Usage (0x26)
        0x95, 0x3F, //   Report Count (63)
        0xB1, 0x02, //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
        0x85, 0x21, //   Report ID (33)
        0x09, 0x27, //   Usage (0x27)
        0x95, 0x04, //   Report Count (4)
        0xB1, 0x02, //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
        0x85, 0x22, //   Report ID (34)
        0x09, 0x40, //   Usage (0x40)
        0x95, 0x3F, //   Report Count (63)
        0xB1, 0x02, //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
        0x85, 0x80, //   Report ID (128)
        0x09, 0x28, //   Usage (0x28)
        0x95, 0x3F, //   Report Count (63)
        0xB1, 0x02, //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
        0x85, 0x81, //   Report ID (129)
        0x09, 0x29, //   Usage (0x29)
        0x95, 0x3F, //   Report Count (63)
        0xB1, 0x02, //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
        0x85, 0x82, //   Report ID (130)
        0x09, 0x2A, //   Usage (0x2A)
        0x95, 0x09, //   Report Count (9)
        0xB1, 0x02, //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
        0x85, 0x83, //   Report ID (131)
        0x09, 0x2B, //   Usage (0x2B)
        0x95, 0x3F, //   Report Count (63)
        0xB1, 0x02, //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
        0x85, 0x84, //   Report ID (132)
        0x09, 0x2C, //   Usage (0x2C)
        0x95, 0x3F, //   Report Count (63)
        0xB1, 0x02, //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
        0x85, 0x85, //   Report ID (133)
        0x09, 0x2D, //   Usage (0x2D)
        0x95, 0x02, //   Report Count (2)
        0xB1, 0x02, //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
        0x85, 0xA0, //   Report ID (160)
        0x09, 0x2E, //   Usage (0x2E)
        0x95, 0x01, //   Report Count (1)
        0xB1, 0x02, //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
        0x85, 0xE0, //   Report ID (224)
        0x09, 0x2F, //   Usage (0x2F)
        0x95, 0x3F, //   Report Count (63)
        0xB1, 0x02, //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
        0x85, 0xF0, //   Report ID (240)
        0x09, 0x30, //   Usage (0x30)
        0x95, 0x3F, //   Report Count (63)
        0xB1, 0x02, //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
        0x85, 0xF1, //   Report ID (241)
        0x09, 0x31, //   Usage (0x31)
        0x95, 0x3F, //   Report Count (63)
        0xB1, 0x02, //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
        0x85, 0xF2, //   Report ID (242)
        0x09, 0x32, //   Usage (0x32)
        0x95, 0x0F, //   Report Count (15)
        0xB1, 0x02, //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
        0x85, 0xF4, //   Report ID (244)
        0x09, 0x35, //   Usage (0x35)
        0x95, 0x3F, //   Report Count (63)
        0xB1, 0x02, //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
        0x85, 0xF5, //   Report ID (245)
        0x09, 0x36, //   Usage (0x36)
        0x95, 0x03, //   Report Count (3)
        0xB1, 0x02, //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
        0xC0, // End Collection
    };

    struct _URB_CONTROL_DESCRIPTOR_REQUEST* pRequest = &Urb->UrbControlDescriptorRequest;

    TraceVerbose(
        TRACE_USBPDO,
        ">> >> >> _URB_CONTROL_DESCRIPTOR_REQUEST: Buffer Length %d",
        pRequest->TransferBufferLength);

    if (pRequest->TransferBufferLength >= ARRAYSIZE(DS5HidReportDescriptor))
    {
        RtlCopyMemory(pRequest->TransferBuffer, DS5HidReportDescriptor, ARRAYSIZE(DS5HidReportDescriptor));
        status = STATUS_SUCCESS;

        //
        // Notify client library that PDO is ready
        // 
        KeSetEvent(&this->_PdoBootNotificationEvent, 0, FALSE);
    }

    return status;
}

// URB_FUNCTION_SELECT_INTERFACE usb.function == 0x0001 没什么内容返回
NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::UsbSelectInterface(PURB Urb)
{
    UNREFERENCED_PARAMETER(Urb);

    return STATUS_NOT_IMPLEMENTED;
}

// GET DESCRIPTOR Response STRING
NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::UsbGetStringDescriptorType(PURB Urb)
{
    TraceVerbose(
        TRACE_USBPDO,
        "Index = %d",
        Urb->UrbControlDescriptorRequest.Index);

    switch (Urb->UrbControlDescriptorRequest.Index)
    {
    case 2: // 设备名称 只看到了 index 2，DS4那边的0和1没看到
        {
            TraceVerbose(
                TRACE_USBPDO,
                "LanguageId = 0x%X",
                Urb->UrbControlDescriptorRequest.LanguageId);

            if (Urb->UrbControlDescriptorRequest.TransferBufferLength < DS5_PRODUCT_NAME_LENGTH)
            {
                auto pDesc = static_cast<PUSB_STRING_DESCRIPTOR>(Urb->UrbControlDescriptorRequest.TransferBuffer);
                pDesc->bLength = DS5_PRODUCT_NAME_LENGTH;
                break;
            }

            // "DualSense Wireless Controller"
            UCHAR ProductString[DS5_PRODUCT_NAME_LENGTH] =
            {
                0x3C, 0x03, 0x44, 0x0, 0x75, 0x0, 0x61, 0x0,
                0x6c, 0x0, 0x53, 0x0, 0x65, 0x0, 0x6e, 0x0,
                0x73, 0x0, 0x65, 0x0, 0x20, 0x0, 0x57, 0x0,
                0x69, 0x0, 0x72, 0x0, 0x65, 0x0, 0x6c, 0x0,
                0x65, 0x0, 0x73, 0x0, 0x73, 0x0, 0x20, 0x0,
                0x43, 0x0, 0x6f, 0x0, 0x6e, 0x0, 0x74, 0x0,
                0x72, 0x0, 0x6f, 0x0, 0x6c, 0x0, 0x6c, 0x0,
                0x65, 0x0, 0x72, 0x0
            };

            Urb->UrbControlDescriptorRequest.TransferBufferLength = DS5_PRODUCT_NAME_LENGTH;
            RtlCopyBytes(Urb->UrbControlDescriptorRequest.TransferBuffer, ProductString, DS5_PRODUCT_NAME_LENGTH);

            break;
        }
    default:
        break;
    }

    return STATUS_SUCCESS;
}

// URB Function: URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER (0x0009) usb.function == 0x0009
// 这里也处理 URB_INTERRUPT in？那前面创建的定时任务是？
// 处理的是 USB 栈对这个虚拟设备发起的 “Bulk/Interrupt 传输请求
NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::UsbBulkOrInterruptTransfer(_URB_BULK_OR_INTERRUPT_TRANSFER* pTransfer, WDFREQUEST Request)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDFREQUEST notifyRequest;

	// Data coming FROM us TO higher driver
	if (pTransfer->TransferFlags & USBD_TRANSFER_DIRECTION_IN
		&& pTransfer->PipeHandle == reinterpret_cast<USBD_PIPE_HANDLE>(0xFFFF0084))
	    // Endpoint: 0x84, Direction: IN 
	{
	    // 主机从设备读信息
		TraceVerbose(
			TRACE_USBPDO,
			">> >> >> Incoming request, queuing...");

		/* This request is sent periodically and relies on data the "feeder"
		   has to supply, so we queue this request and return with STATUS_PENDING.
		   The request gets completed as soon as the "feeder" sent an update. */
		status = WdfRequestForwardToIoQueue(Request, this->_PendingUsbInRequests);

		return (NT_SUCCESS(status)) ? STATUS_PENDING : status;
	}
    
    // 应用 -> 用户态
    
    // 主机从设备写信息

    // 收到来自游戏/应用的指令，传递给用户态处理
    // usb.function == 0x0009 && usb.endpoint_address != 0x84
    
	// Store relevant bytes of buffer in PDO context
    // 截取马达LED等数据？但是偏移量对不上啊，如果不带 URB Header也要偏移16 bytes啊
	RtlCopyBytes(&this->_OutputReport,
		static_cast<PUCHAR>(pTransfer->TransferBuffer) + DS5_OUTPUT_BUFFER_OFFSET,
		DS5_OUTPUT_BUFFER_LENGTH);

    
    // AwaitOutputCache 包装更完整的数据
	this->_AwaitOutputCache.Size = sizeof(DS5_AWAIT_OUTPUT);
	this->_AwaitOutputCache.SerialNo = this->_SerialNo;
	RtlCopyMemory(
		this->_AwaitOutputCache.Report.Buffer,
		pTransfer->TransferBuffer,
		pTransfer->TransferBufferLength <= sizeof(DS5_OUTPUT_BUFFER)
		? pTransfer->TransferBufferLength
		: sizeof(DS4_OUTPUT_BUFFER)
	);

	DumpAsHex("!! XUSB_REQUEST_NOTIFICATION",
		&this->_AwaitOutputCache,
		sizeof(DS5_AWAIT_OUTPUT)
	);

    // 向用户态广播，发送数据
	if (!NT_SUCCESS(status = DMF_NotifyUserWithRequestMultiple_DataBroadcast(
		this->_OutputReportNotify,
		&this->_AwaitOutputCache,
		sizeof(DS5_AWAIT_OUTPUT),
		STATUS_SUCCESS
	)))
	{
		TraceError(
			TRACE_USBPDO,
			"DMF_NotifyUserWithRequestMultiple_DataBroadcast failed with status %!STATUS!",
			status
		);
	}

    // 处理用户态发起的“等待事件”请求 好像是为了性能优化？只发送精简的数据
	if (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(
		this->_PendingNotificationRequests,
		&notifyRequest)))
	{
	    // 用户态有人要 直接返回
		PDS5_REQUEST_NOTIFICATION notify = nullptr;

		status = WdfRequestRetrieveOutputBuffer(
			notifyRequest,
			sizeof(DS5_REQUEST_NOTIFICATION),
			reinterpret_cast<PVOID*>(&notify),
			nullptr
		);

		if (NT_SUCCESS(status))
		{
			// Assign values to output buffer
			notify->Size = sizeof(DS5_REQUEST_NOTIFICATION);
			notify->SerialNo = this->_SerialNo;
			notify->Report = this->_OutputReport;

			DumpAsHex("!! XUSB_REQUEST_NOTIFICATION",
				notify,
				sizeof(DS5_REQUEST_NOTIFICATION)
			);

			WdfRequestCompleteWithInformation(notifyRequest, status, notify->Size);
		}
		else
		{
			TraceError(
				TRACE_USBPDO,
				"WdfRequestRetrieveOutputBuffer failed with status %!STATUS!",
				status);
		}
	}
	else
	{
	    // 用户态没人要数据，把数据缓存起来
		PVOID clientBuffer, contextBuffer;

		if (NT_SUCCESS(DMF_BufferQueue_Fetch(
			this->_UsbInterruptOutBufferQueue,
			&clientBuffer,
			&contextBuffer
		)))
		{
			RtlCopyMemory(
				clientBuffer,
				&this->_OutputReport,
				DS5_OUTPUT_BUFFER_LENGTH
			);

			*static_cast<size_t*>(contextBuffer) = DS5_OUTPUT_BUFFER_LENGTH;

			TraceVerbose(TRACE_USBPDO, "Queued %Iu bytes", DS5_OUTPUT_BUFFER_LENGTH);

			DMF_BufferQueue_Enqueue(this->_UsbInterruptOutBufferQueue, clientBuffer);
		}
	}

	return status;
}


NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::UsbControlTransfer(PURB Urb)
{
    NTSTATUS status;

    switch (Urb->UrbControlTransfer.SetupPacket[6])
    {
    case 0x14:
        //
        // This is some weird USB 1.0 condition and _must fail_
        // 
        Urb->UrbControlTransfer.Hdr.Status = USBD_STATUS_STALL_PID;
        status = STATUS_UNSUCCESSFUL;
        break;
    case 0x08:
        //
        // This is some weird USB 1.0 condition and _must fail_
        // 
        Urb->UrbControlTransfer.Hdr.Status = USBD_STATUS_STALL_PID;
        status = STATUS_UNSUCCESSFUL;
        break;
    default:
        status = STATUS_SUCCESS;
        break;
    }

    return status;
}


void ViGEm::Bus::Targets::EmulationTargetDS5::AbortPipe()
{
    // Higher driver shutting down, emptying PDOs queues
    WdfTimerStop(this->_PendingUsbInRequestsTimer, TRUE);
}

// 当设备有状态更新，立即更新 _Report
NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::SubmitReportImpl(PVOID NewReport)
{
    NTSTATUS				status;
    WDFREQUEST				usbRequest;

    /*
     * The logic here is unusual to keep backwards compatibility with the
     * original API that didn't allow submitting the full report.
     */

    status = WdfIoQueueRetrieveNextRequest(this->_PendingUsbInRequests, &usbRequest);

    if (!NT_SUCCESS(status))
        return status;

    // Get pending IRP
    PIRP pendingIrp = WdfRequestWdmGetIrp(usbRequest);

    // Get USB request block
    const auto urb = static_cast<PURB>(URB_FROM_IRP(pendingIrp));

    // Get transfer buffer
    const auto buffer = static_cast<PUCHAR>(urb->UrbBulkOrInterruptTransfer.TransferBuffer);

    // Set correct buffer size
    urb->UrbBulkOrInterruptTransfer.TransferBufferLength = DS5_REPORT_SIZE;

    // Cast to expected struct
    const auto pSubmit = static_cast<PDS4_SUBMIT_REPORT>(NewReport);

    /*
     * Copy report to cache and transfer buffer
     * Skip first byte as it contains the never changing report ID
     */

    //
    // "Old" API which only allows to update partial report
    // 
    if (pSubmit->Size == sizeof(DS5_SUBMIT_REPORT))
    {
        TraceVerbose(TRACE_DS5, "Received DS4_SUBMIT_REPORT update");

        RtlCopyBytes(
            &this->_Report[1],
            &(static_cast<PDS5_SUBMIT_REPORT>(NewReport))->Report,
            sizeof((static_cast<PDS5_SUBMIT_REPORT>(NewReport))->Report)
        );
    }

    //
    // "Extended" API allowing complete report update
    // 
    if (pSubmit->Size == sizeof(DS5_SUBMIT_REPORT_EX))
    {
        TraceVerbose(TRACE_DS5, "Received DS4_SUBMIT_REPORT_EX update");

        RtlCopyBytes(
            &this->_Report[1],
            &(static_cast<PDS5_SUBMIT_REPORT_EX>(NewReport))->Report,
            sizeof((static_cast<PDS5_SUBMIT_REPORT_EX>(NewReport))->Report)
        );
    }

    if (buffer)
        RtlCopyBytes(buffer, this->_Report, DS5_REPORT_SIZE);

    // Complete pending request
    WdfRequestComplete(usbRequest, status);

    return status;
}

void ViGEm::Bus::Targets::EmulationTargetDS5::ProcessPendingNotification(WDFQUEUE Queue)
{
    NTSTATUS status;
    WDFREQUEST request;
    PVOID clientBuffer, contextBuffer;
    PDS5_REQUEST_NOTIFICATION notify = nullptr;

    FuncEntry(TRACE_DS5);

    //
    // Loop through and drain all queued requests until buffer is empty
    // 
    while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Queue, &request)))
    {
        status = DMF_BufferQueue_Dequeue(
            this->_UsbInterruptOutBufferQueue,
            &clientBuffer,
            &contextBuffer
        );

        //
        // Shouldn't happen, but if so, error out
        // 
        if (!NT_SUCCESS(status))
        {
            //
            // Don't requeue request as we maya be out of order now
            // 
            WdfRequestComplete(request, status);
            continue;
        }

        if (NT_SUCCESS(WdfRequestRetrieveOutputBuffer(
            request,
            sizeof(DS5_REQUEST_NOTIFICATION),
            reinterpret_cast<PVOID*>(&notify),
            nullptr
        )))
        {
            // 
            // Assign values to output buffer
            // 
            notify->Size = sizeof(DS5_REQUEST_NOTIFICATION);
            notify->SerialNo = this->_SerialNo;
            notify->Report = *static_cast<PDS5_OUTPUT_REPORT>(clientBuffer);

            DumpAsHex("!! XUSB_REQUEST_NOTIFICATION",
                notify,
                sizeof(DS5_REQUEST_NOTIFICATION)
            );

            WdfRequestCompleteWithInformation(request, status, notify->Size);
        }

        DMF_BufferQueue_Reuse(this->_UsbInterruptOutBufferQueue, clientBuffer);

        //
        // If no more buffer to process, exit loop and await next callback
        // 
        if (DMF_BufferQueue_Count(this->_UsbInterruptOutBufferQueue) == 0)
        {
            break;
        }
    }

    TraceVerbose(TRACE_USBPDO, "%!FUNC! Exit");
}

VOID ViGEm::Bus::Targets::EmulationTargetDS5::DmfDeviceModulesAdd(_In_ PDMFMODULE_INIT DmfModuleInit)
{
    UNREFERENCED_PARAMETER(DmfModuleInit);
}

// 前面 PdoInitContext 创建了个定时器，似乎用于处理URB_INTERRUPT in？
// 定期把数据返回，数据不一定是最新的
VOID ViGEm::Bus::Targets::EmulationTargetDS5::PendingUsbRequestsTimerFunc(
    _In_ WDFTIMER Timer
)
{
    const auto ctx = reinterpret_cast<EmulationTargetDS5*>(Core::EmulationTargetPdoGetContext(
        WdfTimerGetParentObject(Timer))->Target);

    WDFREQUEST usbRequest;

    FuncEntry(TRACE_DS5);

    // Get pending USB request
    const auto status = WdfIoQueueRetrieveNextRequest(ctx->_PendingUsbInRequests, &usbRequest);

    if (NT_SUCCESS(status))
    {
        // Get pending IRP
        const auto pendingIrp = WdfRequestWdmGetIrp(usbRequest);

        const auto irpStack = IoGetCurrentIrpStackLocation(pendingIrp);

        // Get USB request block
        const auto urb = static_cast<PURB>(irpStack->Parameters.Others.Argument1);

        // Get transfer buffer
        const auto buffer = static_cast<PUCHAR>(urb->UrbBulkOrInterruptTransfer.TransferBuffer);

        // Set buffer length to report size
        urb->UrbBulkOrInterruptTransfer.TransferBufferLength = DS5_REPORT_SIZE;

        // Copy cached report to transfer buffer 
        if (buffer)
            RtlCopyBytes(buffer, ctx->_Report, DS5_REPORT_SIZE);

        // Complete pending request
        WdfRequestComplete(usbRequest, status);
    }

    TraceVerbose(TRACE_DS5, "%!FUNC! Exit with status %!STATUS!", status);
}

VOID ViGEm::Bus::Targets::EmulationTargetDS5::GenerateRandomMacAddress(PMAC2_ADDRESS Address)
{
    // Vendor "C0:13:37"
    Address->Vendor0 = 0xC0;
    Address->Vendor1 = 0x13;
    Address->Vendor2 = 0x37;

    ULONG seed = KeQueryPerformanceCounter(NULL).LowPart;

    Address->Nic0 = RtlRandomEx(&seed) % 0xFF;
    Address->Nic1 = RtlRandomEx(&seed) % 0xFF;
    Address->Nic2 = RtlRandomEx(&seed) % 0xFF;
}

VOID ViGEm::Bus::Targets::EmulationTargetDS5::SetOutputReportNotifyModule(DMFMODULE Module)
{
    this->_OutputReportNotify = Module;
}
