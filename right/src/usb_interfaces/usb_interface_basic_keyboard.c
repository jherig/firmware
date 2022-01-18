#include "led_display.h"
#include "usb_composite_device.h"
#include "usb_descriptors/usb_descriptor_basic_keyboard_report.h"
#include "usb_interfaces/usb_interface_basic_keyboard.h"
#include "usb_report_updater.h"
#include "debug.h"

static usb_basic_keyboard_report_t usbBasicKeyboardReports[2];
static uint8_t usbBasicKeyboardOutBuffer[USB_BASIC_KEYBOARD_SET_REPORT_LENGTH];
usb_hid_protocol_t usbBasicKeyboardProtocol;
uint32_t UsbBasicKeyboardActionCounter;
usb_basic_keyboard_report_t* ActiveUsbBasicKeyboardReport = usbBasicKeyboardReports;

static usb_basic_keyboard_report_t* GetInactiveUsbBasicKeyboardReport(void)
{
    return ActiveUsbBasicKeyboardReport == usbBasicKeyboardReports ? usbBasicKeyboardReports+1 : usbBasicKeyboardReports;
}

static void SwitchActiveUsbBasicKeyboardReport(void)
{
    ActiveUsbBasicKeyboardReport = GetInactiveUsbBasicKeyboardReport();
}

void UsbBasicKeyboardResetActiveReport(void)
{
    bzero(ActiveUsbBasicKeyboardReport, USB_BASIC_KEYBOARD_REPORT_LENGTH);
}

usb_hid_protocol_t UsbBasicKeyboardGetProtocol(void)
{
    return usbBasicKeyboardProtocol;
}

usb_status_t UsbBasicKeyboardAction(void)
{
    if (!UsbCompositeDevice.attach) {
        return kStatus_USB_Error; // The device is not attached
    }

    uint16_t length = usbBasicKeyboardProtocol == 0 ? USB_BOOT_KEYBOARD_REPORT_LENGTH : USB_BASIC_KEYBOARD_REPORT_LENGTH;

    usb_status_t usb_status = USB_DeviceHidSend(
        UsbCompositeDevice.basicKeyboardHandle, USB_BASIC_KEYBOARD_ENDPOINT_INDEX,
        (uint8_t *)ActiveUsbBasicKeyboardReport, length);
    if (usb_status == kStatus_USB_Success) {
        UsbBasicKeyboardActionCounter++;
        SwitchActiveUsbBasicKeyboardReport();
    }

    // latch the active protocol to avoid ISR <-> Thread race
    usbBasicKeyboardProtocol = ((usb_device_hid_struct_t*)UsbCompositeDevice.basicKeyboardHandle)->protocol;

    return usb_status;
}

usb_status_t UsbBasicKeyboardCheckIdleElapsed()
{
    return kStatus_USB_Busy;
}

usb_status_t UsbBasicKeyboardCheckReportReady()
{
    if (memcmp(ActiveUsbBasicKeyboardReport, GetInactiveUsbBasicKeyboardReport(), sizeof(usb_basic_keyboard_report_t)) != 0)
        return kStatus_USB_Success;

    return UsbBasicKeyboardCheckIdleElapsed();
}

usb_status_t UsbBasicKeyboardCallback(class_handle_t handle, uint32_t event, void *param)
{
    usb_device_hid_struct_t *hidHandle = (usb_device_hid_struct_t *)handle;
    usb_status_t error = kStatus_USB_InvalidRequest;

    switch (event) {
        case ((uint32_t)-kUSB_DeviceEventSetConfiguration):
            error = kStatus_USB_Success;
            break;
        case ((uint32_t)-kUSB_DeviceEventSetInterface):
            if (*(uint8_t*)param == 0) {
                error = kStatus_USB_Success;
            }
            break;

        case kUSB_DeviceHidEventSendResponse:
            UsbReportUpdateSemaphore &= ~(1 << USB_BASIC_KEYBOARD_INTERFACE_INDEX);
            if (UsbCompositeDevice.attach) {
                error = kStatus_USB_Success;
            }
            break;

        case kUSB_DeviceHidEventGetReport: {
            usb_device_hid_report_struct_t *report = (usb_device_hid_report_struct_t*)param;
            if (report->reportType == USB_DEVICE_HID_REQUEST_GET_REPORT_TYPE_INPUT && report->reportId == 0 && report->reportLength <= USB_BASIC_KEYBOARD_REPORT_LENGTH) {
                report->reportBuffer = (void*)ActiveUsbBasicKeyboardReport;

                UsbBasicKeyboardActionCounter++;
                SwitchActiveUsbBasicKeyboardReport();
                error = kStatus_USB_Success;
            } else {
                error = kStatus_USB_InvalidRequest;
            }
            break;
        }

        case kUSB_DeviceHidEventSetReport: {
            usb_device_hid_report_struct_t *report = (usb_device_hid_report_struct_t*)param;
            if (report->reportType == USB_DEVICE_HID_REQUEST_GET_REPORT_TYPE_OUPUT && report->reportId == 0 && report->reportLength == sizeof(usbBasicKeyboardOutBuffer)) {
                LedDisplay_SetIcon(LedDisplayIcon_CapsLock, report->reportBuffer[0] & HID_KEYBOARD_LED_CAPSLOCK);
                error = kStatus_USB_Success;
            } else {
                error = kStatus_USB_InvalidRequest;
            }
            break;
        }
        case kUSB_DeviceHidEventRequestReportBuffer: {
            usb_device_hid_report_struct_t *report = (usb_device_hid_report_struct_t*)param;
            if (report->reportLength <= sizeof(usbBasicKeyboardOutBuffer)) {
                report->reportBuffer = usbBasicKeyboardOutBuffer;
                error = kStatus_USB_Success;
            } else {
                error = kStatus_USB_AllocFail;
            }
            break;
        }

        case kUSB_DeviceHidEventSetProtocol: {
            uint8_t report = *(uint16_t*)param;
            if (report <= 1) {
                hidHandle->protocol = report;
                error = kStatus_USB_Success;
            }
            else {
                error = kStatus_USB_InvalidRequest;
            }
            break;
        }

        default:
            break;
    }

    return error;
}

usb_status_t UsbBasicKeyboardSetConfiguration(class_handle_t handle, uint8_t configuration)
{
    usbBasicKeyboardProtocol = 1; // HID Interfaces with boot protocol support start in report protocol mode.
    return kStatus_USB_Error;
}

usb_status_t UsbBasicKeyboardSetInterface(class_handle_t handle, uint8_t interface, uint8_t alternateSetting)
{
    return kStatus_USB_Error;
}

static void setRolloverError(usb_basic_keyboard_report_t* report)
{
    if (report->scancodes[0] != HID_KEYBOARD_SC_ERROR_ROLLOVER) {
        memset(report->scancodes, HID_KEYBOARD_SC_ERROR_ROLLOVER, USB_BOOT_KEYBOARD_MAX_KEYS);
    }
}

void UsbBasicKeyboard_AddScancode(usb_basic_keyboard_report_t* report, uint8_t scancode, uint8_t* idx)
{
    if (usbBasicKeyboardProtocol==0) {
        if (idx != NULL && *idx < USB_BOOT_KEYBOARD_MAX_KEYS) {
            report->scancodes[(*idx)++] = scancode;
            return;
        }

        if (idx == NULL) {
            for (uint8_t i = 0; i < USB_BOOT_KEYBOARD_MAX_KEYS; i++) {
                if (!report->scancodes[i]) {
                    report->scancodes[i] = scancode;
                    return;
                }
            }
        }

        setRolloverError(report);
    } else if (USB_BASIC_KEYBOARD_IS_IN_BITFIELD(scancode)) {
        set_bit(scancode - USB_BASIC_KEYBOARD_MIN_BITFIELD_SCANCODE, report->bitfield);
    } else if (USB_BASIC_KEYBOARD_IS_IN_MODIFIERS(scancode)) {
        set_bit(scancode - USB_BASIC_KEYBOARD_MIN_MODIFIERS_SCANCODE, &report->modifiers);
    }
}

void UsbBasicKeyboard_RemoveScancode(usb_basic_keyboard_report_t* report, uint8_t scancode)
{
    if (usbBasicKeyboardProtocol==0) {
        for (uint8_t i = 0; i < USB_BOOT_KEYBOARD_MAX_KEYS; i++) {
            if (report->scancodes[i] == scancode) {
                report->scancodes[i] = 0;
                return;
            }
        }
    } else if (USB_BASIC_KEYBOARD_IS_IN_BITFIELD(scancode)) {
        clear_bit(scancode - USB_BASIC_KEYBOARD_MIN_BITFIELD_SCANCODE, report->bitfield);
    } else if (USB_BASIC_KEYBOARD_IS_IN_MODIFIERS(scancode)) {
        clear_bit(scancode - USB_BASIC_KEYBOARD_MIN_MODIFIERS_SCANCODE, &report->modifiers);
    }
}

bool UsbBasicKeyboard_ContainsScancode(usb_basic_keyboard_report_t* report, uint8_t scancode)
{
    if (usbBasicKeyboardProtocol==0) {
        for (uint8_t i = 0; i < USB_BOOT_KEYBOARD_MAX_KEYS; i++) {
            if (report->scancodes[i] == scancode) {
                return true;
            }
        }
        return false;
    } else if (USB_BASIC_KEYBOARD_IS_IN_BITFIELD(scancode)) {
        return test_bit(scancode - USB_BASIC_KEYBOARD_MIN_BITFIELD_SCANCODE, report->bitfield);
    } else if (USB_BASIC_KEYBOARD_IS_IN_MODIFIERS(scancode)) {
        return test_bit(scancode - USB_BASIC_KEYBOARD_MIN_MODIFIERS_SCANCODE, &report->modifiers);
    } else {
        return false;
    }
}

void UsbBasicKeyboard_MergeReports(usb_basic_keyboard_report_t* targetReport, usb_basic_keyboard_report_t* sourceReport, uint8_t* idx)
{
    if (usbBasicKeyboardProtocol==0) {
        targetReport->modifiers |= sourceReport->modifiers;
        for (int i = 0; sourceReport->scancodes[i] != 0; i++) {
            if (*idx < USB_BOOT_KEYBOARD_MAX_KEYS) {
                targetReport->scancodes[(*idx)++] = sourceReport->scancodes[i];
            } else {
                setRolloverError(targetReport);
                return;
            }
        }
    } else {
        targetReport->modifiers |= sourceReport->modifiers;
        for (int i = 0; i < USB_BASIC_KEYBOARD_BITFIELD_LENGTH; i++) {
            targetReport->bitfield[i] |= sourceReport->bitfield[i];
        }
    }
}
