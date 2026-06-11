/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/* *
 * @file HardwareSerial.cpp
 * @brief HardwareSerial class implementation for Arduino compatibility - Direct SDK call
 * @version 3.0
 * @date 2026-04-21
 */

#include "HardwareSerial.h"
#include <string.h>
#include <securec.h>

// Buffer size for RX/TX
#ifndef SERIAL_RX_BUFFER_SIZE
#define SERIAL_RX_BUFFER_SIZE 64
#endif

#ifndef SERIAL_TX_BUFFER_SIZE
#define SERIAL_TX_BUFFER_SIZE 64
#endif

// HardwareSerial Constructor
HardwareSerial::HardwareSerial(uart_bus_t uart_bus)
    : _uart_bus(uart_bus),
      _initialized(false),
      _rx_buffer(nullptr),
      _rx_head(0),
      _rx_tail(0),
      _tx_buffer(nullptr),
      _tx_head(0),
      _tx_tail(0),
      _baud_rate(0),
      _config(SERIAL_8N1)
{
    _init_buffer();
}

// HardwareSerial Destructor
HardwareSerial::~HardwareSerial()
{
    end();
    if (_rx_buffer) {
        delete[] _rx_buffer;
        _rx_buffer = nullptr;
    }
    if (_tx_buffer) {
        delete[] _tx_buffer;
        _tx_buffer = nullptr;
    }
}

// Initialize buffers
void HardwareSerial::_init_buffer()
{
    _rx_buffer = new uint8_t[SERIAL_RX_BUFFER_SIZE];
    if (!_rx_buffer) {
        return; // Allocation failed
    }
    _tx_buffer = new uint8_t[SERIAL_TX_BUFFER_SIZE];
    if (!_tx_buffer) {
        delete[] _rx_buffer;
        _rx_buffer = nullptr;
        return; // Allocation failed
    }

    _rx_head = 0;
    _rx_tail = 0;
    _tx_head = 0;
    _tx_tail = 0;
}

// Store character in circular buffer
void HardwareSerial::_store_char(uint8_t c, volatile uint16_t *head, volatile uint16_t *tail, uint8_t *buffer,
    size_t buffer_size)
{
    if (head == nullptr || tail == nullptr || buffer == nullptr || buffer_size == 0) {
        return;
    }

    uint16_t next_head = (*head + 1) % buffer_size;
    if (next_head != *tail) {
        buffer[*head] = c;
        *head = next_head;
    }
}

// Read from buffer
int HardwareSerial::_read_from_buffer()
{
    if (!_rx_buffer) {
        return -1; // Buffer not allocated
    }
    if (_rx_head == _rx_tail) {
        return -1; // Buffer empty
    }
    uint8_t c = _rx_buffer[_rx_tail];
    _rx_tail = (_rx_tail + 1) % SERIAL_RX_BUFFER_SIZE;
    return c;
}

// Check available in buffer
int HardwareSerial::_available_in_buffer() const
{
    if (!_rx_buffer) {
        return 0; // Buffer not allocated
    }
    if (_rx_head >= _rx_tail) {
        return _rx_head - _rx_tail;
    } else {
        return SERIAL_RX_BUFFER_SIZE - _rx_tail + _rx_head;
    }
}

// Write to TX buffer
void HardwareSerial::_write_to_buffer(uint8_t c)
{
    if (!_tx_buffer) {
        return; // Buffer not allocated
    }
    uint16_t next_head = (_tx_head + 1) % SERIAL_TX_BUFFER_SIZE;
    if (next_head != _tx_tail) {
        _tx_buffer[_tx_head] = c;
        _tx_head = next_head;
    }
}

// RX Callback (static for C linkage)
void HardwareSerial::_rx_callback(const void *buffer, uint16_t length, bool error)
{
    if (error || buffer == nullptr || length == 0) {
        return;
    }

    const uint8_t *data = (const uint8_t *)buffer;
    (void)data; // Unused in stub implementation
    for (uint16_t i = 0; i < length; i++) {
        // Store in buffer - this is called from ISR context
        // In a real implementation, this would need to find the right
        // HardwareSerial instance based on the UART bus
        // For now, this is a simplified implementation
        (void)i; // Unused in stub implementation
    }
}

// Begin with default config (8N1)
void HardwareSerial::begin(unsigned long baud)
{
    begin(baud, SERIAL_8N1);
}

// Begin with specified config
void HardwareSerial::begin(unsigned long baud, uint8_t config)
{
    if (_initialized) {
        end();
    }

    // Validate UART bus number
    if (_uart_bus >= UART_BUS_MAX_NUMBER) {
        // This UART bus is not supported by hardware
        _initialized = false;
        return;
    }

    // Validate baud rate (reasonable range: 300 to 10 Mbps)
    if (baud < 300 || baud > 10000000UL) { // baud rate range: 300 bps to 10000000UL(10 Mbps)
        _initialized = false;
        return;
    }

    // Validate config: extract and verify data bits (bits 1-3 must be 0-3)
    uint8_t data_bits_val = (config >> 1) & 0x07;
    if (data_bits_val > 3) { // Valid values are 0-3 for 5-8 data bits
        _initialized = false;
        return;
    }

    // Configure UART attributes
    uart_attr_t attr;
    attr.baud_rate = (uint32_t)baud;

    // Parse config to data bits, parity, stop bits
    data_bits_val = (config >> 1) & 0x07; // Extract bits 1-3
    switch (data_bits_val) {
        case 0: // 5 bits (SERIAL_5N1, SERIAL_5N2)
            attr.data_bits = UART_DATA_BIT_5;
            break;
        case 1: // 6 bits (SERIAL_6N1, SERIAL_6N2)
            attr.data_bits = UART_DATA_BIT_6;
            break;
        case 2: // 7 bits (SERIAL_7N1, SERIAL_7N2)
            attr.data_bits = UART_DATA_BIT_7;
            break;
        case 3: // 3 means 8 bits (SERIAL_8N1, SERIAL_8N2) and default
        default:
            attr.data_bits = UART_DATA_BIT_8;
            break;
    }

    // Parity (check if even or odd parity mode)
    // Even parity: 0x08-0x0F, Odd parity: 0x10-0x17, None: others
    if (config >= 0x08 && config <= 0x0F) {
        attr.parity = UART_PARITY_EVEN;
    } else if (config >= 0x10 && config <= 0x17) {
        attr.parity = UART_PARITY_ODD;
    } else {
        attr.parity = UART_PARITY_NONE;
    }

    // Stop bits (bit 0 and parity bit)
    if (config & 0x01) {
        attr.stop_bits = UART_STOP_BIT_2;
    } else {
        attr.stop_bits = UART_STOP_BIT_1;
    }

    // Configure pins based on UART bus
    // Pin mapping is handled by SDK's uart_port_config_pinmux()
    // Default configuration for ws63:
    // - UART_BUS_0: GPIO 13/14 (TX/RX)
    // - UART_BUS_1: GPIO 13/14 (TX/RX) - alternative function
    // - UART_BUS_2: GPIO 7/8 or GPIO 12/13 (depends on board)
    uart_pin_config_t pins;
    pins.tx_pin = (pin_t) - 1; // Use SDK default pin mapping
    pins.rx_pin = (pin_t) - 1;
    pins.cts_pin = (pin_t) - 1; // Not used
    pins.rts_pin = (pin_t) - 1; // Not used

    // Extra attributes
    uart_extra_attr_t extra_attr;
    (void)memset_s(&extra_attr, sizeof(extra_attr), 0, sizeof(extra_attr));

    // RX buffer config
    uart_buffer_config_t rx_config;
    rx_config.rx_buffer = _rx_buffer;
    rx_config.rx_buffer_size = SERIAL_RX_BUFFER_SIZE;

    // Initialize UART
    errcode_t ret = uapi_uart_init(_uart_bus, &pins, &attr, &extra_attr, &rx_config);
    if (ret == ERRCODE_SUCC) {
        _baud_rate = baud;
        _config = config;
        _initialized = true;

        // Register RX callback
        uapi_uart_register_rx_callback(_uart_bus, UART_RX_CONDITION_FULL_OR_SUFFICIENT_DATA_OR_IDLE,
            1, // Trigger on each byte
            _rx_callback);
    } else {
        // Initialization failed - possible pin conflict or hardware issue
        _initialized = false;
    }
}

// End UART
void HardwareSerial::end()
{
    if (!_initialized) {
        return;
    }

    uapi_uart_unregister_rx_callback(_uart_bus);
    uapi_uart_deinit(_uart_bus);
    _initialized = false;
}

// Check available bytes
int HardwareSerial::available() const
{
    if (!_initialized) {
        return 0;
    }

    // Check buffer first
    int buf_avail = _available_in_buffer();
    if (buf_avail > 0) {
        return buf_avail;
    }

    // Check if RX FIFO has data
    if (!uapi_uart_rx_fifo_is_empty(_uart_bus)) {
        return 1;
    }

    return 0;
}

// Read a byte
int HardwareSerial::read()
{
    if (!_initialized) {
        return -1;
    }

    // Try buffer first
    int c = _read_from_buffer();
    if (c >= 0) {
        return c;
    }

    // Read directly from UART
    uint8_t byte;
    int32_t ret = uapi_uart_read(_uart_bus, &byte, 1, 0); // No timeout
    if (ret > 0) {
        return byte;
    }

    return -1;
}

// Peek at next byte
int HardwareSerial::peek() const
{
    if (!_initialized || _rx_head == _rx_tail) {
        return -1;
    }

    return _rx_buffer[_rx_tail];
}

// Flush TX buffer
void HardwareSerial::flush()
{
    if (!_initialized) {
        return;
    }

    // Wait until TX FIFO is empty
    while (!uapi_uart_tx_fifo_is_empty(_uart_bus)) {
        // Wait
    }

    // Clear TX buffer
    _tx_head = 0;
    _tx_tail = 0;
}

// Write a single byte
size_t HardwareSerial::write(uint8_t c)
{
    if (!_initialized) {
        return 0;
    }

    int32_t ret = uapi_uart_write(_uart_bus, &c, 1, 100); // 100ms timeout
    return (ret > 0) ? 1 : 0;
}

// Write a buffer
size_t HardwareSerial::write(const uint8_t *buffer, size_t size)
{
    if (!_initialized || buffer == nullptr || size == 0) {
        return 0;
    }

    int32_t ret = uapi_uart_write(_uart_bus, buffer, (uint32_t)size, 100);
    return (ret > 0) ? (size_t)ret : 0;
}

// Write a string
size_t HardwareSerial::write(const char *str)
{
    if (!_initialized || str == nullptr) {
        return 0;
    }

    size_t len = strlen(str);
    return write((const uint8_t *)str, len);
}

// Primary serial port (typically connected to USB/UART debug)
// Always available (UART_BUS_0)
HardwareSerial Serial(UART_BUS_0);

// Secondary serial port
// Only available if hardware supports UART_BUS_1
#if UART_BUS_MAX_NUMBER > 1
HardwareSerial Serial1(UART_BUS_1);
#endif

// Tertiary serial port
// Only available if hardware supports UART_BUS_2
#if UART_BUS_MAX_NUMBER > 2
HardwareSerial Serial2(UART_BUS_2);
#endif
