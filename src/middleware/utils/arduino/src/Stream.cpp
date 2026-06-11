/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * HiSilicon Adaptation: Direct SDK call implementation
 */
/*
 Stream.cpp - adds parsing methods to Stream class
 Copyright (c) 2008 David A. Mellis.  All right reserved.

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

 Created July 2011
 parsing functions based on TextFinder library by Michael Margolis

 findMulti/findUntil routines written by Jim Leonard/Xuth
*/

#include "Arduino.h"
#include "Stream.h"
#include "WString.h"

// Magic numbers made explicit
#define DEFAULT_TIMEOUT_MS 1000UL
#define TIMEOUT_ERROR (-1)
#define MIN_AVAILABLE_BYTES 1
#define MIN_READ_LENGTH 1
#define DECIMAL_BASE 10
#define FRACTION_STEP 0.1f

Stream::Stream() : _timeout(DEFAULT_TIMEOUT_MS) {}

Stream::~Stream() {}

void Stream::setTimeout(unsigned long timeout)
{
    _timeout = timeout;
}

unsigned long Stream::getTimeout()
{
    return _timeout;
}

bool Stream::find(const char *target)
{
    return findUntil(target, NULL);
}

bool Stream::findUntil(const char *target, const char *terminator)
{
    if (target == NULL)
        return false;

    size_t targetLen = strlen(target);
    size_t terminatorLen = terminator ? strlen(terminator) : 0;
    size_t index = 0;
    size_t terminatorIndex = 0;

    unsigned long startMillis = 0;
    (void)startMillis; // Unused in stub implementation

    while (targetLen > 0) {
        int c = timedRead();
        if (c < 0) {
            return false; // Timeout
        }

        if (c == target[index]) {
            // Found a match
            index++;
            if (index >= targetLen) {
                return true; // Found the whole target
            }
            continue;
        }

        // Not a match, check terminator
        if (terminator && c == terminator[terminatorIndex]) {
            terminatorIndex++;
            if (terminatorIndex >= terminatorLen) {
                return false; // Found terminator before target
            }
        } else {
            terminatorIndex = 0;
        }
        index = 0;
    }
    return true; // Empty target always found
}

long Stream::parseInt()
{
    return parseInt(NO_IGNORE_CHAR);
}

long Stream::parseInt(char ignore)
{
    bool isNegative = false;
    long value = 0;
    int c;

    c = peekNextDigit(ignore, false);
    if (c < 0)
        return 0; // Timeout

    do {
        if (c == '-') {
            isNegative = true;
        } else if (c >= '0' && c <= '9') {
            value = value * DECIMAL_BASE + (c - '0');
        }
        c = read(); // Consume the character
    } while (isdigit(c) || c == ignore);

    if (isNegative) {
        value = -value;
    }
    return value;
}

float Stream::parseFloat()
{
    return parseFloat(NO_IGNORE_CHAR);
}

float Stream::parseFloat(char ignore)
{
    bool isNegative = false;
    bool isFractional = false;
    float result = 0.0f;
    float fraction = 0.1f;
    int c;

    c = peekNextDigit(ignore, true);
    if (c < 0)
        return 0.0; // Timeout

    do {
        if (c == '-') {
            isNegative = true;
        } else if (c == '.') {
            isFractional = true;
        } else if (c >= '0' && c <= '9') {
            if (isFractional) {
                result += fraction * (c - '0');
                fraction *= FRACTION_STEP;
            } else {
                result = result * (float)DECIMAL_BASE + (c - '0');
            }
        }
        c = read(); // Consume the character
    } while (isdigit(c) || c == '.' || c == ignore);

    if (isNegative) {
        result = -result;
    }
    return result;
}

size_t Stream::readBytes(char *buffer, size_t length)
{
    if (buffer == nullptr) {
        return 0;
    }

    size_t count = 0;
    unsigned long startMillis = 0;
    (void)startMillis; // Unused in stub implementation

    while (count < length) {
        int c = timedRead();
        if (c < 0)
            break; // Timeout
        buffer[count++] = (char)c;
    }

    return count;
}

size_t Stream::readBytesUntil(char terminator, char *buffer, size_t length)
{
    return readBytesUntil(terminator, buffer, length, false);
}

int Stream::readBytesUntil(char terminator, char *buffer, size_t length, bool withTerminator)
{
    if (buffer == nullptr || length < MIN_READ_LENGTH) {
        return 0;
    }

    size_t index = 0;
    unsigned long startMillis = 0;
    (void)startMillis; // Unused in stub implementation

    while (index < length - 1) {
        int c = timedRead();
        if (c < 0)
            break; // Timeout

        if (c == terminator) {
            if (withTerminator) {
                buffer[index++] = (char)c;
            }
            break;
        }
        buffer[index++] = (char)c;
    }

    buffer[index] = '\0';
    return index;
}

String Stream::readString()
{
    String ret;
    int c = timedRead();
    while (c >= 0) {
        ret += (char)c;
        c = timedRead();
    }
    return ret;
}

String Stream::readStringUntil(char terminator)
{
    String ret;
    int c = timedRead();
    while (c >= 0 && c != terminator) {
        ret += (char)c;
        c = timedRead();
    }
    return ret;
}

int Stream::timedRead()
{
    unsigned long startMillis = millis(); // 记录开始时间

    while (available() < MIN_AVAILABLE_BYTES) {
        // 超时检查：超过 _timeout 毫秒则退出
        if ((millis() - startMillis) >= _timeout) {
            return TIMEOUT_ERROR;
        }
    }
    return read();
}

int Stream::timedPeek()
{
    unsigned long startMillis = millis(); // 记录开始时间

    while (available() < MIN_AVAILABLE_BYTES) {
        // 超时检查：超过 _timeout 毫秒则退出
        if ((millis() - startMillis) >= _timeout) {
            return TIMEOUT_ERROR;
        }
    }
    return peek();
}

int Stream::peekNextDigit(char ignore, bool detectDecimal)
{
    int c;
    while (1) {
        c = timedPeek();
        if (c < 0)
            return c; // Timeout

        if (c == '-' || (c >= '0' && c <= '9')) {
            return c;
        }
        if (detectDecimal && c == '.') {
            return c;
        }
        if (c == ignore) {
            read(); // Consume and continue
            continue;
        }
        return TIMEOUT_ERROR; // Non-digit, non-ignored character
    }
}
