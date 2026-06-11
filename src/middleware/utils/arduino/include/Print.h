/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * HiSilicon Adaptation: Direct SDK call implementation
 */
/*
  Print.h - Base class that provides print() and println()
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
*/

#ifndef PRINT_H
#define PRINT_H

#include <stdint.h>
#include <stddef.h>

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

class Print {
private:
    int write_error;
    size_t printNumber(unsigned long, uint8_t);
    size_t printFloat(double, uint8_t);

public:
    Print();
    virtual ~Print();

    virtual size_t write(uint8_t c) = 0;
    virtual size_t write(const uint8_t *buffer, size_t size);
    virtual size_t write(const char *buffer, size_t size);
    virtual size_t write(const char *str);

    size_t print(const char *str);
    size_t print(char c);
    size_t print(int n, int base = DEC);
    size_t print(unsigned int n, int base = DEC);
    size_t print(long n, int base = DEC);
    size_t print(unsigned long n, int base = DEC);
    size_t print(double n, int digits = 2);

    size_t println(void);
    size_t println(const char *str);
    size_t println(char c);
    size_t println(int n, int base = DEC);
    size_t println(unsigned int n, int base = DEC);
    size_t println(long n, int base = DEC);
    size_t println(unsigned long n, int base = DEC);
    size_t println(double n, int digits = 2);

    int getWriteError();
    void clearWriteError();

protected:
    void setWriteError(int err = 1);
};

#endif // PRINT_H
