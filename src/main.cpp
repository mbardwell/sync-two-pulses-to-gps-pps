#include "TeensyTimerTool.h"
#include <Arduino.h>
using namespace TeensyTimerTool;

const uint8_t PPS_INTERRUPT_PIN = 12;
const uint8_t CAMERA_A = 13;
const uint8_t CAMERA_B = 14;
const uint8_t PPS_TEST_PIN = 3;
const int DEFAULT_FREQ = 40;
const int DEFAULT_DIVIDER = 10;
int freq = DEFAULT_FREQ;
int divider = DEFAULT_DIVIDER;
bool on = false;
PeriodicTimer t1(GPT1);
PeriodicTimer t2(GPT2);
PeriodicTimer t_pps_test(PIT);
volatile int counter_a = 0;
volatile int counter_b = 0;

int timer_period_of_frequency(int freq_hz) { return 1'000'000 / (2 * freq_hz); }

void trigger_camera(int pin, volatile int &counter, int counter_max,
                    PeriodicTimer &t) {
  digitalWriteFast(pin, !digitalReadFast(pin));
  if (++counter == counter_max) {
    t.stop();
  }
}

void callbackpps() {
  digitalWrite(PPS_TEST_PIN, HIGH);
  digitalWrite(PPS_TEST_PIN, LOW);
}

void on_pps() {
  if (on) {
    // This whole routine takes <2us, which is more than accurate enough for us
    // to be able to run it on each pulse, ensuring synchronisation to the
    // second and the ability to dictate precisely how many frames we want
    t1.stop();
    t2.stop();
    counter_a = 0;
    counter_b = 0;
    digitalWriteFast(CAMERA_A, HIGH);
    digitalWriteFast(CAMERA_B, HIGH);
    counter_a++; // because we forced a pulse
    counter_b++; // because we forced a pulse
    t1.start();
    t2.start();
  } else {
    t1.stop();
    t2.stop();
    digitalWriteFast(CAMERA_A, LOW);
    digitalWriteFast(CAMERA_B, LOW);
  }
}

void setup() {
  pinMode(CAMERA_A, OUTPUT);
  pinMode(CAMERA_B, OUTPUT);
  pinMode(PPS_INTERRUPT_PIN, INPUT_PULLUP);
  attachInterrupt(PPS_INTERRUPT_PIN, on_pps, RISING);

  /* mock PPS
   *  connect pin PPS_TEST_PIN to PPS_INTERRUPT_PIN to fake the PPS signal
   */
  pinMode(PPS_TEST_PIN, OUTPUT);
  digitalWrite(PPS_TEST_PIN, LOW);
  t_pps_test.begin(callbackpps, 1'000'000, true);
  // end mock PPS
}

void parse_serial() {
  // Expect <F60\n> <D10\n> <I\n> <O\n>
  static int value;
  static char arg;
  static bool data_updated;
  static const char END_MARKER = '\n';
  static char rx_char;

  if (data_updated) {
    value = 0;
    arg = '\0';
    data_updated = false;
  }

  if (Serial.available() > 0) {
    rx_char = Serial.read();
    if (arg == '\0') {
      arg = rx_char;
    } else if (rx_char != END_MARKER) {
      value = value * 10 + rx_char - '0';
    } else if (rx_char == END_MARKER) {
      switch (arg) {
      case 'F':
        freq = value;
        Serial.print("Frequency updated: ");
        Serial.println(freq);
        break;
      case 'D':
        divider = value;
        Serial.print("Divider updated: ");
        Serial.println(divider);
        break;
      case 'I':
        Serial.print("Starting capture. Frequency: ");
        Serial.print(freq);
        Serial.print(". Divider: ");
        Serial.println(divider);
        t1.begin([] { trigger_camera(CAMERA_A, counter_a, 2 * freq, t1); },
                 timer_period_of_frequency(freq), false);
        t2.begin(
            [] {
              trigger_camera(CAMERA_B, counter_b, 2 * (freq / divider), t2);
            },
            timer_period_of_frequency(freq) * divider, false);
        on = true;
        break;
      case 'O':
        Serial.println("Off");
        on = false;
        break;
      default:
        Serial.print("Error. Received unknown argument: ");
        Serial.print(arg);
        Serial.println(
            ". Supported: Freq <Fx>, Divider <Dx>, Start <I>, Stop <O>");
        break;
      }
      data_updated = true;
    } else {
      Serial.println("You should not be here");
      data_updated = true;
    }
  }
}

void loop() { parse_serial(); }
