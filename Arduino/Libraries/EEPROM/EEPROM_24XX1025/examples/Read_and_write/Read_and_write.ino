#include <I2C16.h>           // Don't miss this line!
#include <EEPROM_24XX1025.h>

// Initialize the EEPROM with address bits A0 = 0, A1 = 0 (in that order)
// This is set by connecting the physical pins 1 and 2
// to either ground (for 0) or VDD (for 11).
EEPROM_24XX1025 eeprom (0, 0);

void setup() {
  // Set up serial. NOTE: You need to set the Arduino
  // serial console to 115200 baud as well!
  Serial.begin(115200);
}

void loop() {
  // Write something out. We unfortunately need to cast between
  // char* and byte*.
  byte *str = (byte *)"Hello, world!";
  eeprom.write(0, str, strlen((char *)str));

  // Read it back in a loop
  while(true) {
    // Read as a block
    byte buf[14] = {0};
    eeprom.read(0, buf, 13);
    Serial.print("block read: ");
    Serial.println((char *)buf);

    // Read byte-for-byte
    eeprom.setPosition(0);
    Serial.print("byte read : ");

    for (int i = 0; i < 13; i++) {
      Serial.print( (char) eeprom.read() );
    }

    Serial.print("\n");

    delay(2000);
  }
}
