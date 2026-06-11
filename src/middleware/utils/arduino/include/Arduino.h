/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * Hisilicon Adaptation: Direct SDK call implementation
 */
/*
  Arduino.h - Main include file for the Arduino SDK
  Copyright (c) 2005-2013 Arduino Team.  All right reserved.

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
*/

#ifndef ARDUINO_H
#define ARDUINO_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

// Arduino standard type aliases (required for middleware compatibility)
typedef bool boolean;
typedef uint8_t byte;

// Include Arduino core classes
#include "Print.h"
#include "Stream.h"
#include "WString.h"

// Binary constants
#define _BV(bit) (1 << (bit))
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))

// Pin modes
#define INPUT 0x00
#define OUTPUT 0x01
#define INPUT_PULLUP 0x02
#define INPUT_PULLDOWN 0x03

// Digital levels
#define LOW 0
#define HIGH 1

// Change this to match ws63 pin count
#define NUM_DIGITAL_PINS 32
#define NUM_ANALOG_INPUTS 8

// Analog reference types
#define DEFAULT 0
#define EXTERNAL 1
#define INTERNAL 2

// Timer constants - use 0xFF for invalid (not 0 to avoid conflict with timer 0)
#define NOT_ON_TIMER 0xFF

// Interrupt modes - use unique values to avoid conflict with LOW/HIGH
#define RISING 4
#define FALLING 5
#define CHANGE 6
// LOW and HIGH are already defined in Digital levels section above

// Serial definitions
// Note: SERIAL_8N1 and other macros are defined in HardwareSerial.h
// Do not redefine here to avoid conflicts

// Wire definitions
#define WIRE_HAS_END 1

// SPI definitions
#define SPI_HAS_TRANSACTION 1
#define LSBFIRST 0
#define MSBFIRST 1
// NOTE: SPI_MODE0-3 are defined in SPI.h with correct values.
// Do NOT define them here to avoid conflicts.
// Arduino standard SPI_MODE0-3 values differ from SPI.h values
// (Arduino: 0,1,2,3 vs SPI.h: 0x00,0x04,0x08,0x0C which encode CPOL/CPHA).

// Math constants
#define PI 3.1415926535897932384626433832795
#define HALF_PI 1.5707963267948966192313216916398
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105
#define EULER 2.718281828459045235360287471352

// Serial communication
#define HAVE_HWSERIAL0
#define HAVE_HWSERIAL1
#define HAVE_HWSERIAL2

// Yield function (for cooperative multitasking)
void yield(void);

// Time functions (implemented in wiring.cpp)
unsigned long millis(void);
unsigned long micros(void);
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);

// Digital IO (implemented in wiring_digital.cpp)
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int digitalRead(uint8_t pin);
void digitalToggle(uint8_t pin);

// Analog IO (implemented in wiring_analog.cpp)
int analogRead(uint8_t pin);
void analogWrite(uint8_t pin, int val);
void analogReference(uint8_t mode);
void analogReadResolution(uint8_t bits);

// Interrupts (implemented in wiring_interrupts.cpp)
void attachInterrupt(uint8_t pin, void (*handler)(void), int mode);
void detachInterrupt(uint8_t pin);
void interrupts(void);
void noInterrupts(void);

// Map digital pin to interrupt number (identity mapping on this platform)
#define digitalPinToInterrupt(p) ((int)(p))

#include <math.h>

// Tone (implemented in wiring_pulse.cpp)
void tone(uint8_t pin, unsigned int frequency, unsigned long duration = 0);
void noTone(uint8_t pin);

// Pulse (implemented in wiring_pulse.cpp)
unsigned long pulseIn(uint8_t pin, uint8_t state, unsigned long timeout = 1000000UL);
unsigned long pulseInLong(uint8_t pin, uint8_t state, unsigned long timeout = 1000000UL);

// Timer functions (implemented in TimerClass.cpp)
void timerInit(uint32_t period_us, void (*callback)());
void timerStart();
void timerStop();

// I2S functions (implemented in I2SClass.cpp)
// Note: Include I2SClass.h for full I2SClass API and default arguments
bool i2s_begin(uint32_t sampleRate, uint8_t bits, uint8_t channels);
void i2s_end();
size_t i2s_write(const uint8_t *buffer, size_t size);
int i2s_available();
int i2s_read();

// Shift (implemented in wiring_pulse.cpp)
void shiftOut(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder, uint8_t val);
uint8_t shiftIn(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder);

void randomSeed(unsigned long seed);
long random(long max);
long random(long min, long max);

// Math
long map(long value, long fromLow, long fromHigh, long toLow, long toHigh);
unsigned int makeWord(unsigned int w);
unsigned int makeWord(unsigned char h, unsigned char l);
#define word(...) makeWord(__VA_ARGS__)

// USB macros (not applicable for ws63, but kept for compatibility)
#define SERIAL_PORT_MONITOR Serial
#define SERIAL_PORT_HARDWARE Serial
#define SERIAL_PORT_HARDWARE1 Serial1
#define SERIAL_PORT_HARDWARE2 Serial2

#endif // ARDUINO_H