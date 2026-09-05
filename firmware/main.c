#include <avr/interrupt.h>
#include <avr/power.h>
#include <avr/wdt.h>
#include <util/delay.h>
#include "Descriptors.h"
#include "protocol.h"
#include "sst39sf020.h"

static protocol_t parser;
static bool boot_requested;
static void request_boot(void) { boot_requested = true; }
static const protocol_ops_t operations = { sst_read_id, sst_read_block, sst_program_block, sst_erase, sst_erase_bank, request_boot };

static USB_ClassInfo_CDC_Device_t serial = {
    .Config = {
        .ControlInterfaceNumber = INTERFACE_ID_CDC_CCI,
        .DataINEndpoint = { .Address = CDC_TX_EPADDR, .Size = CDC_TXRX_EPSIZE, .Banks = 1 },
        .DataOUTEndpoint = { .Address = CDC_RX_EPADDR, .Size = CDC_TXRX_EPSIZE, .Banks = 1 },
        .NotificationEndpoint = { .Address = CDC_NOTIFICATION_EPADDR,
                                  .Size = CDC_NOTIFICATION_EPSIZE, .Banks = 1 },
    },
};

void EVENT_USB_Device_ConfigurationChanged(void)
{
    protocol_reset(&parser);
    CDC_Device_ConfigureEndpoints(&serial);
}

void EVENT_USB_Device_ControlRequest(void)
{
    CDC_Device_ProcessControlRequest(&serial);
}

void EVENT_USB_Device_Disconnect(void)
{
    protocol_reset(&parser);
}

void sst_service(void)
{
    CDC_Device_USBTask(&serial);
    USB_USBTask();
}

static bool send_bank(uint8_t bank)
{
    uint8_t data[256];
    uint16_t crc = 0;
    uint16_t first_block = (uint16_t)bank << 6;
    for (uint8_t i = 0; i < 64; ++i) {
        if (sst_read_block(first_block + i, data)) return false;
        crc = protocol_crc_update(crc, data, sizeof(data));
        if (CDC_Device_SendData(&serial, data, sizeof(data)) != ENDPOINT_READYWAIT_NoError)
            return false;
    }
    uint8_t checksum[2] = { crc >> 8, crc };
    return CDC_Device_SendData(&serial, checksum, sizeof(checksum)) == ENDPOINT_READYWAIT_NoError;
}

int main(void)
{
    MCUSR &= ~_BV(WDRF);
    wdt_disable();
    /* Y1 is 16 MHz. Override CKDIV8 at runtime; no clock fuse write needed. */
    clock_prescale_set(clock_div_1);

    /* PF4 is a cartridge latch signal, not a JTAG pin in this application.
       JTD requires two writes within four cycles, with interrupts disabled. */
    MCUCR |= _BV(JTD);
    MCUCR |= _BV(JTD);
    sst_init();

    USB_Init();
    sei();

    uint16_t last_frame = 0;
    uint8_t response[RESPONSE_MAX];
    for (;;) {
        bool ready = USB_DeviceState == DEVICE_STATE_Configured &&
            (serial.State.ControlLineStates.HostToDevice & CDC_CONTROL_LINE_OUT_DTR);
        uint16_t frame = USB_Device_GetFrameNumber();
        if (protocol_tick(&parser, (frame - last_frame) & 0x7ff))
            CDC_Device_SendByte(&serial, 7);
        last_frame = frame;
        int16_t byte = CDC_Device_ReceiveByte(&serial);
        if (!ready) {
            protocol_reset(&parser);
        } else if (byte >= 0) {
            uint16_t count = protocol_feed(&parser, byte, response, &operations);
            if (count) {
                if (CDC_Device_SendData(&serial, response, count) != ENDPOINT_READYWAIT_NoError) {
                    protocol_reset(&parser);
                } else {
                    uint8_t bank;
                    if (protocol_take_bank_read(&parser, &bank) && !send_bank(bank))
                        protocol_reset(&parser);
                }
            }
        }
        CDC_Device_USBTask(&serial);
        USB_USBTask();
        if (boot_requested) {
            CDC_Device_Flush(&serial);
            for (uint8_t i = 0; i < 50; ++i) { sst_service(); _delay_ms(1); }
            USB_Detach();
            cli();
            *(volatile uint16_t*)0x0afe = 0xb007;
            wdt_enable(WDTO_120MS);
            for (;;) {}
        }
    }
}
