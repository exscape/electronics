/*
 * Microchip MCP4901 / MCP4911 / MCP4921 8/10/12-bit DAC driver
 * Thomas Backman, 2012-07
 * serenity@exscape.org
 
 * Code license: BSD/MIT. Whatever; I *prefer* to be credited,
 * and you're *not* allowed to claim you wrote this from scratch.
 * Apart from that, do whatever you feel like. Commercial projects,
 * code modifications, etc.

 Pins used: 
 * Arduino pin 10 (SS) to device CS (pin 2)
 * Arduino pin 11 (MOSI) to device SDI (pin 4)
 * Arduino pin 13 (SCLK) to device SCK (pin 3)
 * Other DAC wirings:  
 * Pin 1: VDD, to +5 V
 * Pin 5: LDAC, to ground (to update vout continuously). This code doesn't support the LDAC function.
 * Pin 6: VREF, to +5 V (or some other reference voltage 0 < VREF <= VDD)
 * Pin 7: VSS, to ground
 * Pin 8: vout
 * (Pin 9, for the DFN package only: VSS)
 
 * Only tested on MCP4901 (8-bit), but it should work on the others as well.
 * LDAC pin support not implemented.
 */

#include <SPI.h>

// The Arduino pin connected to CS on the DAC
#define SS_PIN 10

// How many bits is this DAC? MCP4901: 8, MCP4911: 10, MCP4921: 12
#define BITWIDTH 8

// Buffer VREF? See datasheet. 1 or 0.
// Note that with BUF = 1, VREF cannot be VDD.
#define BUF 0

// "1" for 1x gain; "0" for 2x gain(!)
#define GAIN 1

// Use 1 for faster output switching; however, on Arduinos that don't use Atmega 168/328,
// this might not work. Disabled by default for that reason.
// You'd have to change the port value in the code to use the correct pin.
// This uses pin 10 (PORTB pin 2) on an Atmega 168/328 based Arduino.
// This *IGNORES* the SS_PIN definition above!
#define PORT_WRITE 0

// Sets the SPI clock frequency, main clock divided by 2, 4, 8, 16, 32, 64 or 128
// (that's 8 MHz down to 125 kHz on 16 MHz Arduinos)
// Try DIV4, DIV8 etc. if DIV2 doesn't work. DIV2 = 8 MHz works for me (on a breadboard), though.
#define SPI_DIVIDER SPI_CLOCK_DIV2

void dac_init(void);
void dac_shutdown(void);
void dac_set(unsigned short);

void setup() {
	dac_init();
}

// Some test code. Slow enough that a multimeter can show what happens.
// Starts at zero, increases to max over ~4-5 seconds, shuts down 3 seconds, repeats.
void loop() {
  for (int i = 0; i < (1 << BITWIDTH); i++) {
    dac_set(i);
    delay(5000/(1 << BITWIDTH));
  }
  
  dac_shutdown();
  delay(3000);
}

////////////////////////////////////////////////////////////
// Normal usage should require no changes below this line //
////////////////////////////////////////////////////////////

void dac_init(void) {
  pinMode(SS_PIN, OUTPUT); // Ensure that SS is set to SPI master mode

  digitalWrite(SS_PIN, HIGH); // Unselect the device
  delayMicroseconds(10);

  SPI.begin();  
  SPI.setBitOrder(MSBFIRST);
  SPI.setDataMode(SPI_MODE0);
  SPI.setClockDivider(SPI_DIVIDER);
}

void dac_shutdown(void) {
 // Shuts the DAC down. Shutdown current is less than 1/50 (typical) of active mode current.
 // My measurements say ~160-180 µA active (unloaded vout), ~3.5 µA shutdown.
 // Time to settle on an output value increases from ~4.5 µs to ~10 µs, though (according to the datasheet).
   
   digitalWrite(SS_PIN, LOW);
   unsigned short out = (BUF << 14) | (GAIN << 13);
   // Sending all zeroes should work, too, but I'm unsure whether there might be a switching time
   // between buffer and gain modes, so we'll send them so that they have the same value once we
   // exit shutdown.
   SPI.transfer((out & 0xff00) >> 8);
   SPI.transfer(out & 0xff);
   digitalWrite(SS_PIN, HIGH);
}

void dac_set(unsigned short data) {
// Truncate the unused bits to fit the 8/10/12 bits the DAC accepts
#if BITWIDTH == 12
  data &= 0xfff;
#elif BITWIDTH == 10
  data &= 0x3ff;
#elif BITWIDTH == 8
  data &= 0xff;
#else
#error Unsupported bitwidth for MCP49x1 series DAC (8/10/12 is supported)
#endif

// Drive chip select low
#if PORT_WRITE == 1
 PORTB &= 0xfb; // Clear PORTB pin 2 = arduino pin 10
#else
 digitalWrite(SS_PIN, LOW); 
#endif

  // bit 15: always 0 (1 means "ignore this command")
  // bit 14: buffer VREF?
  // bit 13: gain bit; 0 for 1x gain, 1 for 2x
  // bit 12: shutdown bit. 1 for active operation
  // bits 11 through 0: data 
  unsigned short out = (BUF << 14) | (GAIN << 13) | (1 << 12) | (data << (12 - BITWIDTH));
  
  // Send the command and data bits
  SPI.transfer((out & 0xff00) >> 8);
  SPI.transfer(out & 0xff);

// Return chip select to high
#if PORT_WRITE == 1
 PORTB |= (1 << 2); // set PORTB pin 2 = arduino pin 10
#else
 digitalWrite(SS_PIN, HIGH);
#endif
}
