/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/* *
 * @file Wire.cpp
 * @brief TwoWire class implementation for I2C communication - Direct SDK call
 * @version 3.0
 * @date 2026-04-29
 *
 * Slave mode implementation (gated by CONFIG_I2C_SUPPORT_SLAVE):
 * - Wire.begin(address) initializes I2C as slave via uapi_i2c_slave_init()
 * - onReceive callback triggered when master writes to us
 * - onRequest callback triggered when master reads from us
 * - poll() method for polling-based slave operation
 * - When CONFIG_I2C_SUPPORT_SLAVE is not enabled, slave APIs provide
 * compatibility stubs (callbacks stored but not triggered by hardware)
 *
 */

#include "Wire.h"
#include <string.h>

// Static instance pointer table for IRQ callback routing
// Maps i2c_bus_t index to the corresponding TwoWire instance
// Indexed by bus number; registered in begin(), unregistered in end()
static TwoWire *g_wire_instances[I2C_BUS_MAX_NUMBER] = {nullptr};

// I2C buffer sizes
#ifndef WIRE_RX_BUFFER_SIZE
#define WIRE_RX_BUFFER_SIZE 32
#endif

#ifndef WIRE_TX_BUFFER_SIZE
#define WIRE_TX_BUFFER_SIZE 32
#endif

// TwoWire Constructor
TwoWire::TwoWire(i2c_bus_t i2c_bus)
    : _i2c_bus(i2c_bus),
      _initialized(false),
      _master_mode(true),
      _tx_buffer(nullptr),
      _tx_index(0),
      _tx_length(0),
      _rx_buffer(nullptr),
      _rx_index(0),
      _rx_length(0),
      _slave_address(0),
      _clock_freq(100000), // Default 100000(100kHz:Standard mode)
      _onReceiveCallback(nullptr),
      _onRequestCallback(nullptr),
      _irq_rx_done(0),
      _irq_tx_done(0),
      _irq_error(0),
      _transmit_status(I2C_SUCCESS)
{
    _init_buffers();
}

// TwoWire Destructor
TwoWire::~TwoWire()
{
    end();
    if (_tx_buffer) {
        delete[] _tx_buffer;
        _tx_buffer = nullptr;
    }
    if (_rx_buffer) {
        delete[] _rx_buffer;
        _rx_buffer = nullptr;
    }
}

// Initialize buffers
// Note: With -fno-exceptions, new returns nullptr on failure (no bad_alloc exception)
void TwoWire::_init_buffers()
{
    _tx_buffer = new uint8_t[WIRE_TX_BUFFER_SIZE];
    _rx_buffer = new uint8_t[WIRE_RX_BUFFER_SIZE];
    _tx_index = 0;
    _tx_length = 0;
    _rx_index = 0;
    _rx_length = 0;
}

// I2C Master write (internal helper)
errcode_t TwoWire::_i2c_master_write(uint8_t addr, const uint8_t *data, size_t len)
{
    i2c_data_t i2c_data;
    i2c_data.send_buf = (uint8_t *)data;
    i2c_data.send_len = (uint32_t)len;
    i2c_data.receive_buf = nullptr;
    i2c_data.receive_len = 0;

    return uapi_i2c_master_write(_i2c_bus, addr, &i2c_data);
}

// I2C Master read (internal helper)
errcode_t TwoWire::_i2c_master_read(uint8_t addr, uint8_t *data, size_t len)
{
    i2c_data_t i2c_data;
    i2c_data.send_buf = nullptr;
    i2c_data.send_len = 0;
    i2c_data.receive_buf = data;
    i2c_data.receive_len = (uint32_t)len;

    return uapi_i2c_master_read(_i2c_bus, addr, &i2c_data);
}

// I2C IRQ Callback (static for C linkage)
// Called from ISR context when I2C events occur
#if defined(CONFIG_I2C_SUPPORT_INT) && (CONFIG_I2C_SUPPORT_INT == 1)
void TwoWire::_i2c_irq_callback(i2c_bus_t bus, uint8_t event)
{
    // Find the TwoWire instance for this bus and dispatch to appropriate callback
    if (bus >= I2C_BUS_MAX_NUMBER) {
        return;
    }

    TwoWire *instance = g_wire_instances[bus];
    if (instance == nullptr) {
        return;
    }

    switch (event) {
        case I2C_IRQ_EVT_RX_DONE:
            // Master wrote data to us (slave receive)
            // Set flag for poll() to handle in main thread context
            instance->_irq_rx_done = 1;
            break;

        case I2C_IRQ_EVT_TX_DONE:
            // Master read data from us (slave request completed)
            instance->_irq_tx_done = 1;
            break;

        case I2C_IRQ_EVT_I2C_BUSY:
            // Bus busy event - no action needed
            break;

        case I2C_IRQ_EVT_I2C_ERR:
            // Error event - set flag for poll() to handle
            instance->_irq_error = 1;
            break;

        default:
            break;
    }
}
#else
void TwoWire::_i2c_irq_callback(i2c_bus_t bus, uint8_t event)
{
    (void)bus;
    (void)event;
    // IRQ support not enabled - callback not used
}
#endif

// Begin as Master (default)
void TwoWire::begin()
{
    if (_initialized) {
        end();
    }

    // Check buffer allocation
    if (!_tx_buffer || !_rx_buffer) {
        return; // Cannot initialize without buffers
    }

    // Register this instance for IRQ callback routing
    if (_i2c_bus < I2C_BUS_MAX_NUMBER) {
        g_wire_instances[_i2c_bus] = this;
    }

    // Initialize as Master with default clock
    errcode_t ret = uapi_i2c_master_init(_i2c_bus, _clock_freq, 0);
    if (ret == ERRCODE_SUCC) {
        _initialized = true;
        _master_mode = true;

// Register IRQ callback if interrupt support is enabled
#if defined(CONFIG_I2C_SUPPORT_INT) && (CONFIG_I2C_SUPPORT_INT == 1)
        uapi_i2c_register_irq_callback(_i2c_bus, _i2c_irq_callback);
#endif
    }
}

// Begin as Slave with address
void TwoWire::begin(uint8_t address)
{
    if (_initialized) {
        end();
    }

#if defined(CONFIG_I2C_SUPPORT_SLAVE) && (CONFIG_I2C_SUPPORT_SLAVE == 1)
    // Full slave mode initialization via SDK
    // Slave address range: 7-bit [0x08, 0x77], 10-bit [0x7800, 0x7BFF]
    errcode_t ret = uapi_i2c_slave_init(_i2c_bus, _clock_freq, (uint16_t)address);
    if (ret == ERRCODE_SUCC) {
        _initialized = true;
        _master_mode = false;
        _slave_address = address;

        // Register this instance for IRQ callback routing
        if (_i2c_bus < I2C_BUS_MAX_NUMBER) {
            g_wire_instances[_i2c_bus] = this;
        }

        // Reset buffers for slave operation
        _tx_index = 0;
        _tx_length = 0;
        _rx_index = 0;
        _rx_length = 0;

// Enable interrupt mode for slave if supported
// Slave mode benefits from interrupts for event notification
#if defined(CONFIG_I2C_SUPPORT_INT) && (CONFIG_I2C_SUPPORT_INT == 1)
        uapi_i2c_set_irq_mode(_i2c_bus, true);
        uapi_i2c_register_irq_callback(_i2c_bus, _i2c_irq_callback);
#endif
    } else {
        // Slave init failed - remain uninitialized
        _initialized = false;
    }
#else
    // Slave mode not supported by SDK config
    // Provide compatibility stub: mark as initialized in slave-like mode
    // Callbacks can be registered but poll() will be a no-op
    // This allows code to compile and run without hardware slave support
    _initialized = true;
    _master_mode = false;
    _slave_address = address;

    // Register this instance for IRQ callback routing (even in stub mode)
    if (_i2c_bus < I2C_BUS_MAX_NUMBER) {
        g_wire_instances[_i2c_bus] = this;
    }

    // Reset buffers
    _tx_index = 0;
    _tx_length = 0;
    _rx_index = 0;
    _rx_length = 0;
#endif
}

// Begin as Slave with address (int overload for compatibility)
void TwoWire::begin(int address)
{
    begin((uint8_t)address);
}

// End I2C
void TwoWire::end()
{
    if (!_initialized) {
        return;
    }

#if defined(CONFIG_I2C_SUPPORT_INT) && (CONFIG_I2C_SUPPORT_INT == 1)
    uapi_i2c_unregister_irq_callback(_i2c_bus);
#endif

    // Distinguish master/slave deinit
    if (_master_mode) {
        uapi_i2c_deinit(_i2c_bus);
    } else {
#if defined(CONFIG_I2C_SUPPORT_SLAVE) && (CONFIG_I2C_SUPPORT_SLAVE == 1)
        uapi_i2c_slave_deinit(_i2c_bus);
#else
        // Stub slave mode: no actual init was done, skip deinit
#endif
    }

    // Unregister this instance from IRQ callback routing
    if (_i2c_bus < I2C_BUS_MAX_NUMBER) {
        g_wire_instances[_i2c_bus] = nullptr;
    }

    _initialized = false;
    _master_mode = true; // Default back to master mode

    // Reset buffers and IRQ flags
    _tx_index = 0;
    _tx_length = 0;
    _rx_index = 0;
    _rx_length = 0;
    _irq_rx_done = 0;
    _irq_tx_done = 0;
    _irq_error = 0;
}

// Set clock frequency
void TwoWire::setClock(uint32_t frequency)
{
    _clock_freq = frequency;
    if (_initialized) {
        uapi_i2c_set_baudrate(_i2c_bus, frequency);
    }
}

// Set clock frequency (16-bit overload for compatibility)
void TwoWire::setClock(uint16_t frequency)
{
    setClock((uint32_t)frequency);
}

// Begin transmission to slave
void TwoWire::beginTransmission(uint8_t address)
{
    _tx_index = 0;
    _tx_length = 0;
    _slave_address = address;
    _transmit_status = I2C_SUCCESS;
}

// Begin transmission (int overload)
void TwoWire::beginTransmission(int address)
{
    beginTransmission((uint8_t)address);
}

// End transmission
uint8_t TwoWire::endTransmission(void)
{
    return endTransmission(true);
}

// End transmission with stop bit control
uint8_t TwoWire::endTransmission(bool sendStop)
{
    if (!_initialized || _tx_length == 0) {
        return I2C_ERROR_OTHER;
    }

    // The sendStop parameter needs support by I2C hardware for repeated start conditions.
    (void)sendStop;

    errcode_t ret = _i2c_master_write(_slave_address, _tx_buffer, _tx_length);

    // Reset TX buffer
    _tx_index = 0;
    _tx_length = 0;
    if (ret != ERRCODE_SUCC) {
        // Decode error
        if (ret == ERRCODE_I2C_TIMEOUT) {
            return I2C_ERROR_NACK_ON_TRANSMIT;
        }
        return I2C_ERROR_OTHER;
    }

    return I2C_SUCCESS;
}

// Request data from slave
uint8_t TwoWire::requestFrom(uint8_t address, uint8_t quantity)
{
    return requestFrom(address, quantity, true);
}

// Request data with stop bit control
uint8_t TwoWire::requestFrom(uint8_t address, uint8_t quantity, bool sendStop)
{
    return requestFrom(address, quantity, (uint8_t)(sendStop ? 1 : 0));
}

// Request data with stop bit control (uint8_t)
uint8_t TwoWire::requestFrom(uint8_t address, uint8_t quantity, uint8_t stop)
{
    if (!_initialized || quantity == 0 || quantity > WIRE_RX_BUFFER_SIZE) {
        return 0;
    }

    // Validate I2C address (7-bit address range: 0x08-0x77)
    if (address < 0x08 || address > 0x77) { // 7-bit address range: 0x08-0x77
        return 0;
    }

    errcode_t ret = _i2c_master_read(address, _rx_buffer, quantity);
    if (ret == ERRCODE_SUCC) {
        _rx_length = quantity;
        _rx_index = 0;
        return quantity;
    }

    // The stop parameter needs support by I2C hardware for repeated start conditions.
    (void)stop;
    return 0;
}

// Request data (int overloads for compatibility)
uint8_t TwoWire::requestFrom(int address, int quantity)
{
    if (address < 0 || quantity < 0) {
        return 0;
    }
    return requestFrom((uint8_t)address, (uint8_t)quantity, true);
}

uint8_t TwoWire::requestFrom(int address, int quantity, int stop)
{
    if (address < 0 || quantity < 0) {
        return 0;
    }
    return requestFrom((uint8_t)address, (uint8_t)quantity, (uint8_t)(stop ? 1 : 0));
}

// Write a single byte
size_t TwoWire::write(uint8_t data)
{
    if (_tx_length >= WIRE_TX_BUFFER_SIZE) {
        return 0; // Buffer full
    }

    _tx_buffer[_tx_length++] = data;
    return 1;
}

// Write a buffer
size_t TwoWire::write(const uint8_t *data, size_t quantity)
{
    if (data == nullptr || quantity == 0) {
        return 0;
    }

    size_t written = 0;
    for (size_t i = 0; i < quantity; i++) {
        if (write(data[i])) {
            written++;
        } else {
            break; // Buffer full
        }
    }
    return written;
}

// Write a string
size_t TwoWire::write(const char *str)
{
    if (str == nullptr) {
        return 0;
    }
    return write((const uint8_t *)str, strlen(str));
}

// Check available bytes in RX buffer
int TwoWire::available() const
{
    return _rx_length - _rx_index;
}

// Read a byte from RX buffer
int TwoWire::read()
{
    if (_rx_index >= _rx_length) {
        return -1; // Buffer empty
    }
    return _rx_buffer[_rx_index++];
}

// Peek at next byte
int TwoWire::peek() const
{
    if (_rx_index >= _rx_length) {
        return -1;
    }
    return _rx_buffer[_rx_index];
}

// Flush (no-op for I2C, Stream interface requirement)
void TwoWire::flush()
{
    // I2C doesn't have a flush operation
    // This is required by the Stream interface
}

// Set receive callback (Slave mode)
// When CONFIG_I2C_SUPPORT_SLAVE is enabled: callbacks triggered by poll() or IRQ
// When disabled: callbacks stored but never triggered by hardware
void TwoWire::onReceive(i2c_receive_callback_t function)
{
    _onReceiveCallback = function;
}

// Set request callback (Slave mode)
void TwoWire::onRequest(i2c_request_callback_t function)
{
    _onRequestCallback = function;
}

// Slave mode polling - call this in loop() for slave operation
// When CONFIG_I2C_SUPPORT_SLAVE is enabled:
//   - Checks for incoming data from master via uapi_i2c_slave_read()
//   - Triggers onReceive callback when data is received
//   - Triggers onRequest callback when master wants to read
// When disabled: no-op
void TwoWire::poll()
{
    if (!_initialized || _master_mode) {
        return; // Only valid in slave mode
    }

#if defined(CONFIG_I2C_SUPPORT_SLAVE) && (CONFIG_I2C_SUPPORT_SLAVE == 1)
    // Handle IRQ error flag (safe to reset state in main thread)
    if (_irq_error) {
        _irq_error = 0;
        _rx_length = 0;
        _rx_index = 0;
        _tx_length = 0;
        _tx_index = 0;
    }

    // Handle IRQ RX done flag — master wrote data, process in main thread
    if (_irq_rx_done) {
        _irq_rx_done = 0;
        if (_onReceiveCallback != nullptr && _rx_length > 0) {
            _onReceiveCallback((int)_rx_length);
        }
        // Reset RX state after callback
        _rx_length = 0;
        _rx_index = 0;
    }

    // Handle IRQ TX done flag — master completed reading from us
    if (_irq_tx_done) {
        _irq_tx_done = 0;
        if (_onRequestCallback != nullptr) {
            _onRequestCallback();
        }
        // Reset TX state after callback
        _tx_index = 0;
        _tx_length = 0;
    }

    // --- Slave Receive: Check if master wrote data to us (polling mode) ---
    if (_onReceiveCallback != nullptr) {
        // Attempt to read data from master (non-blocking poll)
        // Prepare RX buffer for incoming data
        i2c_data_t i2c_data;
        i2c_data.receive_buf = _rx_buffer;
        i2c_data.receive_len = WIRE_RX_BUFFER_SIZE;
        i2c_data.send_buf = nullptr;
        i2c_data.send_len = 0;

        errcode_t ret = uapi_i2c_slave_read(_i2c_bus, &i2c_data);
        if (ret == ERRCODE_SUCC && i2c_data.receive_len > 0) {
            // Data received from master
            _rx_length = (uint16_t)i2c_data.receive_len;
            _rx_index = 0;

            // Trigger the receive callback
            _onReceiveCallback((int)_rx_length);

            // Reset RX state after callback
            _rx_length = 0;
            _rx_index = 0;
        }
        // If no data or error, simply continue (no data available)
    }

    // --- Slave Transmit: Handle master read requests ---
    // NOTE: We do NOT proactively call uapi_i2c_slave_write() here.
    // Slave transmit should only occur when the master initiates a read.
    // On this SDK, there is no callback-driven slave TX API, so we
    // only handle slave receive (master-write-to-us) via polling.
    // If the SDK adds a slave-read-event callback in the future,
    // the onRequest callback should be invoked from that callback,
    // and slave_write called at that time.
    // For now, the onRequest callback is stored but not triggered by poll().
    // Users needing slave TX should use interrupt-driven mode
    // (CONFIG_I2C_SUPPORT_INT) where the I2C_IRQ_EVT_TX_DONE event
    // indicates master completed a read.
#else
    // Slave mode not supported by SDK config
    // poll() is a no-op in this configuration
    // Users must enable CONFIG_I2C_SUPPORT_SLAVE for full slave support
#endif
}

// Global Wire instance
TwoWire Wire(I2C_BUS_0);

#if I2C_BUS_MAX_NUMBER > 1
TwoWire Wire1(I2C_BUS_1);
#endif
