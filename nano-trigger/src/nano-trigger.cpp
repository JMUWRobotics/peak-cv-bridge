#include "Arduino.h"
#include <limits.h>

#define DEFAULT_FREQUENCY_HZ 10

// both of these defines have to represent the same prescaling
#define PRESCALE_FACTOR 1024
#define PRESCALE_BITS (_BV(CS12) | _BV(CS10))

typedef unsigned long ulong;

void
set_edge_delay(float freq_hz)
{
    constexpr ulong freq_timer_hz = F_CPU / PRESCALE_FACTOR;
    ulong icr1_target = ulong(freq_timer_hz / freq_hz - 1.f);
    // - 1.f:
    // https://stackoverflow.com/questions/48873501/setting-up-arduino-uno-atmega328p-pwm-with-timer1

    if (USHRT_MAX < icr1_target) {
        Serial.print("icr1_target overflow for freq: ");
        Serial.println(icr1_target);

        icr1_target = USHRT_MAX;
    }

    ICR1 = static_cast<uint16_t>(icr1_target);
    OCR1A = ICR1 / 2;
}

void
setup()
{
    Serial.begin(9600);
    Serial.println("Arduino Nano squarewave generator at your service.");

    // disable interrupts for 16-bit access
    byte sreg = SREG;
    cli();

    // PB1 as output pin -> D9 (Arduino nano)
    DDRB |= _BV(PB1);

    TCCR1A = 0;
    TCCR1B = 0;

    // OC1A to low on compare match
    TCCR1A |= _BV(COM1A1);

    // Fast PWM, ICR1 as TOP
    TCCR1B |= _BV(WGM13) | _BV(WGM12);
    TCCR1A |= _BV(WGM11);

    // 1/1024 prescale
    TCCR1B |= PRESCALE_BITS;

    // restore interrupts
    SREG = sreg;

    set_edge_delay(DEFAULT_FREQUENCY_HZ);
}

void
loop()
{
    if (0 < Serial.available()) {
        float frequency_hz = Serial.parseFloat(SKIP_ALL);

        if (0.f < frequency_hz) {
            Serial.print("Got frequency of ");
            Serial.print(frequency_hz);
            Serial.println(" Hz");

            set_edge_delay(frequency_hz);
        }
    }
}
