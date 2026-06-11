/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/*
  Print.cpp - HiSilicon Adaptation for Arduino compatibility layer
  Uses UartPuts as backend for all output

  Implements printNumber() and printFloat() for all numeric print() variants
  Uses pure integer arithmetic - no libm dependency
*/
/*
 Print.cpp - Base class that provides print() and println()
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
 
 Modified 23 November 2006 by David A. Mellis
 Modified 03 August 2015 by Chuck Todd
*/

#include "Print.h"
#include <string.h>

/* Arduino compatibility layer uart output interface */
#ifdef __cplusplus
extern "C" {
#endif
extern uint32_t UartPuts(uint8_t id, char *data, uint32_t len, uint32_t mode);
#ifdef __cplusplus
}
#endif

#define LONG_LEN 8
#define FLOAT_LEN 16
#define EOL_LEN 2
#define DECIMAL_BASE 10.0  // Multiplier to shift fractional decimal point

// ============ Constructor / Destructor ============

Print::Print() : write_error(0) {}

Print::~Print() {}

// ============ Write Methods (backend output) ============

size_t Print::write(const char *str)
{
    if (str == nullptr)
        return 0;
    size_t len = strlen(str);
    if (len == 0)
        return 0;
    UartPuts(0, const_cast<char *>(str), (uint32_t)len, 1);
    return len;
}

size_t Print::write(const char *buffer, size_t size)
{
    if (buffer == nullptr || size == 0)
        return 0;
    UartPuts(0, const_cast<char *>(buffer), (uint32_t)size, 1);
    return size;
}

size_t Print::write(const uint8_t *buffer, size_t size)
{
    if (buffer == nullptr || size == 0)
        return 0;
    UartPuts(0, const_cast<char *>((const char *)buffer), (uint32_t)size, 1);
    return size;
}

// ============ printNumber (protected helper) ============

size_t Print::printNumber(unsigned long n, uint8_t base)
{
    if (base < BIN)
        base = DEC;
    if (base > HEX)
        base = HEX;

    char buf[LONG_LEN * sizeof(unsigned long) + 1];
    char *p = &buf[sizeof(buf) - 1];
    *p = '\0';

    static const char digits[] = "0123456789ABCDEF";
    do {
        *--p = digits[n % base];
        n /= base;
    } while (n > 0);

    return write((const uint8_t *)p, strlen(p));
}

// ============ printFloat (protected helper) ============

size_t Print::printFloat(double n, uint8_t digits)
{
    size_t count = 0;

    // Handle negative numbers
    if (n < 0.0) {
        count += write('-');
        n = -n;
    }

    // Convert to integer: multiply by 10^digits and round
    int32_t int_part = (int32_t)n;
    double frac = n - (double)int_part;

    // Print integer part
    char int_buf[FLOAT_LEN];
    char *p = &int_buf[sizeof(int_buf) - 1];
    *p = '\0';
    int32_t tmp = int_part;
    do {
        *--p = (char)('0' + (tmp % DEC));
        tmp /= DEC;
    } while (tmp > 0);
    count += write((const uint8_t *)p, strlen(p));

    // Print fractional part
    if (digits > 0) {
        count += write('.');

        for (uint8_t i = 0; i < digits; i++) {
            frac *= DECIMAL_BASE;
        }
        uint32_t frac_int = (uint32_t)(frac + 0.5);

        // Print each fractional digit
        uint32_t divisor = 1;
        for (uint8_t i = 1; i < digits; i++) {
            divisor *= DEC;
        }

        for (uint8_t i = 0; i < digits; i++) {
            uint8_t d = (uint8_t)(frac_int / divisor);
            char c = (char)('0' + d);
            count += write((uint8_t)c);
            frac_int %= divisor;
            divisor /= DEC;
        }
    }

    return count;
}

// ============ print() overloads ============

size_t Print::print(const char *str)
{
    if (str == nullptr)
        return print("(null)");
    return write(str);
}

size_t Print::print(char c)
{
    return write((uint8_t)c);
}

size_t Print::print(int n, int base)
{
    return print((long)n, base);
}

size_t Print::print(unsigned int n, int base)
{
    return print((unsigned long)n, base);
}

size_t Print::print(long n, int base)
{
    if (base == DEC && n < 0) {
        size_t count = write('-');
        count += printNumber((unsigned long)(-n), DEC);
        return count;
    }
    return printNumber((unsigned long)n, base);
}

size_t Print::print(unsigned long n, int base)
{
    return printNumber(n, base);
}

size_t Print::print(double n, int digits)
{
    return printFloat(n, (uint8_t)digits);
}

// ============ println() overloads ============

size_t Print::println(void)
{
    return write("\r\n", EOL_LEN);
}

size_t Print::println(const char *str)
{
    size_t n = print(str);
    n += println();
    return n;
}

size_t Print::println(char c)
{
    size_t n = print(c);
    n += println();
    return n;
}

size_t Print::println(int n, int base)
{
    size_t s = print(n, base);
    s += println();
    return s;
}

size_t Print::println(unsigned int n, int base)
{
    size_t s = print(n, base);
    s += println();
    return s;
}

size_t Print::println(long n, int base)
{
    size_t s = print(n, base);
    s += println();
    return s;
}

size_t Print::println(unsigned long n, int base)
{
    size_t s = print(n, base);
    s += println();
    return s;
}

size_t Print::println(double n, int digits)
{
    size_t s = print(n, digits);
    s += println();
    return s;
}

// ============ Error Handling ============

int Print::getWriteError()
{
    return write_error;
}

void Print::clearWriteError()
{
    setWriteError(0);
}

void Print::setWriteError(int err)
{
    write_error = err;
}
