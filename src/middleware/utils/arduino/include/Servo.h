/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/*
  Servo.h - Servo library
  HiSilicon Adaptation: Uses PWM hardware to generate 50Hz servo signals

  Pulse range: 544-2400μs (standard hobby servo)
  Supports up to 4 independent servos
  APIs: uapi_pwm_init(), uapi_pwm_open(), uapi_pwm_start(), uapi_pwm_update_duty_ratio()
*/

#ifndef SERVO_H
#define SERVO_H

#include <stdint.h>
#include <Arduino.h>

// Maximum number of servos
#define MAX_SERVOS 4

// Servo pulse timing (microseconds)
#define MIN_PULSE_WIDTH 544  // Minimum pulse width (0 degrees)
#define MAX_PULSE_WIDTH 2400 // Maximum pulse width (180 degrees)

// Servo frequency (50Hz = 20ms period)
#define SERVO_FREQUENCY 50

// Standard servo timing defaults
// Center pulse width (microseconds)
#define SERVO_CENTER_US 1500
// Full period in microseconds (50Hz => 20ms)
#define SERVO_PERIOD_US 20000
// Servo angle range
#define SERVO_MIN_ANGLE 0
#define SERVO_MAX_ANGLE 180
#define SERVO_CENTER_ANGLE 90
// Microseconds per second constant
#define MICROSECONDS_PER_SECOND 1000000UL
// Default PWM clock frequency to assume when driver returns 0
#define DEFAULT_PWM_FREQ_HZ 40000000UL

// Invalid channel (must not conflict with valid channels 0-3)
#define INVALID_CHANNEL 0xFF

class Servo {
public:
    Servo();
    ~Servo();

    // Attach servo to a pin, returns true if successful
    bool attach(uint8_t pin);

    // Attach servo to a pin with min/max pulse width, returns true if successful
    bool attach(uint8_t pin, int min, int max);

    // Attach servo to a pin with min/max pulse and angle range
    bool attach(uint8_t pin, int min, int max, int minAngle, int maxAngle);

    // Detach servo from pin
    void detach();

    // Deinitialize PWM module and release all resources.
    // Note: This will stop ALL PWM channels used by all Servo instances.
    // After calling this, all Servo instances will be detached and
    // uapi_pwm_init() will be called again on next attach.
    static void end();

    // Write angle (0-180 degrees)
    void write(uint8_t angle);

    // Write pulse width directly (microseconds)
    void writeMicroseconds(uint16_t pulse);

    // Read current angle
    uint8_t read();

    // Check if servo is attached
    bool attached();

private:
    uint8_t m_pin;         // Pin number
    uint8_t m_channel;     // PWM channel/handle
    bool m_attached;       // Attachment status
    uint8_t m_angle;       // Current angle (0-180 degrees)
    uint16_t m_pulseWidth; // Current pulse width in microseconds
    int m_minPulse;        // Minimum pulse width (default 544)
    int m_maxPulse;        // Maximum pulse width (default 2400)
    int m_minAngle;        // Minimum angle (default 0)
    int m_maxAngle;        // Maximum angle (default 180)

    // Calculate duty ratio from pulse width
    void updateDutyRatio(uint16_t pulseWidth);

    // Convert angle to pulse width
    uint16_t angleToPulse(uint8_t angle);

    // Convert pulse width to angle
    uint8_t pulseToAngle(uint16_t pulse);
};

#endif // SERVO_H
