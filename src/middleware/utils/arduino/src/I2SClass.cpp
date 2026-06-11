/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/* *
 * @file I2SClass.cpp
 * @brief I2S library implementation for Arduino compatibility layer
 * @version 1.1
 * @date 2026-04-29
 *
 * I2S (Inter-IC Sound) interface for audio data transmission.
 * Full SDK integration with I2S driver.
 * DMA support is conditional on CONFIG_I2S_SUPPORT_DMA.
 * When DMA is not compiled in, writeDMA/readDMA fallback to write/read,
 * and isDMAEnabled() returns false.
 */

#include "I2SClass.h"
#include <string.h>
#include "securec.h"

#include "i2s.h"
#include "sio_porting.h"
#include "hal_sio.h"

#if defined(CONFIG_I2S_SUPPORT_DMA) && (CONFIG_I2S_SUPPORT_DMA == 1)
#include "dma.h"
#endif

// I2S buffer size default (128 samples)
#define I2S_DEFAULT_BUFFER_SIZE 128
#define I2S_DEFAULT_DIV_NUMBER 32
#define I2S_DMA_INT_THRESHOLD 16
#define I2S_DMA_WIDTH_32BIT 2
#define I2S_DMA_BURST_LENGTH_1 0
#define I2S_DMA_BURST_LENGTH_2 2
#define I2S_DMA_PRIORITY_DEFAULT 2
#define I2S_SAMPLE_RATE_ENUM_44K 7
#define I2S_DATA_WIDTH_ENUM_16BIT 1
#define I2S_DATA_WIDTH_ENUM_18BIT 2
#define I2S_DATA_WIDTH_ENUM_20BIT 3
#define I2S_DATA_WIDTH_ENUM_24BIT 4
#define I2S_DATA_WIDTH_ENUM_32BIT 5
#define I2S_CHANNELS_MAX_4CH 4
#define I2S_CHANNELS_MAX_8CH 8
#define I2S_CHANNELS_ENUM_2CH 0
#define I2S_CHANNELS_ENUM_4CH 1
#define I2S_CHANNELS_ENUM_8CH 2
#define I2S_CHANNELS_ENUM_16CH 3

// I2S RX buffer for interrupt-driven reception
static volatile size_t g_i2s_rx_available = 0;
static uint32_t g_i2s_rx_left_buffer[CONFIG_DATA_LEN_MAX];
static uint32_t g_i2s_rx_right_buffer[CONFIG_DATA_LEN_MAX];

// I2S TX buffer
static volatile size_t i2s_tx_available = 0;

// I2S RX callback
static void i2s_rx_callback(uint32_t *left_buff, uint32_t *right_buff, uint32_t length)
{
    if (length <= CONFIG_DATA_LEN_MAX) {
        (void)memcpy_s(g_i2s_rx_left_buffer, sizeof(g_i2s_rx_left_buffer), left_buff, length * sizeof(uint32_t));
        (void)memcpy_s(g_i2s_rx_right_buffer, sizeof(g_i2s_rx_right_buffer), right_buff, length * sizeof(uint32_t));
        g_i2s_rx_available = length;
    }
}

// Helper functions for mapping Arduino-style params to SDK enums
__attribute__((unused)) static hal_sio_data_width_t map_data_width(uint8_t bits)
{
    switch (bits) {
        case I2S_BITS_16:
            return SIXTEEN_BIT;
        case I2S_BITS_18:
            return EIGHTEEN_BIT;
        case I2S_BITS_20:
            return TWENTY_BIT;
        case I2S_BITS_24:
            return TWENTY_FOUR_BIT;
        case I2S_BITS_32:
            return THIRTY_TWO_BIT;
        default:
            return SIXTEEN_BIT;
    }
}

static hal_sio_channel_number_t __attribute__((unused)) map_channels(uint8_t channels)
{
    if (channels <= I2S_STEREO)
        return TWO_CH;
    if (channels <= I2S_CHANNELS_MAX_4CH)
        return FOUR_CH;
    if (channels <= I2S_CHANNELS_MAX_8CH)
        return EIGHT_CH;
    return SIXTEEN_CH;
}

__attribute__((unused)) static i2s_sample_rate_t map_sample_rate(uint32_t rate)
{
    if (rate <= I2S_FREQ_8K)
        return I2S_SAMPLE_RATE_8K;
    if (rate <= I2S_FREQ_11K)
        return I2S_SAMPLE_RATE_11K;
    if (rate <= I2S_FREQ_12K)
        return I2S_SAMPLE_RATE_12K;
    if (rate <= I2S_FREQ_16K)
        return I2S_SAMPLE_RATE_16K;
    if (rate <= I2S_FREQ_22K)
        return I2S_SAMPLE_RATE_22K;
    if (rate <= I2S_FREQ_24K)
        return I2S_SAMPLE_RATE_24K;
    if (rate <= I2S_FREQ_32K)
        return I2S_SAMPLE_RATE_32K;
    if (rate <= I2S_FREQ_44K)
        return I2S_SAMPLE_RATE_44K;
    if (rate <= I2S_FREQ_48K)
        return I2S_SAMPLE_RATE_48K;
    if (rate <= I2S_FREQ_88K)
        return I2S_SAMPLE_RATE_88K;
    if (rate <= I2S_FREQ_96K)
        return I2S_SAMPLE_RATE_96K;
    if (rate <= I2S_FREQ_176K)
        return I2S_SAMPLE_RATE_176K;
    return I2S_SAMPLE_RATE_192K;
}

// Constructor
I2SClass::I2SClass() noexcept
    : m_initialized(false),
      m_is_master(true),
      m_dma_enabled(false),
      m_transmitting(false),
      m_receiving(false),
      m_sample_rate(I2S_FREQ_44K),
      m_bits_per_sample(I2S_BITS_16),
      m_channels(I2S_STEREO),
      m_sck_pin(I2S_SCK_PIN),
      m_ws_pin(I2S_WS_PIN),
      m_sd_pin(I2S_SD_PIN),
      m_sdin_pin(I2S_SD_IN_PIN),
      m_buffer_size(I2S_DEFAULT_BUFFER_SIZE),
      m_rxCallback(i2s_rx_callback)
{
}

// Destructor
I2SClass::~I2SClass()
{
    end();
}

// Initialize I2S
bool I2SClass::begin(uint32_t sample_rate, uint8_t bits_per_sample, uint8_t channels)
{
    // Validate I2S parameters
    if (sample_rate < I2S_MIN_SAMPLE_RATE || sample_rate > I2S_MAX_SAMPLE_RATE) {
        return false;
    }

    if (bits_per_sample != I2S_BITS_16 && bits_per_sample != I2S_BITS_18 && bits_per_sample != I2S_BITS_20 &&
        bits_per_sample != I2S_BITS_24 && bits_per_sample != I2S_BITS_32) {
        return false;
    }

    if (channels < 1 || channels > I2S_CHANNELS_MAX) {
        return false;
    }

    if (m_initialized) {
        end();
    }

    m_sample_rate = sample_rate;
    m_bits_per_sample = bits_per_sample;
    m_channels = channels;

#if defined(CONFIG_I2S_SUPPORT) && (CONFIG_I2S_SUPPORT == 1)
    i2s_config_t config;
    config.drive_mode = m_is_master ? MASTER : SLAVE;
    config.transfer_mode = STD_MODE;
    config.data_width = map_data_width(bits_per_sample);
    config.channels_num = map_channels(channels);
    config.timing = NONE_TIMING_MODE;
    config.clk_edge = FALLING_EDGE;
    config.div_number = I2S_DEFAULT_DIV_NUMBER;
    config.number_of_channels = channels;

    errcode_t ret = uapi_i2s_init(SIO_BUS_0, m_rxCallback);
    if (ret != ERRCODE_SUCC) {
        return false;
    }

    ret = uapi_i2s_set_config(SIO_BUS_0, &config);
    if (ret != ERRCODE_SUCC) {
        uapi_i2s_deinit(SIO_BUS_0);
        return false;
    }

    uapi_i2s_set_crg_clock_enable(SIO_BUS_0, true);

#if defined(CONFIG_I2S_SUPPORT_DMA) && (CONFIG_I2S_SUPPORT_DMA == 1)
    // Configure DMA if enabled
    if (m_dma_enabled) {
        uapi_dma_deinit();
        uapi_dma_init();
        uapi_dma_open();

        i2s_dma_attr_t dma_attr;
        dma_attr.tx_dma_enable = true;
        dma_attr.tx_int_threshold = I2S_DMA_INT_THRESHOLD;
        dma_attr.rx_dma_enable = true;
        dma_attr.rx_int_threshold = I2S_DMA_INT_THRESHOLD;

        uapi_i2s_dma_config(SIO_BUS_0, &dma_attr);
    }
#endif

    m_initialized = true;
    return true;
#else
    m_initialized = true;
    return true;
#endif
}

// Deinitialize I2S
void I2SClass::end()
{
    if (!m_initialized) {
        return;
    }

#if defined(CONFIG_I2S_SUPPORT) && (CONFIG_I2S_SUPPORT == 1)
    uapi_i2s_set_crg_clock_enable(SIO_BUS_0, false);
    uapi_i2s_deinit(SIO_BUS_0);

#if defined(CONFIG_I2S_SUPPORT_DMA) && (CONFIG_I2S_SUPPORT_DMA == 1)
    if (m_dma_enabled) {
        uapi_dma_close();
        uapi_dma_deinit();
        m_dma_enabled = false;
    }
#endif
#endif

    m_initialized = false;
    m_transmitting = false;
    m_receiving = false;
    g_i2s_rx_available = 0;
    i2s_tx_available = 0;
}

// Set I2S pins
void I2SClass::setPins(int sck, int ws, int sd, int sdin)
{
    m_sck_pin = sck;
    m_ws_pin = ws;
    m_sd_pin = sd;
    m_sdin_pin = sdin;
}

// ========== Stream Interface ==========

int I2SClass::available() const
{
    if (!m_initialized || !m_receiving) {
        return 0;
    }
#if defined(CONFIG_I2S_SUPPORT) && (CONFIG_I2S_SUPPORT == 1)
    i2s_rx_data_t rx_data;
    errcode_t ret = uapi_i2s_get_data(SIO_BUS_0, &rx_data);
    if (ret == ERRCODE_SUCC) {
        g_i2s_rx_available = rx_data.length;
        if (g_i2s_rx_available > 0 && g_i2s_rx_available <= CONFIG_DATA_LEN_MAX) {
            memcpy(g_i2s_rx_left_buffer, rx_data.left_buff, g_i2s_rx_available * sizeof(uint32_t));
            memcpy(g_i2s_rx_right_buffer, rx_data.right_buff, g_i2s_rx_available * sizeof(uint32_t));
        }
    }
#endif

    return (int)g_i2s_rx_available;
}

int I2SClass::read()
{
    if (!m_initialized || !m_receiving) {
        return -1;
    }

#if defined(CONFIG_I2S_SUPPORT) && (CONFIG_I2S_SUPPORT == 1)
    if (g_i2s_rx_available == 0) {
        return -1;
    }

    uint32_t sample = g_i2s_rx_left_buffer[0];
    g_i2s_rx_available = 0;
    return (int)sample;
#else
    return -1;
#endif
}

size_t I2SClass::read(uint8_t *buffer, size_t size)
{
    if (!m_initialized || !m_receiving) {
        return 0;
    }

    if (buffer == nullptr || size == 0) {
        return 0;
    }

#if defined(CONFIG_I2S_SUPPORT) && (CONFIG_I2S_SUPPORT == 1)
#if defined(CONFIG_I2S_SUPPORT_DMA) && (CONFIG_I2S_SUPPORT_DMA == 1)
    if (m_dma_enabled) {
        i2s_dma_config_t dma_cfg;
        dma_cfg.src_width = I2S_DMA_WIDTH_32BIT;
        dma_cfg.dest_width = I2S_DMA_WIDTH_32BIT;
        dma_cfg.burst_length = I2S_DMA_BURST_LENGTH_2;
        dma_cfg.priority = I2S_DMA_PRIORITY_DEFAULT;

        int32_t ret = uapi_i2s_merge_read_by_dma(SIO_BUS_0, buffer, size, &dma_cfg, 0, true);
        return (ret == ERRCODE_SUCC) ? size : 0;
    }
#endif

    // Polling mode fallback
    if (g_i2s_rx_available == 0) {
        i2s_rx_data_t rx_data;
        errcode_t ret = uapi_i2s_get_data(SIO_BUS_0, &rx_data);
        if (ret == ERRCODE_SUCC) {
            g_i2s_rx_available = rx_data.length;
            if (g_i2s_rx_available > 0 && g_i2s_rx_available <= CONFIG_DATA_LEN_MAX) {
                memcpy(g_i2s_rx_left_buffer, rx_data.left_buff, g_i2s_rx_available * sizeof(uint32_t));
                memcpy(g_i2s_rx_right_buffer, rx_data.right_buff, g_i2s_rx_available * sizeof(uint32_t));
            }
        }
    }

    if (g_i2s_rx_available == 0) {
        return 0;
    }

    size_t bytes_to_copy = g_i2s_rx_available * sizeof(uint32_t);
    if (bytes_to_copy > size) {
        bytes_to_copy = size;
    }
    (void)memcpy_s(buffer, size, g_i2s_rx_left_buffer, bytes_to_copy);
    g_i2s_rx_available = 0;

    return bytes_to_copy;
#else
    return 0;
#endif
}

int I2SClass::peek() const
{
    return -1;
}

size_t I2SClass::write(uint8_t data)
{
    return write(&data, 1);
}

size_t I2SClass::write(const uint8_t *buffer, size_t size)
{
    if (!m_initialized || !m_transmitting) {
        return 0;
    }

    if (buffer == nullptr || size == 0) {
        return 0;
    }

#if defined(CONFIG_I2S_SUPPORT) && (CONFIG_I2S_SUPPORT == 1)
#if defined(CONFIG_I2S_SUPPORT_DMA) && (CONFIG_I2S_SUPPORT_DMA == 1)
    if (m_dma_enabled) {
        i2s_dma_config_t dma_cfg;
        dma_cfg.src_width = I2S_DMA_WIDTH_32BIT;
        dma_cfg.dest_width = I2S_DMA_WIDTH_32BIT;
        dma_cfg.burst_length = I2S_DMA_BURST_LENGTH_2;
        dma_cfg.priority = I2S_DMA_PRIORITY_DEFAULT;

        int32_t ret = uapi_i2s_merge_write_by_dma(SIO_BUS_0, buffer, size, &dma_cfg, 0, true);
        return (ret == ERRCODE_SUCC) ? size : 0;
    }
#endif

    // Polling mode fallback
    i2s_tx_data_t tx_data;
    tx_data.left_buff = (uint32_t *)buffer;
    tx_data.right_buff = (uint32_t *)buffer;
    tx_data.length = size / sizeof(uint32_t);

    errcode_t ret = uapi_i2s_write_data(SIO_BUS_0, &tx_data);
    return (ret == ERRCODE_SUCC) ? size : 0;
#else
    return size;
#endif
}

void I2SClass::flush() {}

// ========== Configuration Methods ==========

void I2SClass::setSampleRate(uint32_t rate)
{
    m_sample_rate = rate;

#if defined(CONFIG_I2S_SUPPORT) && (CONFIG_I2S_SUPPORT == 1) && defined(CONFIG_I2S_SUPPORT_DYNAMIC_SAMPLE_RATE)
    i2s_sample_rate_t sr = map_sample_rate(rate);
    uapi_i2s_set_sample_rate(SIO_BUS_0, sr);
#endif
}

uint32_t I2SClass::getSampleRate() const
{
    return m_sample_rate;
}

void I2SClass::setBitsPerSample(uint8_t bits)
{
    m_bits_per_sample = bits;
}

uint8_t I2SClass::getBitsPerSample() const
{
    return m_bits_per_sample;
}

void I2SClass::setChannels(uint8_t channels)
{
    m_channels = channels;
}

uint8_t I2SClass::getChannels() const
{
    return m_channels;
}

void I2SClass::setMaster(bool master)
{
    m_is_master = master;
}

bool I2SClass::isMaster() const
{
    return m_is_master;
}

// ========== DMA Support Methods ==========

void I2SClass::setDMA(bool enable)
{
    m_dma_enabled = enable;

#if defined(CONFIG_I2S_SUPPORT) && (CONFIG_I2S_SUPPORT == 1)
#if defined(CONFIG_I2S_SUPPORT_DMA) && (CONFIG_I2S_SUPPORT_DMA == 1)
    // DMA is supported at compile time: configure hardware when already initialized
    if (m_initialized) {
        if (enable) {
            // Initialize DMA subsystem
            uapi_dma_deinit();
            errcode_t ret = uapi_dma_init();
            if (ret != ERRCODE_SUCC) {
                printf("[I2S] DMA init failed: 0x%08X\r\n", ret);
                m_dma_enabled = false;
                return;
            }
            uapi_dma_open();

            // Configure I2S DMA attributes
            i2s_dma_attr_t dma_attr;
            dma_attr.tx_dma_enable = true;
            dma_attr.tx_int_threshold = I2S_DMA_INT_THRESHOLD;
            dma_attr.rx_dma_enable = true;
            dma_attr.rx_int_threshold = I2S_DMA_INT_THRESHOLD;

            ret = (errcode_t)uapi_i2s_dma_config(SIO_BUS_0, &dma_attr);
            if (ret != ERRCODE_SUCC) {
                // DMA init succeeded but I2S DMA config failed - clean up DMA
                uapi_dma_close();
                uapi_dma_deinit();
                m_dma_enabled = false;
                printf("[I2S] DMA config failed: 0x%08X\r\n", ret);
                return;
            }
            printf("[I2S] DMA enabled\r\n");
        } else {
            // Disable DMA: reconfigure I2S DMA attributes to non-DMA mode
            i2s_dma_attr_t dma_attr;
            dma_attr.tx_dma_enable = false;
            dma_attr.tx_int_threshold = I2S_DMA_INT_THRESHOLD;
            dma_attr.rx_dma_enable = false;
            dma_attr.rx_int_threshold = I2S_DMA_INT_THRESHOLD;

            uapi_i2s_dma_config(SIO_BUS_0, &dma_attr);
            printf("[I2S] DMA disabled\r\n");
        }
    }
#else
    // DMA not supported at compile time: only record the flag, no hardware operation
    printf("[I2S] DMA not supported (CONFIG_I2S_SUPPORT_DMA not enabled)\r\n");
#endif
#else
    // I2S not supported at compile time
    (void)enable;
#endif
}

bool I2SClass::isDMAEnabled() const
{
#if defined(CONFIG_I2S_SUPPORT_DMA) && (CONFIG_I2S_SUPPORT_DMA == 1)
    return m_dma_enabled;
#else
    // DMA not compiled in: always return false
    return false;
#endif
}

size_t I2SClass::writeDMA(const uint8_t *buffer, size_t size)
{
    if (!m_initialized || !m_transmitting) {
        return 0;
    }

    if (buffer == nullptr || size == 0) {
        return 0;
    }

#if defined(CONFIG_I2S_SUPPORT_DMA) && (CONFIG_I2S_SUPPORT_DMA == 1)
    if (!m_dma_enabled) {
        // DMA flag not set, fallback to normal write
        return write(buffer, size);
    }

    // Full DMA write implementation
    i2s_dma_config_t dma_cfg;
    dma_cfg.src_width = I2S_DMA_WIDTH_32BIT; // 4 bytes (32-bit samples)
    dma_cfg.dest_width = I2S_DMA_WIDTH_32BIT;
    dma_cfg.burst_length = I2S_DMA_BURST_LENGTH_1; // Burst length 1
    dma_cfg.priority = I2S_DMA_PRIORITY_DEFAULT;

    int32_t ret = uapi_i2s_merge_write_by_dma(SIO_BUS_0, buffer, size, &dma_cfg, 0, true);
    if (ret != ERRCODE_SUCC) {
        osal_printk("[I2S] DMA write failed: %d\r\n", ret);
        return 0;
    }
    return size;
#else
    // DMA not compiled in: fallback to normal write
    return write(buffer, size);
#endif
}

size_t I2SClass::readDMA(uint8_t *buffer, size_t size)
{
    if (!m_initialized || !m_receiving) {
        return 0;
    }

    if (buffer == nullptr || size == 0) {
        return 0;
    }

#if defined(CONFIG_I2S_SUPPORT_DMA) && (CONFIG_I2S_SUPPORT_DMA == 1)
    if (!m_dma_enabled) {
        // DMA flag not set, fallback to normal read
        return read(buffer, size);
    }

    // Full DMA read implementation
    i2s_dma_config_t dma_cfg;
    dma_cfg.src_width = I2S_DMA_WIDTH_32BIT; // 4 bytes (32-bit samples)
    dma_cfg.dest_width = I2S_DMA_WIDTH_32BIT;
    dma_cfg.burst_length = I2S_DMA_BURST_LENGTH_1; // Burst length 1
    dma_cfg.priority = I2S_DMA_PRIORITY_DEFAULT;

    int32_t ret = uapi_i2s_merge_read_by_dma(SIO_BUS_0, buffer, size, &dma_cfg, 0, true);
    if (ret != ERRCODE_SUCC) {
        osal_printk("[I2S] DMA read failed: %d\r\n", ret);
        return 0;
    }
    return size;
#else
    // DMA not compiled in: fallback to normal read
    return read(buffer, size);
#endif
}

// ========== Other Methods ==========

void I2SClass::setCallback(void (*callback)(uint32_t *left, uint32_t *right, uint32_t length))
{
    m_rxCallback = callback;
}

bool I2SClass::isInitialized() const
{
    return m_initialized;
}

size_t I2SClass::getBufferSize() const
{
    return m_buffer_size;
}

void I2SClass::setBufferSize(size_t size)
{
    m_buffer_size = size;
}

bool I2SClass::isTransmitting() const
{
    return m_transmitting;
}

bool I2SClass::isReceiving() const
{
    return m_receiving;
}

void I2SClass::startTransmit()
{
    if (!m_initialized) {
        return;
    }
    m_transmitting = true;
}

void I2SClass::stopTransmit()
{
    m_transmitting = false;
}

void I2SClass::startReceive()
{
    if (!m_initialized) {
        return;
    }

#if defined(CONFIG_I2S_SUPPORT) && (CONFIG_I2S_SUPPORT == 1)
    uapi_i2s_read_start(SIO_BUS_0);
#endif

    m_receiving = true;
}

void I2SClass::stopReceive()
{
    m_receiving = false;
}

// ========== Private Helper Methods ==========

uint8_t I2SClass::getSampleRateEnum(uint32_t rateHz) const
{
    (void)rateHz;
    return I2S_SAMPLE_RATE_ENUM_44K;
}

uint8_t I2SClass::getDataWidthEnum(uint8_t bits) const
{
    switch (bits) {
        case I2S_BITS_16:
            return I2S_DATA_WIDTH_ENUM_16BIT;
        case I2S_BITS_18:
            return I2S_DATA_WIDTH_ENUM_18BIT;
        case I2S_BITS_20:
            return I2S_DATA_WIDTH_ENUM_20BIT;
        case I2S_BITS_24:
            return I2S_DATA_WIDTH_ENUM_24BIT;
        case I2S_BITS_32:
            return I2S_DATA_WIDTH_ENUM_32BIT;
        default:
            return I2S_DATA_WIDTH_ENUM_16BIT;
    }
}

uint8_t I2SClass::getChannelsEnum(uint8_t channels) const
{
    if (channels <= I2S_STEREO) {
        return I2S_CHANNELS_ENUM_2CH;
    } else if (channels <= I2S_CHANNELS_MAX_4CH) {
        return I2S_CHANNELS_ENUM_4CH;
    } else if (channels <= I2S_CHANNELS_MAX_8CH) {
        return I2S_CHANNELS_ENUM_8CH;
    } else {
        return I2S_CHANNELS_ENUM_16CH;
    }
}

// ========== Global I2S Instance ==========

I2SClass I2S;

// ========== C-style API Implementation ==========

bool i2s_begin(uint32_t sample_rate, uint8_t bits, uint8_t channels)
{
    return I2S.begin(sample_rate, bits, channels);
}

void i2s_end()
{
    I2S.end();
}

size_t i2s_write(const uint8_t *buffer, size_t size)
{
    return I2S.write(buffer, size);
}

int i2s_available()
{
    return I2S.available();
}

int i2s_read()
{
    return I2S.read();
}
