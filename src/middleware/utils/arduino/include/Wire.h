/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/* *
 * @file Wire.h
 * @brief TwoWire class for I2C communication - Full SDK integration
 * @version 3.0
 * @date 2026-04-29
 */

#ifndef WIRE_H
#define WIRE_H

#include <stdint.h>
#include <stddef.h>
#include "Stream.h"

// I2C SDK configuration - align with ws63_liteos_app.config
// CONFIG_I2C_SUPPORT_MASTER=y is set in the config
// CONFIG_I2C_SUPPORT_SLAVE and CONFIG_I2C_SUPPORT_INT are not set by default
// We define MASTER to ensure uapi_i2c_master_* functions are available
#ifndef CONFIG_I2C_SUPPORT_MASTER
#define CONFIG_I2C_SUPPORT_MASTER 1
#endif

#include "driver/i2c.h"

// I2C buffer sizes
#ifndef WIRE_RX_BUFFER_SIZE
#define WIRE_RX_BUFFER_SIZE 32
#endif

#ifndef WIRE_TX_BUFFER_SIZE
#define WIRE_TX_BUFFER_SIZE 32
#endif

// I2C status codes (Arduino-compatible)
#define I2C_SUCCESS 0x00
#define I2C_ERROR_DATA_TOO_LONG 0x01
#define I2C_ERROR_NACK_ON_TRANSMIT 0x02
#define I2C_ERROR_NACK_ON_ADDRESS 0x03
#define I2C_ERROR_OTHER 0x04

// Callback function types for Slave mode
typedef void (*i2c_receive_callback_t)(int howMany);
typedef void (*i2c_request_callback_t)(void);

class TwoWire : public Stream {
private:
    i2c_bus_t _i2c_bus;
    bool _initialized;
    bool _master_mode;

    // TX buffer (for beginTransmission / slave onRequest)
    uint8_t *_tx_buffer;
    uint16_t _tx_index;
    uint16_t _tx_length;

    // RX buffer (for requestFrom / slave onReceive)
    uint8_t *_rx_buffer;
    uint16_t _rx_index;
    uint16_t _rx_length;

    // Slave address (used for both master target and slave own address)
    uint16_t _slave_address;

    // Clock frequency
    uint32_t _clock_freq;

    // Callbacks (for Slave mode)
    i2c_receive_callback_t _onReceiveCallback;
    i2c_request_callback_t _onRequestCallback;

    // IRQ event flags (set from ISR, read by poll() in main thread)
    volatile uint8_t _irq_rx_done;
    volatile uint8_t _irq_tx_done;
    volatile uint8_t _irq_error;

    // Transfer status
    uint8_t _transmit_status;

    // Private methods
    void _init_buffers();
    errcode_t _i2c_master_write(uint8_t addr, const uint8_t *data, size_t len);
    errcode_t _i2c_master_read(uint8_t addr, uint8_t *data, size_t len);

    // SDK IRQ callback handler (static for C linkage)
    // Only active when CONFIG_I2C_SUPPORT_INT is enabled
    static void _i2c_irq_callback(i2c_bus_t bus, uint8_t event);

public:
    TwoWire(i2c_bus_t i2c_bus = I2C_BUS_0);
    ~TwoWire();

    // Initialize I2C
    void begin();                // Master mode
    void begin(uint8_t address); // Slave mode with address
    void begin(int address);     // Slave mode (compatibility)
    void end();

    // Clock configuration
    void setClock(uint32_t frequency);
    void setClock(uint16_t frequency); // Compatibility overload

    // Master mode: Transmission
    void beginTransmission(uint8_t address);
    void beginTransmission(int address); // Compatibility overload
    uint8_t endTransmission(void);
    uint8_t endTransmission(bool sendStop);

    // Master mode: Reception
    uint8_t requestFrom(uint8_t address, uint8_t quantity);
    uint8_t requestFrom(uint8_t address, uint8_t quantity, bool sendStop);
    uint8_t requestFrom(uint8_t address, uint8_t quantity, uint8_t stop);
    uint8_t requestFrom(int address, int quantity);
    uint8_t requestFrom(int address, int quantity, int stop);

    // Write methods (Stream/Print interface)
    // In master mode: queues bytes for endTransmission
    // In slave mode: fills TX buffer for onRequest callback
    size_t write(uint8_t data);
    size_t write(const uint8_t *data, size_t quantity);
    size_t write(const char *str);

    // Read methods (Stream interface)
    int available() const;
    int read();
    int peek() const;
    void flush();

    // Slave mode: Callbacks
    // When CONFIG_I2C_SUPPORT_SLAVE enabled: triggered by poll() or IRQ
    // When disabled: callbacks stored but not hardware-triggered
    void onReceive(i2c_receive_callback_t function);
    void onRequest(i2c_request_callback_t function);

    // Slave mode: Polling
    // Call this in loop() for slave operation
    // When CONFIG_I2C_SUPPORT_SLAVE enabled:
    //   - Checks for master write (slave receive) via uapi_i2c_slave_read()
    //   - Handles master read (slave request) via uapi_i2c_slave_write()
    //   - Triggers registered callbacks
    // When disabled: no-op
    void poll();

    // Status
    bool isInitialized() const
    {
        return _initialized;
    }
    bool isMaster() const
    {
        return _master_mode;
    }
    i2c_bus_t getI2cBus() const
    {
        return _i2c_bus;
    }

    // Operator overloads
    operator bool() const
    {
        return _initialized;
    }
};

// Global Wire instance
extern TwoWire Wire;

// Optional: Second I2C bus on some boards
#if I2C_BUS_MAX_NUMBER > 1
extern TwoWire Wire1;
#endif

#endif // WIRE_H
