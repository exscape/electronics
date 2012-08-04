#include <I2C16.h>
#include <EEPROM_24XX1025.h>

// Initialize the EEPROM with address bits A1 = 0, A0 = 0
// (set by connecting the physical pins to either ground (0) or VDD (1))
EEPROM_24XX1025 eeprom (0,0);

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
