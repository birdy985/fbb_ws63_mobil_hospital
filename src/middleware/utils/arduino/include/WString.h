/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * HiSilicon Adaptation: Direct SDK call implementation with LiteOS heap
 */
/*
  WString.h - String library for Wiring & Arduino
  ...mostly rewritten by Paul Stoffregen...
  Copyright (c) 2009-10 Hernando Barragan.  All right reserved.
  Copyright 2011, Paul Stoffregen, paul@pjrc.com

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

#ifndef WSTRING_H
#define WSTRING_H

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

class String {
private:
    char *_buffer;
    unsigned int _len;
    unsigned int _capacity;

    void invalidate();
    bool changeBuffer(unsigned int maxStrLen);

public:
    // Constructors
    String();
    String(const char *cstr);
    String(const String &str);
    String(char c);
    String(int num, int base = 10);
    String(unsigned int num, int base = 10);
    String(long num, int base = 10);
    String(unsigned long num, int base = 10);
    String(float num, int digits = 2);
    String(double num, int digits = 2);
    ~String();

    // Memory management
    bool reserve(unsigned int size);
    unsigned int capacity() const;

    // Basic operations
    unsigned int length() const;
    char charAt(unsigned int index) const;
    void setCharAt(unsigned int index, char c);
    const char *c_str() const;
    char *buffer() const;

    // Concatenation
    String &concat(const String &str);
    String &concat(const char *cstr);
    String &concat(char c);
    String &concat(int num);
    String &concat(unsigned int num);
    String &concat(long num);
    String &concat(unsigned long num);
    String &concat(float num);
    String &concat(double num);

    String &operator = (const String &rhs);
    String &operator = (const char *cstr);
    String &operator += (const String &rhs);
    String &operator += (const char *cstr);
    String &operator += (char c);

    String operator + (const String &rhs) const;
    String operator + (const char *cstr) const;
    String operator + (char c) const;

    // Comparison
    int compareTo(const String &s) const;
    bool equals(const String &s) const;
    bool equalsIgnoreCase(const String &s) const;
    bool startsWith(const String &prefix) const;
    bool startsWith(const String &prefix, unsigned int offset) const;
    bool endsWith(const String &suffix) const;

    // Search
    int indexOf(char ch) const;
    int indexOf(char ch, unsigned int from) const;
    int indexOf(const String &s) const;
    int indexOf(const String &s, unsigned int from) const;
    int lastIndexOf(char ch) const;
    int lastIndexOf(char ch, unsigned int from) const;
    int lastIndexOf(const String &s) const;
    int lastIndexOf(const String &s, unsigned int from) const;

    // Substring
    String substring(unsigned int begin) const;
    String substring(unsigned int begin, unsigned int end) const;

    // Conversion
    long toInt() const;
    float toFloat() const;
    double toDouble() const;

    // Modification
    void toLowerCase();
    void toUpperCase();
    void trim();

    // Operators
    bool operator == (const String &rhs) const;
    bool operator != (const String &rhs) const;
    bool operator < (const String &rhs) const;
    bool operator > (const String &rhs) const;
    bool operator <= (const String &rhs) const;
    bool operator >= (const String &rhs) const;
    char operator[](unsigned int index) const;
    char &operator[](unsigned int index);

    friend String operator + (const char *cstr, const String &rhs);
};

// External functions
String toString(const String &val);

#endif // WSTRING_H
