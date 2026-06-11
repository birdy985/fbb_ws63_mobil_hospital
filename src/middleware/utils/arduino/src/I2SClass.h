/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/* *
 * @file I2SClass.h
 * @brief I2S library for Arduino compatibility layer
 * @version 1.1
 * @date 2026-04-29
 *
 * I2S (Inter-IC Sound) interface for audio data transmission.
 * Supports MASTER mode with configurable sample rate, bit depth, and channels.
 * DMA support is conditional on CONFIG_I2S_SUPPORT_DMA.
 */

#ifndef I2SCLASS_H
#define I2SCLASS_H

#include <stdint.h>
#include <stdbool.h>
#include "Stream.h"

// Forward declare I2S types - actual definitions come from SDK when included in cpp
// We use void* for callback type to avoid header dependency issues in Arduino.h context
typedef void (*i2s_callback_forward_t)(uint32_t *left_buff, uint32_t *right_buff, uint32_t length);

// I2S Pin Definitions (ws63 specific)
#define I2S_SCK_PIN 0   // Serial Clock
#define I2S_WS_PIN 1    // Word Select / LRCK
#define I2S_SD_PIN 2    // Serial Data Out
#define I2S_SD_IN_PIN 3 // Serial Data In (for full-duplex)

// I2S Standard Rates
#define I2S_FREQ_8K 8000
#define I2S_FREQ_11K 11025
#define I2S_FREQ_12K 12000
#define I2S_FREQ_16K 16000
#define I2S_FREQ_22K 22050
#define I2S_FREQ_24K 24000
#define I2S_FREQ_32K 32000
#define I2S_FREQ_44K 44100
#define I2S_FREQ_48K 48000
#define I2S_FREQ_88K 88200
#define I2S_FREQ_96K 96000
#define I2S_FREQ_176K 176400
#define I2S_FREQ_192K 192000

// I2S Data Width
#define I2S_BITS_16 16
#define I2S_BITS_18 18
#define I2S_BITS_20 20
#define I2S_BITS_24 24
#define I2S_BITS_32 32

// I2S Channels
#define I2S_MONO 1
#define I2S_STEREO 2
#define I2S_CHANNELS_MAX 8

// I2S Sample Rate Limits
#define I2S_MIN_SAMPLE_RATE 8000
#define I2S_MAX_SAMPLE_RATE 192000

/* *
 * @class I2SClass
 * @brief Arduino-compatible I2S interface class
 *
 * Provides I2S audio interface for ws63 chip.
 * Inherits from Stream for Arduino compatibility.
 *
 * DMA methods (writeDMA/readDMA) are always declared for API compatibility.
 * When CONFIG_I2S_SUPPORT_DMA is not enabled, they fallback to normal write/read.
 * isDMAEnabled() returns false when DMA is not supported at compile time.
 */
class I2SClass : public Stream {
public:
    I2SClass() noexcept;
    ~I2SClass();

    bool begin(uint32_t sample_rate, uint8_t bits_per_sample = I2S_BITS_16, uint8_t channels = I2S_STEREO);
    void end();
    void setPins(int sck, int ws, int sd, int sdin = -1);

    // ========== Stream Interface ==========
    int available() const override;
    int read() override;
    size_t read(uint8_t *buffer, size_t size);
    int peek() const override;
    size_t write(uint8_t data) override;

    /*
    * I2S 是 32-bit 总线，size需要按照"数据长度 * sizeof(uint32_t)"计数
    */
    size_t write(const uint8_t *buffer, size_t size) override;
    void flush() override;

    // ========== Configuration Methods ==========
    void setSampleRate(uint32_t rate);
    uint32_t getSampleRate() const;
    void setBitsPerSample(uint8_t bits);
    uint8_t getBitsPerSample() const;
    void setChannels(uint8_t channels);
    uint8_t getChannels() const;
    void setMaster(bool master);
    bool isMaster() const;

    // ========== DMA Support ==========
    /* *
     * @brief Enable or disable DMA for I2S transfer
     * @param enable true to enable DMA, false to disable
     * When CONFIG_I2S_SUPPORT_DMA is not enabled, this only records the flag
     * without operating hardware.
     */
    void setDMA(bool enable);

    /* *
     * @brief Check if DMA is enabled
     * @return true if DMA enabled AND supported at compile time, false otherwise
     */
    bool isDMAEnabled() const;

    /* *
     * @brief Write data using DMA
     * @param buffer Pointer to data buffer
     * @param size Number of bytes to write
     * @return Number of bytes written
     * When CONFIG_I2S_SUPPORT_DMA is not enabled, falls back to normal write().
     */
    size_t writeDMA(const uint8_t *buffer, size_t size);

    /* *
     * @brief Read data using DMA
     * @param buffer Pointer to receive buffer
     * @param size Number of bytes to read
     * @return Number of bytes read
     * When CONFIG_I2S_SUPPORT_DMA is not enabled, falls back to normal read().
     */
    size_t readDMA(uint8_t *buffer, size_t size);

    // ========== Other Methods ==========
    void setCallback(void (*callback)(uint32_t *left, uint32_t *right, uint32_t length));
    bool isInitialized() const;
    size_t getBufferSize() const;
    void setBufferSize(size_t size);
    bool isTransmitting() const;
    bool isReceiving() const;
    void startTransmit();
    void stopTransmit();
    void startReceive();
    void stopReceive();

private:
    bool m_initialized;
    bool m_is_master;
    bool m_dma_enabled;
    bool m_transmitting;
    bool m_receiving;
    uint32_t m_sample_rate;
    uint8_t m_bits_per_sample;
    uint8_t m_channels;
    int m_sck_pin;
    int m_ws_pin;
    int m_sd_pin;
    int m_sdin_pin;
    size_t m_buffer_size;
    i2s_callback_forward_t m_rxCallback;

    uint8_t getSampleRateEnum(uint32_t rateHz) const;
    uint8_t getDataWidthEnum(uint8_t bits) const;
    uint8_t getChannelsEnum(uint8_t channels) const;
};

// Global I2S instance
extern I2SClass I2S;

// C-style API for compatibility
bool i2s_begin(uint32_t sample_rate, uint8_t bits = I2S_BITS_16, uint8_t channels = I2S_STEREO);
void i2s_end();
size_t i2s_write(const uint8_t *buffer, size_t size);
int i2s_available();
int i2s_read();

#endif // I2SCLASS_H
