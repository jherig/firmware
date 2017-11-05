#include "fsl_common.h"
#include "usb_commands/usb_command_write_config.h"
#include "usb_protocol_handler.h"
#include "eeprom.h"

void UsbCommand_WriteConfig(bool isHardware)
{
    uint8_t length = GET_USB_BUFFER_UINT8(1);
    uint16_t offset = GET_USB_BUFFER_UINT16(2);

    if (length > USB_GENERIC_HID_OUT_BUFFER_LENGTH-4) {
        SET_USB_BUFFER_UINT8(0, UsbStatusCode_TransferConfig_LengthTooLarge);
        return;
    }

    uint8_t *buffer = isHardware ? HardwareConfigBuffer.buffer : StagingUserConfigBuffer.buffer;
    uint16_t bufferLength = isHardware ? HARDWARE_CONFIG_SIZE : USER_CONFIG_SIZE;

    if (offset + length > bufferLength) {
        SET_USB_BUFFER_UINT8(0, UsbStatusCode_TransferConfig_BufferOutOfBounds);
        return;
    }

    memcpy(buffer+offset, GenericHidInBuffer+4, length);
}
