/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/* *
 * @file SPI.h
 * @brief SPIClass for SPI communication - Full SDK integration with stub fallback
 * @version 3.0
 * @date 2026-04-29
 */

#ifndef SPI_ARDUINO_H
#define SPI_ARDUINO_H

#include <stdint.h>
#include <stddef.h>

// SPI Bit Order
#ifndef LSBFIRST
#define LSBFIRST 0
#endif
#ifndef MSBFIRST
#define MSBFIRST 1
#endif

// SPI Data Mode
#ifndef SPI_MODE0
#define SPI_MODE0 0x00 // CPOL=0, CPHA=0
#endif
#ifndef SPI_MODE1
#define SPI_MODE1 0x04 // CPOL=0, CPHA=1
#endif
#ifndef SPI_MODE2
#define SPI_MODE2 0x08 // CPOL=1, CPHA=0
#endif
#ifndef SPI_MODE3
#define SPI_MODE3 0x0C // CPOL=1, CPHA=1
#endif

// SPI Clock Divider (based on 32MHz base clock)
#define SPI_CLOCK_DIV2 2
#define SPI_CLOCK_DIV4 4
#define SPI_CLOCK_DIV8 8
#define SPI_CLOCK_DIV16 16
#define SPI_CLOCK_DIV32 32
#define SPI_CLOCK_DIV64 64
#define SPI_CLOCK_DIV128 128

#define SPI_DEFAULT_CLOCK 4000000UL
#define SPI_MAX_CLOCK 32000000UL  // Max SPI clock (based on 32MHz base)

// SPISettings class for transaction configuration
class SPISettings {
private:
    uint32_t _clock;
    uint8_t _bitOrder;
    uint8_t _dataMode;

public:
    SPISettings(uint32_t clock, uint8_t bitOrder, uint8_t dataMode)
        : _clock(clock), _bitOrder(bitOrder), _dataMode(dataMode)
    {}

    SPISettings() : _clock(SPI_DEFAULT_CLOCK), _bitOrder(MSBFIRST), _dataMode(SPI_MODE0) {}

    uint32_t getClock() const
    {
        return _clock;
    }
    uint8_t getBitOrder() const
    {
        return _bitOrder;
    }
    uint8_t getDataMode() const
    {
        return _dataMode;
    }

    friend class SPIClass;
};

// Default SPI settings
#define SPI_DEFAULT_SETTINGS SPISettings(SPI_DEFAULT_CLOCK, MSBFIRST, SPI_MODE0)

// Forward declaration for spi_slave_t (defined in spi_porting.h)
// When CONFIG_SPI_SUPPORT_MASTER is not enabled, define a compatible type
#if defined(CONFIG_SPI_SUPPORT_MASTER) && (CONFIG_SPI_SUPPORT_MASTER == 1)
#include "driver/spi.h"
#endif

class SPIClass {
private:
    bool _initialized;
    bool _in_transaction;

    // Current configuration
    uint32_t _clock_freq;
    uint8_t _bit_order;
    uint8_t _data_mode;
    uint8_t _cs_pin;
#if defined(CONFIG_SPI_SUPPORT_MASTER) && (CONFIG_SPI_SUPPORT_MASTER == 1)
    spi_slave_t _selected_slave;
#endif

    // Saved settings during transaction (for potential restore)
    uint32_t _saved_clock;
    uint8_t _saved_bit_order;
    uint8_t _saved_data_mode;

    // Interrupt protection
    bool _using_interrupt;
    uint8_t _interrupt_num;

public:
    SPIClass();
    ~SPIClass();

    // Initialize/Deinitialize
    void begin();
    void begin(uint8_t cs_pin);
    void end();

    // Transaction management
    void beginTransaction(SPISettings settings);
    void endTransaction();

    // Data transfer
    // When CONFIG_SPI_SUPPORT_MASTER is enabled: real hardware transfer
    // When disabled: loopback mode (transfer returns input data)
    uint8_t transfer(uint8_t data);
    uint16_t transfer16(uint16_t data);
    void transfer(void *buf, size_t count);

    // Configuration
    void setBitOrder(uint8_t bitOrder);
    void setDataMode(uint8_t mode);
    void setClockDivider(uint8_t div);
    void setClock(uint32_t clockFreq);
    void setFrequency(uint32_t freq);

    // Interrupt protection
    void usingInterrupt(uint8_t interruptNum);
    void notUsingInterrupt(uint8_t interruptNum);

    // Chip Select / Slave selection
    // When CONFIG_SPI_SUPPORT_MASTER is enabled: selects slave via SDK
    // When disabled: stores slave index only
    void selectSlave(uint8_t cs);

    // Status
    bool isInitialized() const
    {
        return _initialized;
    }

    // Operator overload
    operator bool() const
    {
        return _initialized;
    }
};

// Global SPI instance
extern SPIClass SPI;

#endif // SPI_ARDUINO_H
