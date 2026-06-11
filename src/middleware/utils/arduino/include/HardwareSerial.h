/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/* *
 * @file HardwareSerial.h
 * @brief HardwareSerial class for Arduino compatibility - Direct SDK call implementation
 * @version 3.0
 * @date 2026-04-21
 */
/*
  HardwareSerial.h - Hardware serial library for Wiring
  Copyright (c) 2006 Nicholas Zambetti.  All right reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

  Modified 28 September 2010 by Mark Sproul
  Modified 14 August 2012 by Alarus
  Modified 3 December 2013 by Matthijs Kooijman
*/


#ifndef HARDWARESERIAL_H
#define HARDWARESERIAL_H

#include <stdint.h>
#include <stddef.h>
#include "Stream.h"

// UART configuration - must be defined before including uart.h
#ifndef CONFIG_UART_SUPPORT_TX
#define CONFIG_UART_SUPPORT_TX 1
#endif
#ifndef CONFIG_UART_SUPPORT_RX
#define CONFIG_UART_SUPPORT_RX 1
#endif

#include "driver/uart.h"

// Buffer size for RX/TX (can be adjusted based on memory constraints)
#ifndef SERIAL_RX_BUFFER_SIZE
#define SERIAL_RX_BUFFER_SIZE 64
#endif

#ifndef SERIAL_TX_BUFFER_SIZE
#define SERIAL_TX_BUFFER_SIZE 64
#endif

// UART configuration macros
#define SERIAL_5N1 0x00
#define SERIAL_6N1 0x02
#define SERIAL_7N1 0x04
#define SERIAL_8N1 0x06
#define SERIAL_5N2 0x08
#define SERIAL_6N2 0x0A
#define SERIAL_7N2 0x0C
#define SERIAL_8N2 0x0E
#define SERIAL_5E1 0x20
#define SERIAL_6E1 0x22
#define SERIAL_7E1 0x24
#define SERIAL_8E1 0x26
#define SERIAL_5E2 0x28
#define SERIAL_6E2 0x2A
#define SERIAL_7E2 0x2C
#define SERIAL_8E2 0x2E
#define SERIAL_5O1 0x30
#define SERIAL_6O1 0x32
#define SERIAL_7O1 0x34
#define SERIAL_8O1 0x36
#define SERIAL_5O2 0x38
#define SERIAL_6O2 0x3A
#define SERIAL_7O2 0x3C
#define SERIAL_8O2 0x3E

class HardwareSerial : public Stream {
private:
    uart_bus_t _uart_bus;
    bool _initialized;

    // RX Buffer
    uint8_t *_rx_buffer;
    volatile uint16_t _rx_head;
    volatile uint16_t _rx_tail;

    // TX Buffer (optional - SDK may have internal buffering)
    uint8_t *_tx_buffer;
    volatile uint16_t _tx_head;
    volatile uint16_t _tx_tail;

    // Configuration
    uint32_t _baud_rate;
    uint8_t _config;

    // Private methods
    void _init_buffer();
    void _store_char(uint8_t c, volatile uint16_t *head, volatile uint16_t *tail, uint8_t *buffer, size_t buffer_size);
    int _read_from_buffer();
    int _available_in_buffer() const;
    void _write_to_buffer(uint8_t c);

    // SDK callback for RX (static for C linkage)
    static void _rx_callback(const void *buffer, uint16_t length, bool error);

public:
    HardwareSerial(uart_bus_t uart_bus = UART_BUS_0);
    ~HardwareSerial();

    // Begin/End - Direct SDK calls
    void begin(unsigned long baud);
    void begin(unsigned long baud, uint8_t config);
    void end();

    // Status
    int available() const;

    // Read methods (Stream interface)
    int read();
    int peek() const;
    void flush();

    // Write methods (Print interface)
    size_t write(uint8_t c);
    size_t write(const uint8_t *buffer, size_t size);
    size_t write(const char *str);

    // Direct SDK access
    uart_bus_t getUartBus() const
    {
        return _uart_bus;
    }
    bool isInitialized() const
    {
        return _initialized;
    }

    // Operator overloads for compatibility
    operator bool() const
    {
        return _initialized;
    }
};

// Global Serial instances
extern HardwareSerial Serial;

// Conditional Serial1/Serial2 based on hardware support
// SDK defines UART_BUS_MAX_NUMBER in platform_core.h
#if UART_BUS_MAX_NUMBER > 1
extern HardwareSerial Serial1;
#endif

#if UART_BUS_MAX_NUMBER > 2
extern HardwareSerial Serial2;
#endif

#endif // HARDWARESERIAL_H
