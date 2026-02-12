/*
* Virtual Gamepad Emulation Framework - Windows kernel-mode bus driver
*
* BSD 3-Clause License
*
* Copyright (c) 2018-2022, Nefarius Software Solutions e.U. and Contributors
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this
*    list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* 3. Neither the name of the copyright holder nor the names of its
*    contributors may be used to endorse or promote products derived from
*    this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


#include <ntifs.h>
#include "Ds5Pdo.hpp"
#include "trace.h"
#include "Ds5Pdo.tmh"
#define NTSTRSAFE_LIB
#include <ntstrsafe.h>

#include <initguid.h>
#include <usbbusif.h>

PCWSTR ViGEm::Bus::Targets::EmulationTargetDS5::_deviceDescription = L"Virtual DualSense 5 Controller";

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

NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::PdoPrepareDevice(PWDFDEVICE_INIT DeviceInit,
                                                                   PUNICODE_STRING DeviceId,
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
    RtlUnicodeStringPrintf(&buffer, L"USB\\VID_%04X&PID_%04X&REV_0100",
                           this->_VendorId, this->_ProductId);

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

    RtlUnicodeStringPrintf(&buffer, L"USB\\VID_%04X&PID_%04X",
                           this->_VendorId, this->_ProductId);

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
    // Important: make Windows treat this as a composite USB device (usbccgp.sys)
    RtlUnicodeStringInit(&buffer, L"USB\\COMPOSITE");

    status = WdfPdoInitAddCompatibleID(DeviceInit, &buffer);
    if (!NT_SUCCESS(status))
    {
        TraceError(
            TRACE_DS5,
            "WdfPdoInitAddCompatibleID (USB\\COMPOSITE) failed with status %!STATUS!",
            status);
        return status;
    }

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
    NTSTATUS status;
    WDF_QUERY_INTERFACE_CONFIG ifaceCfg;

    //
    // Expose USB_BUS_INTERFACE_USBDI_GUID (needed by USBAudio stack)
    // 使用 USB 2.0 (V1) 接口
    //
    USB_BUS_INTERFACE_USBDI_V1 ds5Interface;
    RtlZeroMemory(&ds5Interface, sizeof(ds5Interface));

    ds5Interface.Size = sizeof(USB_BUS_INTERFACE_USBDI_V1);
    ds5Interface.Version = USB_BUSIF_USBDI_VERSION_1;
    // BusContext 应该是 this（PDO对象），而不是 WDF Device Handle
    ds5Interface.BusContext = static_cast<PVOID>(this);

    ds5Interface.InterfaceReference = WdfDeviceInterfaceReferenceNoOp;
    ds5Interface.InterfaceDereference = WdfDeviceInterfaceDereferenceNoOp;

    //
    // Do NOT register SubmitIsoOutUrb callback.
    // Setting it to NULL forces USBAudio to submit ISO OUT URBs
    // through the normal IOCTL path (URB_FUNCTION_ISOCH_TRANSFER),
    // which allows us to delay completion and throttle the audio rate.
    // Without this, USBAudio calls the callback synchronously and
    // completes URBs instantly, causing the audio to play too fast.
    //
    ds5Interface.SubmitIsoOutUrb = NULL;
    ds5Interface.GetUSBDIVersion = UsbInterfaceGetUSBDIVersion;
    ds5Interface.QueryBusTime = UsbInterfaceQueryBusTime;
    ds5Interface.QueryBusInformation = UsbInterfaceQueryBusInformation;
    ds5Interface.IsDeviceHighSpeed = UsbInterfaceIsDeviceHighSpeed;

    WDF_QUERY_INTERFACE_CONFIG_INIT(
        &ifaceCfg,
        reinterpret_cast<PINTERFACE>(&ds5Interface),
        &USB_BUS_INTERFACE_USBDI_GUID,
        nullptr
    );

    status = WdfDeviceAddQueryInterface(this->_PdoDevice, &ifaceCfg);
    if (!NT_SUCCESS(status))
    {
        TraceError(
            TRACE_DS5,
            "WdfDeviceAddQueryInterface failed with status %!STATUS!",
            status);
        return status;
    }

    // Set default HID input report (everything zero`d)
    UCHAR DefaultHidReport[DS5_REPORT_SIZE] =
    {
        0x01, 0x7f, 0x7d, 0x7f, 0x7e, 0x00, 0x00, 0xa7,
        0x08, 0x00, 0x00, 0x00, 0x52, 0x43, 0x30, 0x41,
        0x01, 0x00, 0x0e, 0x00, 0xef, 0xff, 0x03, 0x03,
        0x7b, 0x1b, 0x18, 0xf0, 0xcc, 0x9c, 0x60, 0x00,
        0xfc, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
        0x00, 0x00, 0x09, 0x09, 0x00, 0x00, 0x00, 0x00,
        0x00, 0xa7, 0xad, 0x60, 0x00, 0x29, 0x18, 0x00,
        0x53, 0x9f, 0x28, 0x35, 0xa5, 0xa8, 0x0c, 0x8b
    };

    // Initialize HID reports to defaults
    RtlCopyBytes(this->_Report, DefaultHidReport, DS5_REPORT_SIZE);
    RtlZeroMemory(&this->_OutputReport, sizeof(DS5_OUTPUT_REPORT));

    // Start pending IRP queue flush timer
    WdfTimerStart(this->_PendingUsbInRequestsTimer, DS5_QUEUE_FLUSH_PERIOD);

    // Start ISO OUT completion timer
    WdfTimerStart(this->_PendingIsoOutTimer, WDF_REL_TIMEOUT_IN_MS(DS5_ISO_OUT_COMPLETION_PERIOD_MS));

    return STATUS_SUCCESS;
}

NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::PdoInitContext()
{
    NTSTATUS status;

    // Initialize periodic timer
    WDF_TIMER_CONFIG timerConfig;
    WDF_TIMER_CONFIG_INIT_PERIODIC(
        &timerConfig,
        PendingUsbRequestsTimerFunc,
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

        //
        // Create manual dispatch queue for pending ISO OUT requests.
        // Requests are held here and completed by a periodic timer
        // to simulate real USB isochronous transfer timing.
        //
        {
            WDF_IO_QUEUE_CONFIG isoQueueConfig;
            WDF_IO_QUEUE_CONFIG_INIT(&isoQueueConfig, WdfIoQueueDispatchManual);

            if (!NT_SUCCESS(status = WdfIoQueueCreate(
                this->_PdoDevice,
                &isoQueueConfig,
                WDF_NO_OBJECT_ATTRIBUTES,
                &this->_PendingIsoOutRequests
            )))
            {
                TraceError(
                    TRACE_DS5,
                    "WdfIoQueueCreate (IsoOut) failed with status %!STATUS!",
                    status);
                break;
            }
        }

        //
        // Create periodic timer for ISO OUT completion.
        // Fires every DS5_ISO_OUT_COMPLETION_PERIOD_MS to dequeue
        // and complete one pending ISO OUT request, throttling the rate.
        //
        {
            WDF_TIMER_CONFIG isoTimerConfig;
            WDF_TIMER_CONFIG_INIT_PERIODIC(
                &isoTimerConfig,
                PendingIsoOutTimerFunc,
                DS5_ISO_OUT_COMPLETION_PERIOD_MS
            );

            WDF_OBJECT_ATTRIBUTES isoTimerAttribs;
            WDF_OBJECT_ATTRIBUTES_INIT(&isoTimerAttribs);
            isoTimerAttribs.ParentObject = this->_PdoDevice;

            if (!NT_SUCCESS(status = WdfTimerCreate(
                &isoTimerConfig,
                &isoTimerAttribs,
                &this->_PendingIsoOutTimer
            )))
            {
                TraceError(
                    TRACE_DS5,
                    "WdfTimerCreate (IsoOut) failed with status %!STATUS!",
                    status);
                break;
            }
        }

        // Load/generate MAC address

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

        RtlUnicodeStringInit(&keyName, L"DualSense");

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
            sizeof(MAC_ADDRESS),
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
                sizeof(MAC_ADDRESS),
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

VOID ViGEm::Bus::Targets::EmulationTargetDS5::GetConfigurationDescriptorType(PUCHAR Buffer, ULONG Length)
{
    UCHAR Ds5DescriptorData[DS5_DESCRIPTOR_SIZE] =
    {
        // --- CONFIGURATION DESCRIPTOR ---
        0x09, // bLength
        0x02, // bDescriptorType (CONFIGURATION)
        0xE3, 0x00, // wTotalLength: 227
        0x04, // bNumInterfaces: 4
        0x01, // bConfigurationValue: 1
        0x00, // iConfiguration: 0
        0xC0, // bmAttributes: SELF-POWERED, NO REMOTE-WAKEUP
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
        0x02, // bInCollection: 2 streaming interfaces
        0x01, // baInterfaceNr(1): Interface 1
        0x02, // baInterfaceNr(2): Interface 2

        // Input Terminal Descriptor (Terminal ID 1: USB Streaming → Output to Speaker)
        0x0C, // bLength: 12
        0x24, // bDescriptorType: CS_INTERFACE
        0x02, // bDescriptorSubtype: Input Terminal
        0x01, // bTerminalID: 1
        0x01, 0x01, // wTerminalType: USB Streaming (0x0101)
        0x06, // bAssocTerminal: 6 (paired with USB OUT terminal)
        0x04, // bNrChannels: 4
        0x33, 0x00, // wChannelConfig: L/R Front + L/R Surround (0x0033)
        0x00, // iChannelNames: 0
        0x00, // iTerminal: 0

        // Feature Unit Descriptor (Unit ID 2 ← from Terminal 1)
        0x0C, // bLength: 12
        0x24, // bDescriptorType: CS_INTERFACE
        0x06, // bDescriptorSubtype: Feature Unit
        0x02, // bUnitID: 2
        0x01, // bSourceID: 1
        0x01, // bControlSize: 1 byte per control
        0x03, // bmaControls[0]: Master – Mute, Volume
        0x00, 0x00, 0x00, 0x00, 0x00, // bmaControls[1..4]: No per-channel controls

        // Output Terminal Descriptor (Terminal ID 3: Speaker ← from Unit 2)
        0x09, // bLength: 9
        0x24, // bDescriptorType: CS_INTERFACE
        0x03, // bDescriptorSubtype: Output Terminal
        0x03, // bTerminalID: 3
        0x01, 0x03, // wTerminalType: Speaker (0x0301)
        0x04, // bAssocTerminal: 4 (paired with mic input)
        0x02, // bSourceID: 2 (Feature Unit)
        0x00, // iTerminal: 0

        // Input Terminal Descriptor (Terminal ID 4: Headset Mic)
        0x0C, // bLength: 12
        0x24, // bDescriptorType: CS_INTERFACE
        0x02, // bDescriptorSubtype: Input Terminal
        0x04, // bTerminalID: 4
        0x02, 0x04, // wTerminalType: Headset (0x0402)
        0x03, // bAssocTerminal: 3 (paired with speaker)
        0x02, // bNrChannels: 2
        0x03, 0x00, // wChannelConfig: L/R Front (0x0003)
        0x00, // iChannelNames: 0
        0x00, // iTerminal: 0

        // Feature Unit Descriptor (Unit ID 5 ← from Terminal 4)
        0x09, // bLength: 9
        0x24, // bDescriptorType: CS_INTERFACE
        0x06, // bDescriptorSubtype: Feature Unit
        0x05, // bUnitID: 5
        0x04, // bSourceID: 4
        0x01, // bControlSize: 1
        0x03, // bmaControls[0]: Master – Mute, Volume
        0x00, // bmaControls[1]: Ch1 – no controls
        0x00, // iFeature: 0

        // Output Terminal Descriptor (Terminal ID 6: USB Streaming ← from Unit 5)
        0x09, // bLength: 9
        0x24, // bDescriptorType: CS_INTERFACE
        0x03, // bDescriptorSubtype: Output Terminal
        0x06, // bTerminalID: 6
        0x01, 0x01, // wTerminalType: USB Streaming (0x0101)
        0x01, // bAssocTerminal: 1
        0x05, // bSourceID: 5
        0x00, // iTerminal: 0

        // --- INTERFACE DESCRIPTOR (1.0): Audio Streaming (OUT - Alternate 0) ---
        0x09, // bLength
        0x04, // bDescriptorType (INTERFACE)
        0x01, // bInterfaceNumber: 1
        0x00, // bAlternateSetting: 0
        0x00, // bNumEndpoints: 0
        0x01, // bInterfaceClass: Audio
        0x02, // bInterfaceSubClass: Audio Streaming
        0x00, // bInterfaceProtocol
        0x00, // iInterface

        // --- INTERFACE DESCRIPTOR (1.1): Audio Streaming (OUT - Alternate 1) ---
        0x09, // bLength
        0x04, // bDescriptorType (INTERFACE)
        0x01, // bInterfaceNumber: 1
        0x01, // bAlternateSetting: 1
        0x01, // bNumEndpoints: 1
        0x01, // bInterfaceClass: Audio
        0x02, // bInterfaceSubClass: Audio Streaming
        0x00, // bInterfaceProtocol
        0x00, // iInterface

        // AS General Descriptor (for Interface 1.1)
        0x07, // bLength: 7
        0x24, // bDescriptorType: CS_INTERFACE
        0x01, // bDescriptorSubtype: AS_GENERAL
        0x01, // bTerminalLink: connected to Terminal ID 1
        0x01, // bDelay: 1 frame
        0x01, 0x00, // wFormatTag: PCM (0x0001)

        // Format Type Descriptor (4-channel, 16-bit, 48kHz)
        0x0B, // bLength: 11
        0x24, // bDescriptorType: CS_INTERFACE
        0x02, // bDescriptorSubtype: FORMAT_TYPE
        0x01, // bFormatType: TYPE_I
        0x04, // bNrChannels: 4
        0x02, // bSubframeSize: 2 bytes/sample
        0x10, // bBitResolution: 16 bits
        0x01, // bSamFreqType: 1 discrete frequency
        0x80, 0xBB, 0x00, // tSamFreq: 48000 Hz (0x00BB80)

        // Endpoint Descriptor (Audio OUT: EP1)
        0x09, // bLength
        0x05, // bDescriptorType (ENDPOINT)
        0x01, // bEndpointAddress: OUT EP1
        0x09, // bmAttributes: Isochronous, Adaptive
        0x88, 0x01, // wMaxPacketSize: 392 bytes
        0x04, // bInterval: 4 (1/(2^(4-1)) ms ≈ 125 µs/frame)
        0x00, // bRefresh
        0x00, // bSynchAddress

        // Class-specific Audio Streaming Endpoint Descriptor (EP1)
        0x07, // bLength
        0x25, // bDescriptorType: CS_ENDPOINT
        0x01, // bDescriptorSubtype: GENERAL
        0x00, // Attributes: No pitch/sampling freq control
        0x00, // Lock Delay Units: Undefined
        0x00, 0x00, // Lock Delay: 0

        // --- INTERFACE DESCRIPTOR (2.0): Audio Streaming IN (Alternate 0) ---
        0x09, // bLength
        0x04, // bDescriptorType (INTERFACE)
        0x02, // bInterfaceNumber: 2
        0x00, // bAlternateSetting: 0
        0x00, // bNumEndpoints: 0
        0x01, // bInterfaceClass: Audio
        0x02, // bInterfaceSubClass: Audio Streaming
        0x00, // bInterfaceProtocol
        0x00, // iInterface

        // --- INTERFACE DESCRIPTOR (2.1): Audio Streaming IN (Alternate 1) ---
        0x09, // bLength
        0x04, // bDescriptorType (INTERFACE)
        0x02, // bInterfaceNumber: 2
        0x01, // bAlternateSetting: 1
        0x01, // bNumEndpoints: 1
        0x01, // bInterfaceClass: Audio
        0x02, // bInterfaceSubClass: Audio Streaming
        0x00, // bInterfaceProtocol
        0x00, // iInterface

        // AS General Descriptor (for Interface 2.1)
        0x07, // bLength: 7
        0x24, // bDescriptorType: CS_INTERFACE
        0x01, // bDescriptorSubtype: AS_GENERAL
        0x06, // bTerminalLink: connected to Terminal ID 6
        0x01, // bDelay: 1 frame
        0x01, 0x00, // wFormatTag: PCM (0x0001)

        // Format Type Descriptor (2-channel, 16-bit, 48kHz)
        0x0B, // bLength: 11
        0x24, // bDescriptorType: CS_INTERFACE
        0x02, // bDescriptorSubtype: FORMAT_TYPE
        0x01, // bFormatType: TYPE_I
        0x02, // bNrChannels: 2
        0x02, // bSubframeSize: 2
        0x10, // bBitResolution: 16
        0x01, // bSamFreqType: 1
        0x80, 0xBB, 0x00, // tSamFreq: 48000 Hz

        // Endpoint Descriptor (Audio IN: EP2)
        0x09, // bLength
        0x05, // bDescriptorType (ENDPOINT)
        0x82, // bEndpointAddress: IN EP2
        0x05, // bmAttributes: Isochronous, Asynchronous
        0xC4, 0x00, // wMaxPacketSize: 196 bytes
        0x04, // bInterval: 4
        0x00, // bRefresh
        0x00, // bSynchAddress

        // Class-specific Audio Streaming Endpoint Descriptor (EP2)
        0x07, // bLength
        0x25, // bDescriptorType: CS_ENDPOINT
        0x01, // bDescriptorSubtype: GENERAL
        0x00, // Attributes: No controls
        0x00, // Lock Delay Units
        0x00, 0x00, // Lock Delay

        // --- INTERFACE DESCRIPTOR (3.0): HID (DualSense 5 Gamepad + Touchpad) ---
        0x09, // bLength
        0x04, // bDescriptorType (INTERFACE)
        0x03, // bInterfaceNumber: 3
        0x00, // bAlternateSetting: 0
        0x02, // bNumEndpoints: 2 (IN + OUT)
        0x03, // bInterfaceClass: HID
        0x00, // bInterfaceSubClass: None
        0x00, // bInterfaceProtocol: None
        0x00, // iInterface

        // HID Descriptor
        0x09, // bLength: 9
        0x21, // bDescriptorType (HID)
        0x11, 0x01, // bcdHID: 1.11
        0x00, // bCountryCode: Not localized
        0x01, // bNumDescriptors: 1 report descriptor
        0x22, // bDescriptorType: Report
        0x21, 0x01, // wDescriptorLength: 289 (0x0121)

        // Endpoint Descriptor (HID IN: EP4)
        0x07, // bLength
        0x05, // bDescriptorType (ENDPOINT)
        0x84, // bEndpointAddress: IN EP4
        0x03, // bmAttributes: Interrupt
        0x40, 0x00, // wMaxPacketSize: 64
        0x06, // bInterval: 6 (polling every 8ms)

        // Endpoint Descriptor (HID OUT: EP3)
        0x07, // bLength
        0x05, // bDescriptorType (ENDPOINT)
        0x03, // bEndpointAddress: OUT EP3
        0x03, // bmAttributes: Interrupt
        0x40, 0x00, // wMaxPacketSize: 64
        0x06, // bInterval: 6
    };

    RtlCopyBytes(Buffer, Ds5DescriptorData, Length);
}

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

NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::SelectConfiguration(PURB Urb)
{
    if (Urb->UrbHeader.Length < DS5_CONFIGURATION_SIZE)
    {
        TraceEvents(TRACE_LEVEL_WARNING,
                    TRACE_USBPDO,
                    ">> >> >> URB_FUNCTION_SELECT_CONFIGURATION: Invalid ConfigurationDescriptor");
        return STATUS_INVALID_PARAMETER;
    }

    PUSBD_INTERFACE_INFORMATION pInfo = &Urb->UrbSelectConfiguration.Interface;

    // INTERFACE 0: Audio Control (no endpoints)
    TraceVerbose(
        TRACE_DS5,
        ">> >> >> URB_FUNCTION_SELECT_CONFIGURATION: Length %d, Interface %d, Alternate %d, Pipes %d",
        static_cast<int>(pInfo->Length),
        static_cast<int>(pInfo->InterfaceNumber),
        static_cast<int>(pInfo->AlternateSetting),
        pInfo->NumberOfPipes);

    pInfo->Class = 0x01; // Audio
    pInfo->SubClass = 0x01; // Audio Control
    pInfo->Protocol = 0x00;
    pInfo->InterfaceHandle = reinterpret_cast<USBD_INTERFACE_HANDLE>(0xFFFF0000);
    // No pipes for Audio Control interface

    pInfo = (PUSBD_INTERFACE_INFORMATION)((PCHAR)pInfo + pInfo->Length);

    // INTERFACE 1: Audio Streaming OUT (Alternate 0 - no endpoints, idle state)
    TraceVerbose(
        TRACE_DS5,
        ">> >> >> URB_FUNCTION_SELECT_CONFIGURATION: Length %d, Interface %d, Alternate %d, Pipes %d",
        static_cast<int>(pInfo->Length),
        static_cast<int>(pInfo->InterfaceNumber),
        static_cast<int>(pInfo->AlternateSetting),
        pInfo->NumberOfPipes);

    pInfo->Class = 0x01; // Audio
    pInfo->SubClass = 0x02; // Audio Streaming
    pInfo->Protocol = 0x00;
    pInfo->InterfaceHandle = reinterpret_cast<USBD_INTERFACE_HANDLE>(0xFFFF0001);
    // Alternate 0 has no endpoints (will switch to Alt 1 via SELECT_INTERFACE when needed)

    pInfo = (PUSBD_INTERFACE_INFORMATION)((PCHAR)pInfo + pInfo->Length);

    // INTERFACE 2: Audio Streaming IN (Alternate 0 - no endpoints, idle state)
    TraceVerbose(
        TRACE_DS5,
        ">> >> >> URB_FUNCTION_SELECT_CONFIGURATION: Length %d, Interface %d, Alternate %d, Pipes %d",
        static_cast<int>(pInfo->Length),
        static_cast<int>(pInfo->InterfaceNumber),
        static_cast<int>(pInfo->AlternateSetting),
        pInfo->NumberOfPipes);

    pInfo->Class = 0x01; // Audio
    pInfo->SubClass = 0x02; // Audio Streaming
    pInfo->Protocol = 0x00;
    pInfo->InterfaceHandle = reinterpret_cast<USBD_INTERFACE_HANDLE>(0xFFFF0002);
    // Alternate 0 has no endpoints (will switch to Alt 1 via SELECT_INTERFACE when needed)

    pInfo = (PUSBD_INTERFACE_INFORMATION)((PCHAR)pInfo + pInfo->Length);

    // INTERFACE 3: HID (2 endpoints - IN and OUT)
    TraceVerbose(
        TRACE_DS5,
        ">> >> >> URB_FUNCTION_SELECT_CONFIGURATION: Length %d, Interface %d, Alternate %d, Pipes %d",
        static_cast<int>(pInfo->Length),
        static_cast<int>(pInfo->InterfaceNumber),
        static_cast<int>(pInfo->AlternateSetting),
        pInfo->NumberOfPipes);

    pInfo->Class = 0x03; // HID
    pInfo->SubClass = 0x00;
    pInfo->Protocol = 0x00;

    pInfo->InterfaceHandle = reinterpret_cast<USBD_INTERFACE_HANDLE>(0xFFFF0000);

    pInfo->Pipes[0].MaximumTransferSize = 0x00400000;
    pInfo->Pipes[0].MaximumPacketSize = 0x40;
    pInfo->Pipes[0].EndpointAddress = 0x84;
    pInfo->Pipes[0].Interval = 0x06;
    pInfo->Pipes[0].PipeType = static_cast<USBD_PIPE_TYPE>(0x03);
    pInfo->Pipes[0].PipeHandle = reinterpret_cast<USBD_PIPE_HANDLE>(0xFFFF0084);
    pInfo->Pipes[0].PipeFlags = 0x00;

    pInfo->Pipes[1].MaximumTransferSize = 0x00400000;
    pInfo->Pipes[1].MaximumPacketSize = 0x40;
    pInfo->Pipes[1].EndpointAddress = 0x03;
    pInfo->Pipes[1].Interval = 0x06;
    pInfo->Pipes[1].PipeType = static_cast<USBD_PIPE_TYPE>(0x03);
    pInfo->Pipes[1].PipeHandle = reinterpret_cast<USBD_PIPE_HANDLE>(0xFFFF0003);
    pInfo->Pipes[1].PipeFlags = 0x00;

    return STATUS_SUCCESS;
}

void ViGEm::Bus::Targets::EmulationTargetDS5::AbortPipe()
{
    // Higher driver shutting down, emptying PDOs queues
    WdfTimerStop(this->_PendingUsbInRequestsTimer, TRUE);
    WdfTimerStop(this->_PendingIsoOutTimer, TRUE);

    // Drain all pending ISO OUT requests
    if (this->_PendingIsoOutRequests != nullptr)
    {
        WdfIoQueuePurgeSynchronously(this->_PendingIsoOutRequests);
    }
}

NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::UsbClassInterface(PURB Urb)
{
    struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST* pRequest = &Urb->UrbControlVendorClassRequest;

    TraceVerbose(
        TRACE_USBPDO,
        ">> >> >> URB_FUNCTION_CLASS_INTERFACE");
    TraceVerbose(TRACE_USBPDO,
                     ">> >> >> TransferFlags = 0x%X, Request = 0x%X, Value = 0x%X, Index = 0x%X, BufLen = %d, RequestTypeReservedBits = 0x%X",
                     pRequest->TransferFlags,
                     pRequest->Request,
                     pRequest->Value,
                     pRequest->Index,
                     pRequest->TransferBufferLength,
                     pRequest->RequestTypeReservedBits);

    // 输出 TransferBuffer 的内容（十六进制）
    if (pRequest->TransferBuffer != nullptr && pRequest->TransferBufferLength > 0)
    {
        ULONG dumpLength = pRequest->TransferBufferLength;
        if (dumpLength > 64)
        {
            dumpLength = 64;
        }

        CHAR bufHex[3 * 64 + 1] = { 0 };
        SIZE_T remaining = ARRAYSIZE(bufHex);
        CHAR* cursor = bufHex;
        const UCHAR* bytes = static_cast<const UCHAR*>(pRequest->TransferBuffer);

        for (ULONG i = 0; i < dumpLength; ++i)
        {
            const CHAR* sep = (i == 0) ? "" : " ";
            size_t written = 0;

            if (NT_SUCCESS(RtlStringCchPrintfA(cursor, remaining, "%s%02X", sep, bytes[i])))
            {
                RtlStringCchLengthA(cursor, remaining, &written);
                cursor += written;
                remaining -= written;
            }
            else
            {
                break;
            }
        }

        TraceVerbose(TRACE_USBPDO, ">> >> >> TransferBuffer content: %s", bufHex);
    }

    // 0x0200 是扬声器 0x0500 是麦克风
    if ((pRequest->Index == 0x0200 || pRequest->Index == 0x0500) && pRequest->Request == HID_REQUEST_SET_CUR)
    {
        UCHAR channel = get_low_bytes(pRequest->Value); // 低位 Channel Number
        UCHAR feature = get_high_bytes(pRequest->Value); // 高位 Feature Unit Control Selector

        TraceVerbose(
                    TRACE_USBPDO,
                    ">> >> >> >> SET_CUR(0x%02X): 0x%02X, Index: 0x%04X",
                    channel, feature,pRequest->Index);

        switch (feature)
        {
        case 0x01: // MUTE_CONTROL
            {
                if (pRequest->TransferBuffer != nullptr && pRequest->TransferBufferLength >= sizeof(this->_AudioMute0200))
                {
                    switch (pRequest->Index)
                    {
                    case 0x0200:
                        RtlCopyBytes(this->_AudioMute0200, pRequest->TransferBuffer, sizeof(this->_AudioMute0200));
                        break;
                    case 0x0500:
                        RtlCopyBytes(this->_AudioMute0500, pRequest->TransferBuffer, sizeof(this->_AudioMute0500));
                        break;
                    default:
                        break;
                    }
                }
                break;
            }
        case 0x02: // VOLUME_CONTROL
            {
                if (pRequest->TransferBuffer != nullptr && pRequest->TransferBufferLength >= sizeof(this->_Volume0200))
                {
                    switch (pRequest->Index)
                    {
                    case 0x0200:
                        RtlCopyBytes(this->_Volume0200, pRequest->TransferBuffer, sizeof(this->_Volume0200));
                        break;
                    case 0x0500:
                        RtlCopyBytes(this->_Volume0500, pRequest->TransferBuffer, sizeof(this->_Volume0500));
                        break;
                    default:
                        break;
                    }
                }
                break;
            }
        default:
            break;
        }
        TraceVerbose(TRACE_USBPDO, ">> >> >> >> END");

        Urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
        return STATUS_SUCCESS;
    }

    switch (pRequest->Request)
    {
    case HID_REQUEST_GET_REPORT:
        {
            UCHAR reportId = get_low_bytes(pRequest->Value); // 低位
            UCHAR reportType = get_high_bytes(pRequest->Value); // 高位

            TraceVerbose(
                TRACE_USBPDO,
                ">> >> >> >> GET_REPORT(%d): %d",
                reportType, reportId);

            switch (reportType)
            {
            case HID_REPORT_TYPE_FEATURE:
                {
                    switch (reportId)
                    {
                    case HID_REPORT_FIRMWARE_INFO_ID:
                        {
                            UCHAR Response[] =
                            {
                                0x20, 0x4a, 0x75, 0x6c, 0x20, 0x20, 0x34, 0x20,
                                0x32, 0x30, 0x32, 0x35, 0x31, 0x30, 0x3a, 0x33,
                                0x38, 0x3a, 0x34, 0x30, 0x03, 0x00, 0x0b, 0x00,
                                0x11, 0x08, 0x00, 0x00, 0x2a, 0x00, 0x10, 0x01,
                                0x00, 0x28, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x30, 0x06, 0x00, 0x00,
                                0x01, 0x00, 0x03, 0x00, 0x10, 0x10, 0x03, 0x00,
                                0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
                            };

                            pRequest->TransferBufferLength = ARRAYSIZE(Response);
                            RtlCopyBytes(pRequest->TransferBuffer, Response, ARRAYSIZE(Response));

                            break;
                        }
                    case HID_REPORT_HARDWARE_INFO_ID:
                        {
                            UCHAR Response[] =
                            {
                                0x22, 0x03, 0x00, 0x11, 0x08, 0x00, 0x00, 0x2a,
                                0x00, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0xec, 0x38, 0xe4, 0x9a, 0x3a, 0x14, 0x10,
                                0x10, 0x03, 0x00, 0x06, 0x00, 0x00, 0x00, 0x19,
                                0x00, 0x00, 0x00, 0x1d, 0x6c, 0x0e, 0x1f, 0x04,
                                0x01, 0x01, 0x03, 0x00, 0x00, 0x41, 0x50, 0x34,
                                0x4c, 0x36, 0x32, 0x36, 0x10, 0x01, 0x00, 0x03,
                                0x00, 0x20, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00
                            };

                            pRequest->TransferBufferLength = ARRAYSIZE(Response);
                            RtlCopyBytes(pRequest->TransferBuffer, Response, ARRAYSIZE(Response));

                            break;
                        }
                    case HID_REPORT_MAC_ADDRESSES_ID:
                        {
                            // Source: http://eleccelerator.com/wiki/index.php?title=DualShock_4#Class_Requests
                            UCHAR Response[] =
                            {
                                0x12, 0x8B, 0x09, 0x07, 0x6D, 0x66, 0x1C, 0x08,
                                0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
                            };

                            // Insert (auto-generated) target MAC address into response
                            RtlCopyBytes(Response + 1, &this->_TargetMacAddress, sizeof(MAC_ADDRESS));
                            // Adjust byte order
                            ReverseByteArray(Response + 1, sizeof(MAC_ADDRESS));

                            // Insert (auto-generated) host MAC address into response
                            RtlCopyBytes(Response + 10, &this->_HostMacAddress, sizeof(MAC_ADDRESS));
                            // Adjust byte order
                            ReverseByteArray(Response + 10, sizeof(MAC_ADDRESS));

                            pRequest->TransferBufferLength = ARRAYSIZE(Response);
                            RtlCopyBytes(pRequest->TransferBuffer, Response, ARRAYSIZE(Response));

                            break;
                        }
                    default:
                        break;
                    }
                    break;
                }
            default:
                break;
            }

            break;
        }
    case HID_REQUEST_SET_REPORT:
        {
            UCHAR reportId = get_low_bytes(pRequest->Value); // 低位
            UCHAR reportType = get_high_bytes(pRequest->Value); // 高位

            TraceVerbose(
                TRACE_USBPDO,
                ">> >> >> >> SET_REPORT(%d): %d",
                reportType, reportId);

            switch (reportType)
            {
            case HID_REPORT_TYPE_FEATURE:
                {
                    switch (reportId)
                    {
                    case HID_REPORT_ID_3:
                        {
                            // Source: http://eleccelerator.com/wiki/index.php?title=DualShock_4#Class_Requests
                            UCHAR Response[] =
                            {
                                0x13, 0xAC, 0x9E, 0x17, 0x94, 0x05, 0xB0, 0x56,
                                0xE8, 0x81, 0x38, 0x08, 0x06, 0x51, 0x41, 0xC0,
                                0x7F, 0x12, 0xAA, 0xD9, 0x66, 0x3C, 0xCE
                            };

                            pRequest->TransferBufferLength = ARRAYSIZE(Response);
                            RtlCopyBytes(pRequest->TransferBuffer, Response, ARRAYSIZE(Response));

                            break;
                        }
                    case HID_REPORT_ID_4:
                        {
                            // Source: http://eleccelerator.com/wiki/index.php?title=DualShock_4#Class_Requests
                            UCHAR Response[] =
                            {
                                0x14, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00
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
            default:
                break;
            }

            break;
        }
    case HID_REQUEST_GET_CUR:
        {
            UCHAR channel = get_low_bytes(pRequest->Value); // 低位 Channel Number
            UCHAR feature = get_high_bytes(pRequest->Value); // 高位 Feature Unit Control Selector

            TraceVerbose(
                        TRACE_USBPDO,
                        ">> >> >> >> GET_CUR(0x%02X): 0x%02X, Index=0x%04X",
                        channel, feature, pRequest->Index);
            switch (feature)
            {
            case 0x01: // MUTE_CONTROL 好像只在设备初始化的时候有查询？但最好后期也向用户态发起请求查询
                {
                    switch (pRequest->Index)
                    {
                    case 0x0200:
                        {
                            UCHAR Response[] =
                            {
                                this->_AudioMute0200[0]
                            };

                            pRequest->TransferBufferLength = ARRAYSIZE(Response);
                            RtlCopyBytes(pRequest->TransferBuffer, Response, ARRAYSIZE(Response));
                            break;
                        }
                    case 0x0500:
                        {
                            UCHAR Response[] =
                            {
                                this->_AudioMute0500[0]
                            };

                            pRequest->TransferBufferLength = ARRAYSIZE(Response);
                            RtlCopyBytes(pRequest->TransferBuffer, Response, ARRAYSIZE(Response));
                            break;
                        }
                    }
                    break;
                }
            case 0x02: // VOLUME_CONTROL
                {
                    switch (pRequest->Index)
                    {
                    case 0x0200: // 扬声器
                        {
                            TraceVerbose(
                                TRACE_USBPDO,
                                ">> >> >> >> Speaker Response");
                            UCHAR Response[] =
                            {
                                this->_Volume0200[0], this->_Volume0200[1]
                            };

                            pRequest->TransferBufferLength = ARRAYSIZE(Response);
                            RtlCopyBytes(pRequest->TransferBuffer, Response, ARRAYSIZE(Response));
                            break;
                        }
                    case 0x0500: // 麦克风
                        {
                            UCHAR Response[] =
                            {
                                this->_Volume0500[0], this->_Volume0500[1]
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
    case HID_REQUEST_GET_MIN:
        {
            UCHAR channel = get_low_bytes(pRequest->Value); // 低位 Channel Number
            UCHAR feature = get_high_bytes(pRequest->Value); // 高位 Feature Unit Control Selector

            TraceVerbose(
                TRACE_USBPDO,
                ">> >> >> >> GET_MIN(0x%02X): 0x%02X, Index=0x%04X",
                channel, feature, pRequest->Index);

            switch (feature)
            {
            case 0x02: // VOLUME_CONTROL
                {
                    switch (pRequest->Index)
                    {
                    case 0x0200: // 扬声器
                        {
                            TraceVerbose(
                                TRACE_USBPDO,
                                ">> >> >> >> Speaker Response");
                            UCHAR Response[] =
                            {
                                0x00, 0x9c // -100.0000 dB
                            };

                            pRequest->TransferBufferLength = ARRAYSIZE(Response);
                            RtlCopyBytes(pRequest->TransferBuffer, Response, ARRAYSIZE(Response));
                            break;
                        }
                    case 0x0500: // 麦克风
                        {
                            UCHAR Response[] =
                            {
                                0x00, 0x00 // 0.0000 dB
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
    case HID_REQUEST_GET_MAX:
        {
            UCHAR channel = get_low_bytes(pRequest->Value); // 低位 Channel Number
            UCHAR feature = get_high_bytes(pRequest->Value); // 高位 Feature Unit Control Selector

            TraceVerbose(
                TRACE_USBPDO,
                ">> >> >> >> GET_MAX(0x%02X): 0x%02X, Index=0x%04X",
                channel, feature, pRequest->Index);

            switch (feature)
            {
            case 0x02: // VOLUME_CONTROL
                {
                    switch (pRequest->Index)
                    {
                    case 0x0200: // 扬声器
                        {
                            TraceVerbose(
                                TRACE_USBPDO,
                                ">> >> >> >> Speaker Response");
                            UCHAR Response[] =
                            {
                                0x00, 0x00 // 0.0000 dB
                            };

                            pRequest->TransferBufferLength = ARRAYSIZE(Response);
                            RtlCopyBytes(pRequest->TransferBuffer, Response, ARRAYSIZE(Response));
                            break;
                        }
                    case 0x0500: // 麦克风
                        {
                            UCHAR Response[] =
                            {
                                0x00, 0x30 // 48.0000 dB
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
    case HID_REQUEST_GET_RES:
        {
            UCHAR channel = get_low_bytes(pRequest->Value); // 低位 Channel Number
            UCHAR feature = get_high_bytes(pRequest->Value); // 高位 Feature Unit Control Selector

            TraceVerbose(
                TRACE_USBPDO,
                ">> >> >> >> GET_RES(0x%02X): 0x%02X, Index=0x%04X",
                channel, feature, pRequest->Index);

            switch (feature)
            {
            case 0x02: // VOLUME_CONTROL
                {
                    switch (pRequest->Index)
                    {
                    case 0x0200: // 扬声器
                        {
                            TraceVerbose(
                                TRACE_USBPDO,
                                ">> >> >> >> Speaker Response");
                            UCHAR Response[] =
                            {
                                0x00, 0x01 // 1.0000 dB
                            };

                            pRequest->TransferBufferLength = ARRAYSIZE(Response);
                            RtlCopyBytes(pRequest->TransferBuffer, Response, ARRAYSIZE(Response));
                            break;
                        }
                    case 0x0500: // 麦克风
                        {
                            UCHAR Response[] =
                            {
                                0x7a, 0x00 // 0.4766 dB
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
    default:
        break;
    }
    
    TraceVerbose(TRACE_USBPDO, ">> >> >> >> END");

    Urb->UrbHeader.Status = USBD_STATUS_SUCCESS;

    return STATUS_SUCCESS;
}

NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::UsbIsochronousTransfer(PURB Urb, WDFREQUEST Request)
{
    //
    // Process audio data immediately (extract and broadcast to user-mode),
    // but delay the URB completion to throttle USBAudio's submission rate.
    //
    UsbInterfaceSubmitIsoOutUrb(this, Urb);

    //
    // Queue the request for delayed completion by the periodic timer.
    // This prevents USBAudio from instantly submitting the next URB,
    // matching real USB isochronous transfer timing (~10-20ms per URB).
    //
    NTSTATUS status = WdfRequestForwardToIoQueue(Request, this->_PendingIsoOutRequests);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_DS5,
            "WdfRequestForwardToIoQueue (IsoOut) failed with status %!STATUS!, completing immediately",
            status);
        return STATUS_SUCCESS;
    }

    return STATUS_PENDING;
}

NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::UsbGetDescriptorFromInterface(PURB Urb)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    UCHAR Ds5HidReportDescriptor[] =
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

    if (pRequest->TransferBufferLength >= ARRAYSIZE(Ds5HidReportDescriptor))
    {
        RtlCopyMemory(pRequest->TransferBuffer, Ds5HidReportDescriptor, ARRAYSIZE(Ds5HidReportDescriptor));
        status = STATUS_SUCCESS;

        //
        // Notify client library that PDO is ready
        // 
        KeSetEvent(&this->_PdoBootNotificationEvent, 0, FALSE);
    }

    return status;
}

NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::UsbSelectInterface(PURB Urb)
{
    PUSBD_INTERFACE_INFORMATION pInfo = &Urb->UrbSelectInterface.Interface;

    TraceVerbose(
        TRACE_USBPDO,
        ">> >> >> URB_FUNCTION_SELECT_INTERFACE: Length %d, Interface %d, Alternate %d, Pipes %d",
        static_cast<int>(pInfo->Length),
        static_cast<int>(pInfo->InterfaceNumber),
        static_cast<int>(pInfo->AlternateSetting),
        pInfo->NumberOfPipes);

    //
    // Audio Streaming interface (Interface 1)
    //
    if (pInfo->InterfaceNumber == 1)
    {
        // Alt 0: no endpoints
        if (pInfo->AlternateSetting == 0)
        {
            pInfo[0].Class = 0x01; // Audio
            pInfo[0].SubClass = 0x02; // Audio Streaming
            pInfo[0].Protocol = 0x00;
            pInfo[0].NumberOfPipes = 0x00;
            pInfo[0].InterfaceHandle = reinterpret_cast<USBD_INTERFACE_HANDLE>(0xFFFF0001);
            return STATUS_SUCCESS;
        }

        // Alt 1: Iso OUT endpoint 0x01
        if (pInfo->AlternateSetting == 1)
        {
            pInfo[0].Class = 0x01; // Audio
            pInfo[0].SubClass = 0x02; // Audio Streaming
            pInfo[0].Protocol = 0x00;
            pInfo[0].NumberOfPipes = 0x01;

            pInfo[0].InterfaceHandle = reinterpret_cast<USBD_INTERFACE_HANDLE>(0xFFFF0001);

            pInfo[0].Pipes[0].MaximumTransferSize = 0x00400000;
            pInfo[0].Pipes[0].MaximumPacketSize = 0x0188; // 392 bytes (matches descriptor)
            pInfo[0].Pipes[0].EndpointAddress = 0x01; // OUT
            pInfo[0].Pipes[0].Interval = 0x04;
            pInfo[0].Pipes[0].PipeType = static_cast<USBD_PIPE_TYPE>(0x01); // Isochronous
            pInfo[0].Pipes[0].PipeHandle = reinterpret_cast<USBD_PIPE_HANDLE>(0xFFFF0001);
            pInfo[0].Pipes[0].PipeFlags = 0x00;

            return STATUS_SUCCESS;
        }

        return STATUS_INVALID_PARAMETER;
    }

    //
    // Audio Streaming interface (Interface 2) - Audio IN
    //
    if (pInfo->InterfaceNumber == 2)
    {
        // Alt 0: no endpoints
        if (pInfo->AlternateSetting == 0)
        {
            pInfo[0].Class = 0x01; // Audio
            pInfo[0].SubClass = 0x02; // Audio Streaming
            pInfo[0].Protocol = 0x00;
            pInfo[0].NumberOfPipes = 0x00;
            pInfo[0].InterfaceHandle = reinterpret_cast<USBD_INTERFACE_HANDLE>(0xFFFF0002);
            return STATUS_SUCCESS;
        }

        // Alt 1: Iso IN endpoint 0x82
        if (pInfo->AlternateSetting == 1)
        {
            pInfo[0].Class = 0x01; // Audio
            pInfo[0].SubClass = 0x02; // Audio Streaming
            pInfo[0].Protocol = 0x00;
            pInfo[0].NumberOfPipes = 0x01;

            pInfo[0].InterfaceHandle = reinterpret_cast<USBD_INTERFACE_HANDLE>(0xFFFF0002);

            pInfo[0].Pipes[0].MaximumTransferSize = 0x00400000;
            pInfo[0].Pipes[0].MaximumPacketSize = 0x00C4; // 196 bytes (matches descriptor)
            pInfo[0].Pipes[0].EndpointAddress = 0x82; // IN
            pInfo[0].Pipes[0].Interval = 0x04;
            pInfo[0].Pipes[0].PipeType = static_cast<USBD_PIPE_TYPE>(0x01); // Isochronous
            pInfo[0].Pipes[0].PipeHandle = reinterpret_cast<USBD_PIPE_HANDLE>(0xFFFF0082);
            pInfo[0].Pipes[0].PipeFlags = 0x00;

            return STATUS_SUCCESS;
        }

        return STATUS_INVALID_PARAMETER;
    }

    //
    // HID interface (Interface 3) - keep existing behavior
    //
    if (pInfo->InterfaceNumber == 3)
    {
        pInfo->Class = 0x03; // HID
        pInfo->SubClass = 0x00;
        pInfo->Protocol = 0x00;

        pInfo->InterfaceHandle = reinterpret_cast<USBD_INTERFACE_HANDLE>(0xFFFF0003);

        pInfo->NumberOfPipes = 0x02;

        pInfo->Pipes[0].MaximumTransferSize = 0x00400000;
        pInfo->Pipes[0].MaximumPacketSize = 0x40;
        pInfo->Pipes[0].EndpointAddress = 0x84;
        pInfo->Pipes[0].Interval = 0x05;
        pInfo->Pipes[0].PipeType = static_cast<USBD_PIPE_TYPE>(0x03);
        pInfo->Pipes[0].PipeHandle = reinterpret_cast<USBD_PIPE_HANDLE>(0xFFFF0084);
        pInfo->Pipes[0].PipeFlags = 0x00;

        pInfo->Pipes[1].MaximumTransferSize = 0x00400000;
        pInfo->Pipes[1].MaximumPacketSize = 0x40;
        pInfo->Pipes[1].EndpointAddress = 0x03;
        pInfo->Pipes[1].Interval = 0x05;
        pInfo->Pipes[1].PipeType = static_cast<USBD_PIPE_TYPE>(0x03);
        pInfo->Pipes[1].PipeHandle = reinterpret_cast<USBD_PIPE_HANDLE>(0xFFFF0003);
        pInfo->Pipes[1].PipeFlags = 0x00;

        return STATUS_SUCCESS;
    }

    return STATUS_INVALID_PARAMETER;
}

NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::UsbGetStringDescriptorType(PURB Urb)
{
    TraceInformation(
        TRACE_USBPDO,
        "Index = %d",
        Urb->UrbControlDescriptorRequest.Index);

    switch (Urb->UrbControlDescriptorRequest.Index)
    {
    case 0:
        {
            // "American English"
            UCHAR LangId[] =
            {
                0x04, 0x03, 0x09, 0x04
            };

            Urb->UrbControlDescriptorRequest.TransferBufferLength = ARRAYSIZE(LangId);
            RtlCopyBytes(Urb->UrbControlDescriptorRequest.TransferBuffer, LangId, ARRAYSIZE(LangId));

            break;
        }
    case 1:
        {
            TraceVerbose(
                TRACE_USBPDO,
                "LanguageId = 0x%X",
                Urb->UrbControlDescriptorRequest.LanguageId);

            if (Urb->UrbControlDescriptorRequest.TransferBufferLength < DS5_MANUFACTURER_NAME_LENGTH)
            {
                auto pDesc = static_cast<PUSB_STRING_DESCRIPTOR>(Urb->UrbControlDescriptorRequest.TransferBuffer);
                pDesc->bLength = DS5_MANUFACTURER_NAME_LENGTH;
                break;
            }

            // "Sony Computer Entertainment"
            UCHAR ManufacturerString[DS5_MANUFACTURER_NAME_LENGTH] =
            {
                0x38, 0x03, 0x53, 0x00, 0x6F, 0x00, 0x6E, 0x00,
                0x79, 0x00, 0x20, 0x00, 0x43, 0x00, 0x6F, 0x00,
                0x6D, 0x00, 0x70, 0x00, 0x75, 0x00, 0x74, 0x00,
                0x65, 0x00, 0x72, 0x00, 0x20, 0x00, 0x45, 0x00,
                0x6E, 0x00, 0x74, 0x00, 0x65, 0x00, 0x72, 0x00,
                0x74, 0x00, 0x61, 0x00, 0x69, 0x00, 0x6E, 0x00,
                0x6D, 0x00, 0x65, 0x00, 0x6E, 0x00, 0x74, 0x00
            };

            Urb->UrbControlDescriptorRequest.TransferBufferLength = DS5_MANUFACTURER_NAME_LENGTH;
            RtlCopyBytes(Urb->UrbControlDescriptorRequest.TransferBuffer, ManufacturerString,
                         DS5_MANUFACTURER_NAME_LENGTH);

            break;
        }
    case 2:
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
                0x3c, 0x3, 0x44, 0x0, 0x75, 0x0, 0x61, 0x0,
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

NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::UsbBulkOrInterruptTransfer(
    _URB_BULK_OR_INTERRUPT_TRANSFER* pTransfer, WDFREQUEST Request)
{
    NTSTATUS status = STATUS_SUCCESS;
    WDFREQUEST notifyRequest;

    // Data coming FROM us TO higher driver
    if (pTransfer->TransferFlags & USBD_TRANSFER_DIRECTION_IN
        && pTransfer->PipeHandle == reinterpret_cast<USBD_PIPE_HANDLE>(0xFFFF0084))
    {
        TraceVerbose(
            TRACE_USBPDO,
            ">> >> >> Incoming request, queuing...");

        /* This request is sent periodically and relies on data the "feeder"
           has to supply, so we queue this request and return with STATUS_PENDING.
           The request gets completed as soon as the "feeder" sent an update. */
        status = WdfRequestForwardToIoQueue(Request, this->_PendingUsbInRequests);

        return (NT_SUCCESS(status)) ? STATUS_PENDING : status;
    }

    // Store relevant bytes of buffer in PDO context
    RtlCopyBytes(&this->_OutputReport,
                 static_cast<PUCHAR>(pTransfer->TransferBuffer) + DS5_OUTPUT_BUFFER_OFFSET,
                 DS5_OUTPUT_BUFFER_LENGTH);


    this->_AwaitOutputCache.Size = sizeof(DS5_AWAIT_OUTPUT);
    this->_AwaitOutputCache.SerialNo = this->_SerialNo;
    RtlCopyMemory(
        this->_AwaitOutputCache.Report.Buffer,
        pTransfer->TransferBuffer,
        pTransfer->TransferBufferLength <= sizeof(DS5_OUTPUT_BUFFER)
        ? pTransfer->TransferBufferLength
        : sizeof(DS5_OUTPUT_BUFFER)
    );

    DumpAsHex("!! XUSB_REQUEST_NOTIFICATION",
              &this->_AwaitOutputCache,
              sizeof(DS5_AWAIT_OUTPUT)
    );

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


    if (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(
        this->_PendingNotificationRequests,
        &notifyRequest)))
    {
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

NTSTATUS ViGEm::Bus::Targets::EmulationTargetDS5::SubmitReportImpl(PVOID NewReport)
{
    NTSTATUS status;
    WDFREQUEST usbRequest;

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
    const auto pSubmit = static_cast<PDS5_SUBMIT_REPORT>(NewReport);

    /*
     * Copy report to cache and transfer buffer
     * Skip first byte as it contains the never changing report ID
     */

    if (pSubmit->Size == sizeof(DS5_SUBMIT_REPORT))
    {
        TraceVerbose(TRACE_DS5, "Received DS5_SUBMIT_REPORT update");

        RtlCopyBytes(
            &this->_Report[1],
            &(static_cast<PDS5_SUBMIT_REPORT>(NewReport))->Report,
            sizeof((static_cast<PDS5_SUBMIT_REPORT>(NewReport))->Report)
        );
    }

    if (buffer)
        RtlCopyBytes(buffer, this->_Report, DS5_REPORT_SIZE);

    // Complete pending request
    WdfRequestComplete(usbRequest, status);

    return status;
}

VOID ViGEm::Bus::Targets::EmulationTargetDS5::ReverseByteArray(PUCHAR Array, INT Length)
{
    const auto s = static_cast<PUCHAR>(ExAllocatePoolZero(
        NonPagedPoolNx,
        sizeof(UCHAR) * Length,
        'U4SD'
    ));
    INT c, d;

    if (s == nullptr)
        return;

    for (c = Length - 1, d = 0; c >= 0; c--, d++)
        *(s + d) = *(Array + c);

    for (c = 0; c < Length; c++)
        *(Array + c) = *(s + c);

    ExFreePoolWithTag(s, 'U4SD');
}

VOID ViGEm::Bus::Targets::EmulationTargetDS5::GenerateRandomMacAddress(PMAC_ADDRESS Address)
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
            // Don't requeue request as we may be out of order now
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

//
// Timer callback for delayed ISO OUT URB completion.
// Fires every DS5_ISO_OUT_COMPLETION_PERIOD_MS and completes one
// pending ISO OUT request, throttling USBAudio's submission rate
// to match real USB isochronous transfer timing.
//
VOID ViGEm::Bus::Targets::EmulationTargetDS5::PendingIsoOutTimerFunc(
    _In_ WDFTIMER Timer
)
{
    const auto ctx = reinterpret_cast<EmulationTargetDS5*>(Core::EmulationTargetPdoGetContext(
        WdfTimerGetParentObject(Timer))->Target);

    WDFREQUEST isoRequest;

    const auto status = WdfIoQueueRetrieveNextRequest(
        ctx->_PendingIsoOutRequests, &isoRequest);

    if (NT_SUCCESS(status))
    {
        WdfRequestComplete(isoRequest, STATUS_SUCCESS);
    }
}

VOID ViGEm::Bus::Targets::EmulationTargetDS5::DmfDeviceModulesAdd(_In_ PDMFMODULE_INIT DmfModuleInit)
{
    UNREFERENCED_PARAMETER(DmfModuleInit);
}

VOID ViGEm::Bus::Targets::EmulationTargetDS5::SetOutputReportNotifyModule(DMFMODULE Module)
{
    this->_OutputReportNotify = Module;
}

VOID ViGEm::Bus::Targets::EmulationTargetDS5::SetAudioNotifyModule(DMFMODULE Module)
{
    this->_AudioNotify = Module;
}

NTSTATUS USB_BUSIFFN ViGEm::Bus::Targets::EmulationTargetDS5::UsbInterfaceSubmitIsoOutUrb(
    IN PVOID BusContext, IN PURB Urb)
{
    // 获取 PDO 上下文
    auto pdo = static_cast<EmulationTargetDS5*>(BusContext);
    LARGE_INTEGER perfFreq = {};
    const LARGE_INTEGER perfStart = KeQueryPerformanceCounter(&perfFreq);

    auto isoUrb = &Urb->UrbIsochronousTransfer;
        
    // 处理所有 ISO 数据包
    // 首先将整个 URB 的音频数据收集到缓存中，然后一次性广播给用户态
    ULONG totalAudioLength = 0;
    DS5_AUDIO_DATA audioCache = {};
    audioCache.Size = sizeof(DS5_AUDIO_DATA);
    audioCache.SerialNo = pdo->_SerialNo;

    for (ULONG i = 0; i < isoUrb->NumberOfPackets; i++)
    {
        PUSBD_ISO_PACKET_DESCRIPTOR packet = &isoUrb->IsoPacket[i];
        
        // ISOCH OUT 的 Length 字段在提交时不被填充（始终为0），
        // 每个包的实际数据长度需要通过相邻包的 Offset 差值计算
        ULONG packetLength;
        if (i < isoUrb->NumberOfPackets - 1)
        {
            packetLength = isoUrb->IsoPacket[i + 1].Offset - packet->Offset;
        }
        else
        {
            packetLength = isoUrb->TransferBufferLength - packet->Offset;
        }
        
        if (packetLength > 0 && (totalAudioLength + packetLength) <= DS5_AUDIO_DATA_MAX_SIZE)
        {
            PUCHAR audioData = (PUCHAR)isoUrb->TransferBuffer + packet->Offset;
            RtlCopyMemory(&audioCache.AudioData[totalAudioLength], audioData, packetLength);
            totalAudioLength += packetLength;
        }
            
        TraceVerbose(TRACE_DS5, "ISOCH OUT: Packet %u: Offset=%u, CalcLength=%u, Status=%u", 
                       i, packet->Offset, packetLength, packet->Status);

        // 设置每个 ISO 包的完成状态
        packet->Status = USBD_STATUS_SUCCESS;
    }

    // 如果有音频数据，广播给用户态应用
    if (totalAudioLength > 0 && pdo->_AudioNotify != nullptr)
    {
        audioCache.AudioDataLength = totalAudioLength;

        TraceVerbose(TRACE_DS5, "Broadcasting audio data: %u bytes", totalAudioLength);

        NTSTATUS broadcastStatus = DMF_NotifyUserWithRequestMultiple_DataBroadcast(
            pdo->_AudioNotify,
            &audioCache,
            sizeof(DS5_AUDIO_DATA),
            STATUS_SUCCESS
        );

        if (!NT_SUCCESS(broadcastStatus))
        {
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_DS5,
                "Audio DMF_NotifyUserWithRequestMultiple_DataBroadcast failed with status %!STATUS!",
                broadcastStatus);
        }
    }

    // 设置 URB 完成状态
    Urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
    isoUrb->ErrorCount = 0;
        
    ULONGLONG elapsedUs = 0;

    if (perfFreq.QuadPart != 0)
    {
        const LARGE_INTEGER perfEnd = KeQueryPerformanceCounter(nullptr);
        elapsedUs = (static_cast<ULONGLONG>(perfEnd.QuadPart - perfStart.QuadPart) * 1000000ULL)
            / static_cast<ULONGLONG>(perfFreq.QuadPart);
    }

    TraceVerbose(TRACE_DS5, 
                    "ISOCH OUT URB completed: StartFrame=%u, Packets=%u, ErrorCount=%u, ElapsedUs=%llu",
                    isoUrb->StartFrame,
                    isoUrb->NumberOfPackets,
                    isoUrb->ErrorCount,
                    elapsedUs);
    
    // 注意：这个函数通过 USB Bus Interface 被直接调用
    // 调用者期望同步返回，URB 的完成会通过 IRP 的完成机制通知上层
    // 如果这个 URB 来自 IOCTL 路径，IRP 会在 EmulationTargetPDO::EvtIoInternalDeviceControl 中完成


    return STATUS_SUCCESS;
}
