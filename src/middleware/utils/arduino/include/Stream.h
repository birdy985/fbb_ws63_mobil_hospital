/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * HiSilicon Adaptation: Direct SDK call implementation
 */
/*
  Stream.h - base class for character-based streams.
  Copyright (c) 2010 David A. Mellis.  All right reserved.

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

  parsing functions based on TextFinder library by Michael Margolis
*/

#ifndef STREAM_H
#define STREAM_H

#include "Print.h"

// Forward declaration for String (defined in WString.h)
class String;

#define NO_IGNORE_CHAR '\x01'

class Stream : public Print {
protected:
    unsigned long _timeout;
    unsigned long _startMillis;
    int readBytesUntil(char terminator, char *buffer, size_t length, bool withTerminator);

public:
    Stream();
    virtual ~Stream();

    virtual int available() const  = 0;
    virtual int read() = 0;
    virtual int peek() const = 0;
    virtual void flush() = 0;

    void setTimeout(unsigned long timeout);
    unsigned long getTimeout();

    bool find(const char *target);
    bool findUntil(const char *target, const char *terminator);

    long parseInt();
    long parseInt(char ignore);
    float parseFloat();
    float parseFloat(char ignore);

    size_t readBytes(char *buffer, size_t length);
    size_t readBytesUntil(char terminator, char *buffer, size_t length);

    String readString();
    String readStringUntil(char terminator);

protected:
    int timedRead();
    int timedPeek();
    int peekNextDigit(char ignore, bool detectDecimal);
};

#endif // STREAM_H
