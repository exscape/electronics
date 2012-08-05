// Thomas Backman
// August 5, 2012
// Receives serial data via a custom protocol, and writes it to the EEPROM.

// Protocol:
// 1) Sender sends a byte with the transfer size for the coming chunk (1 - 255 bytes)
// 2) Receiver sends an ACK byte
// 3) Sender sends all the data in one go
// 4) Receiver sends an ACK byte
// 5) The receiver processes the received data.
// 6) Receiver sends a RDY byte when it is ready to receive further data.
// 7) The sender goes back to step 1, if there is more data to send.
//
// The loop is broken when the entire file is sent, after which
// 8) The sender sends an END byte (0x0) signalling the end of the transmission.

// On the receiver side, the above applies, with the addition of sending the ERR byte (after discarding
// all incoming bytes until "they stop coming") if there is a transmission error.

#include <I2C16.h>
#include <EEPROM_24XX1025.h>

EEPROM_24XX1025 eeprom (0, 0);

#define RDY 0xfd
#define ACK 0xfe
#define ERR 0xfc
#define END 0x0

uint32_t bytesReceived = 0;

void setup() {
  Serial.begin(115200);
  pinMode(13, OUTPUT);

  bytesReceived = 0;
  eeprom.setPosition(0); // Not really needed
}

void sendError(void) {
  // Discard the rest of the bytes we've been sent
  while (Serial.available()) {
    Serial.read();
  }

  Serial.write(ERR);
  Serial.flush();
  for(;;) {
    digitalWrite(13, HIGH);
    delay(75);
    digitalWrite(13, LOW);
    delay(200);
  }
}

void sendAck(void) {
  Serial.write(ACK);
  Serial.flush();
}

void sendRdy(void) {
  Serial.write(RDY);
  Serial.flush();
}

void loop() {
  while (Serial.available() == 0) { }
  
  byte length = Serial.read();
  if (length == END || length > 128) {
    // Stop receiving!
    for (;;) {
      digitalWrite(13, HIGH);
      delay(500);
      digitalWrite(13, LOW);
      delay(500);
    }
  }

  // Request the sender to start delivering those bytes!
  sendAck();

  char buf[128] = {0};

  for (int i = 0; i < length; i++) {
    while (!Serial.available()) { }
    int b = Serial.read();
    if (b >= 0 && b <= 0xff) {
      // Make sure this is a received byte, not an error (e.g. -1)
      buf[i] = b;
    }
    else {
      sendError();
    }
    
    bytesReceived++;
    
    if (bytesReceived > 131072) {
      sendError();
    }
  }

  sendAck(); // Tell the sender we got the data OK
  eeprom.write((byte *)buf, length);
  sendRdy(); // We're ready for the next chunk, if any
}
