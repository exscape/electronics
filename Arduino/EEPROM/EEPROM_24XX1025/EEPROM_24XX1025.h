#ifndef _24XX1025_H
#define _24HH1025_H

#include <I2C16.h>

class EEPROM_24XX1025 {
public:
  EEPROM_24XX1025(byte A0, byte A1) 
  { 
    I2c16.begin();
    I2c16.setSpeed(true); // set 400 kHz clock frequency
    curpos = 0;
    eeprom_pos = 0xffffffff;
    devaddr = 0x50 /* 1010 binary (shifted left), see datasheet */ | (A1 << 2) | (A0 << 1); 
  }

  uint32_t getPosition(void) { 
    return curpos; 
  }
  boolean setPosition(uint32_t pos) { 
    if (pos < 131072) {
      curpos = pos; /* eeprom_pos is UNCHANGED! */
      return true;
    }
    else
      return false;
  }

  byte read(void);
  uint32_t read(byte *data, uint32_t bytesToRead); // reads from curpos
  uint32_t read(uint32_t fulladdr, byte *data, uint32_t bytesToRead);

  boolean write(byte data);
  uint32_t write(byte *data, uint32_t bytesToWrite); // writes at curpos
  uint32_t write(uint32_t fulladdr, byte *data, uint32_t bytesToWrite);

private:
  uint8_t  devaddr;
  uint32_t curpos; // 16 bits only covers half of 128 kiB, we need 17 bits... so 32 it is
  uint32_t eeprom_pos; // a "copy" of the EEPROMs *INTERNAL* counter

  uint8_t writeSinglePage(uint32_t fulladdr, byte *data, uint8_t bytesToWrite); // never spans multiple pages
  uint8_t readChunk(uint32_t fulladdr, byte *data, uint8_t bytesToRead);  // reads a small chunk
  uint8_t writeChunk(uint32_t fulladdr, byte *data, uint8_t byteToWrite); // writes a small chunk
};

#endif
