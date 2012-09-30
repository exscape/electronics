#include <SPI.h>         // Remember this line!
#include <DAC_MCP49xx.h>

// The Arduino pin used for the slave select / chip select
#define SS_PIN 10

// Set up the DAC. 
// First argument: model (MCP4901, MCP4911, MCP4921)
// Second argument: SS pin (10 is preferred)
// (The third argument, the LDAC pin, can be left out if not used)
DAC_MCP49xx dac(DAC_MCP49xx::MCP4901, SS_PIN);

void setup() {
  // Set the SPI frequency to 1 MHz (on 16 MHz Arduinos), to be safe.
  // DIV2 = 8 MHz works for me, though, even on a breadboard.
  // This is not strictly required, as there is a default setting.
  dac.setSPIDivider(SPI_CLOCK_DIV16);
  
  // Use "port writes", see the manual page. In short, if you use pin 10 for
  // SS (and pin 7 for LDAC, if used), this is much faster.
  // Also not strictly required (no setup() code is needed at all).
  dac.setPortWrite(true);
}

// Output something slow enough that a multimeter can pick it up.
// Shifts the output between 0 V and <VREF> (5 volts for many, but not
// necessarily!).
// For MCP4911, use 1023 instead of 255.
// For MCP4921, use 4095 instead of 255.
void loop() {
  dac.output(0);
  delay(2000);
  dac.output(255);
  delay(2000);
}
