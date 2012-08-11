#include "EEPROM_24XX1025.h"

/*
 * Microchip 24XX1025 I2C EEPROM driver for Arduino
 * Tested with: Arduino Uno R3, Microchip 24LC1025 (5 V, 400 kHz I2C)
 * Should work with: all Arduino compatible boards, 24XX1025 models
 *
 * Written by Thomas Backman <serenity@exscape.org>, August 2012
 *
 * Uses a modified version of Wayne Truchsess' I2C Master library:
 * http://dsscircuits.com/articles/arduino-i2c-master-library.html
 * Changes were made to support 16-bit addresses and acknowledge polling.
 * The unmodified version WILL NOT WORK with this code!
 *
 * License: MIT/BSD. Basically, do whatever you want, but credit me. Don't say you wrote this.
 * You CAN modify the code (and spread the modifications) IF you credit me for the original
 * code, AND add a note that you've changed it.
 */

#define DEVICE_SIZE 131072

// Finds the block number (0 or 1) from a 17-bit address
#define BLOCKNUM(addr) (( (addr) & (1UL << 16)) >> 16)

// Converts a "full" 17-bit address to the 16-bit page address used by the EEPROM
// The block number (above) is also required, of course, but is sent separately
// in the device address byte.
#define TO_PAGEADDR(addr) ((uint16_t)(addr & 0xffff))

// Undoes the previous two. (Better safe than sorry re: parenthesis and casts, doesn't cost anything!)
#define TO_FULLADDR(block, page) (((uint32_t)(((uint32_t)block) << 16)) | (((uint32_t)(page))))

// Private method
uint8_t EEPROM_24XX1025::readChunk(uint32_t fulladdr, const void *data, uint8_t bytesToRead) {
  if (bytesToRead == 0 || fulladdr >= DEVICE_SIZE)
    return 0;
  if (fulladdr + bytesToRead > DEVICE_SIZE)
    bytesToRead = DEVICE_SIZE - fulladdr;

  uint8_t err = 0;
  if (fulladdr < 65536 && fulladdr + bytesToRead > 65536) {
    // This read crosses the "block boundary" and cannot be sequentially read
    // by the EEPROM itself

    // Read part 1 (from the first block)
    err = I2c16.read(devaddr, fulladdr /* always 16-bit */, 65536 - fulladdr, (byte *)data);
    if (err) {
      eeprom_pos = 0xffffffff;
      return 0;
    }

    // Read part 2 (from the second block)
    err = I2c16.read(devaddr | (1 << 2), 0, bytesToRead - (65536 - fulladdr), (byte *)data + (uint16_t)((65536 - fulladdr)));
    if (err) {
      eeprom_pos = 0xffffffff;
      curpos += (65536 - fulladdr); // move the cursor forward the amount we read successfully
      if (curpos >= DEVICE_SIZE)
        curpos %= DEVICE_SIZE;
      return (uint8_t)(65536 - fulladdr); // num bytes read previously
    }
    else {
      eeprom_pos = TO_FULLADDR(1, bytesToRead - (65536 - fulladdr));
      curpos += bytesToRead;
      if (curpos >= DEVICE_SIZE)
        curpos %= DEVICE_SIZE;
      return bytesToRead;
    }
  }
  else {
    // Doesn't cross the block border, so we can do this in one read
    uint8_t block = BLOCKNUM(fulladdr);
    err = I2c16.read(devaddr | (block << 2), TO_PAGEADDR(fulladdr), bytesToRead, (byte *)data);
    if (err) {
      eeprom_pos = 0xffffffff;
      return 0;
    }
    else {
      eeprom_pos += bytesToRead;
      curpos += bytesToRead;
      if (curpos >= DEVICE_SIZE)
        curpos %= DEVICE_SIZE;
      return bytesToRead;
    }
  }
}

// Private method
uint8_t EEPROM_24XX1025::writeSinglePage(uint32_t fulladdr, const void *data, uint8_t bytesToWrite) {
  // Writes 1 - 128 bytes, but only *within a single page*. Never crosses a page/block border.
  // Enforcing this is up to the caller.
  if (bytesToWrite == 0 | bytesToWrite > 128)
    return 0;

  uint8_t ret = I2c16.write(devaddr | ((BLOCKNUM(fulladdr)) << 2), TO_PAGEADDR(fulladdr), (byte *)data, bytesToWrite);
  if (ret != 0) {
    // We can't be sure what the internal counter is now, since it looks like the write failed.
    eeprom_pos = 0xffffffff;
    return 0;
  }
  else {
    // Try to keep track of the internal counter
    eeprom_pos += bytesToWrite;
    curpos += bytesToWrite;
    if (curpos >= DEVICE_SIZE)
      curpos %= DEVICE_SIZE;
  }

  // Wait for the EEPROM to finish this write. To do so, we use acknowledge polling,
  // a technique described in the datasheet. We sent a START condition and the device address
  // byte, and see if the device acknowledges (pulls SDA low) or not. Loop until it does.
  uint32_t start = micros();
  while (I2c16.acknowledgePoll(devaddr | ((BLOCKNUM(fulladdr)) << 2)) == 0) {
    delayMicroseconds(20);
  }
  uint32_t end = micros();

  if (end - start < 500) {
    // This write took less than 500 us (typical is 3-4 ms). This most likely means
    // that the device is write protected, as it will acknowledge new commands at once
    // when write protect is active.
    Serial.println("WARNING: EEPROM appears to be write protected!");
    return 0;
  }

  return bytesToWrite;
}

// Private method
uint8_t EEPROM_24XX1025::writeChunk(uint32_t fulladdr, const void *data, uint8_t bytesToWrite) {
  // Used to turn 1-128 byte writes into full page writes (i.e. turn them into proper single-page writes)
  if (bytesToWrite == 0 || bytesToWrite > 128 || fulladdr >= DEVICE_SIZE)
    return 0;

  if (fulladdr + bytesToWrite > DEVICE_SIZE)
    bytesToWrite = DEVICE_SIZE - fulladdr;

  // Calculate the 16-bit address, and the page number of the first and second (if applicable)
  // blocks we're going to write to.
  uint32_t pageaddr = TO_PAGEADDR(fulladdr);
  uint8_t firstBlock = BLOCKNUM(fulladdr);
  uint8_t secondBlock = BLOCKNUM(fulladdr + bytesToWrite - 1);

  // These page numbers are *relative to the block number*, i.e. firstPage = 0 may mean at byte 0 or byte 65536
  // depending on firstBlock above. Same goes for secondPage/secondBlock of course.
  uint16_t firstPage = pageaddr / 128; // pageaddr is already relative to block!
  uint16_t secondPage = (TO_PAGEADDR(pageaddr + bytesToWrite - 1))/128;

  if (firstPage == secondPage && firstBlock == secondBlock) {
    // Data doesn't "cross the border" between pages. Easy!
    return writeSinglePage(fulladdr, data, bytesToWrite);
  }
  else {
    // The data spans two pages, e.g. begins at address 120 and is 12 bytes long, which would make it go
    // past the edge of this page (addresses 0 - 127) and onto the next.
    // We need to split this write manually.

    uint8_t bytesInFirstPage = ((firstPage + 1) * 128) - pageaddr;
    uint8_t bytesInSecondPage = bytesToWrite - bytesInFirstPage;

    uint8_t ret = 0;

    // Write the data that belongs to the first page
    if ((ret = writeSinglePage(TO_FULLADDR(firstBlock, pageaddr), data, bytesInFirstPage))
      != bytesInFirstPage)
    {
      return ret;
    }

    // Write the data that belongs to the second page
    if ((ret = writeSinglePage(TO_FULLADDR(secondBlock, secondPage * 128), (const void*)((byte *)data + bytesInFirstPage), bytesInSecondPage))
      != bytesInSecondPage)
    {
      return bytesInFirstPage + ret;
    }
  }

  return bytesToWrite;
}

byte EEPROM_24XX1025::readByte(void) {
  // Reads a byte from the current position and returns it

  if (eeprom_pos != curpos) {
    // If the EEPROM internal position counter has (or might have) changed,
    // do a "full" read, where we sent the 16-byte address.
    I2c16.read((uint8_t)(devaddr | ((BLOCKNUM(curpos)) << 2)), TO_PAGEADDR(curpos), 1U);
    eeprom_pos = curpos;
  }
  else {
    // If we know that the internal counter is correct, don't send the address, but
    // rely on the EEPROM logic to return the "next" byte properly. This saves
    // overhead and time.
    I2c16.read((uint8_t)(devaddr | ((BLOCKNUM(curpos)) << 2)), 1U);
  }

  curpos++;
  eeprom_pos++;
  if (eeprom_pos == 65536) {
    // Seems to wrap here. The datasheet could be read as if this were 17-bit, but I don't think it is.
    eeprom_pos = 0;
  }
  if (curpos >= DEVICE_SIZE) {
    // Wrap around if we overflow the device capacity.
    curpos %= DEVICE_SIZE;
    eeprom_pos = 0xffffffff;
  }

  return I2c16.receive(); // Returns 0 if no bytes are queued
}

uint32_t EEPROM_24XX1025::read(const void *data, uint32_t bytesToRead) {
  return read(curpos, data, bytesToRead);
}

uint32_t EEPROM_24XX1025::read(uint32_t fulladdr, const void *data, uint32_t bytesToRead) {
  if (bytesToRead == 0)
    return 0;
  if (bytesToRead <= 255)
    return readChunk(fulladdr, data, bytesToRead); // can be handled without this function
  if (fulladdr + bytesToRead >= DEVICE_SIZE)
    bytesToRead = DEVICE_SIZE - fulladdr; // constrain read size to end of device

  const uint32_t chunksize = 240; // bytes to read per chunk. Must be smaller than 255.

  // If we get here, we have a >255 byte read that is now constrained to a valid range.
  uint32_t bytesRead = 0;
  uint32_t t = 0;

  while (bytesRead < bytesToRead) {
    t = readChunk(fulladdr + bytesRead, (const void*)((byte *)data + bytesRead), min(chunksize, bytesToRead - bytesRead));
    if (t == min(chunksize, bytesToRead - bytesRead))
      bytesRead += t;
    else
      return bytesRead; // Failure!
  }

  return bytesRead;
}

boolean EEPROM_24XX1025::writeByte(byte data) {
  // Writes a byte to the EEPROM.
  // WARNING: writing a single byte still uses a full page write,
  // so writing 128 sequential bytes instead of 1 page write
  // will use 128 times as many of the chip's limited lifetime writes!!
  // In otherwords: ONLY USE THIS if you *really* only need to write ONE byte.
  // Even for just *TWO* bytes, write([pos,] data, bytesToWrite) is "twice as good"!
  // In short: writeBlock for 128 bytes will use 1 page "life" each on 1 or 2 pages.
  // writeByte 128 times will use 128 page "lives", spread over 1 or 2 pages.

  // Find which block the byte is in, based on the full (17-bit) address.
  // We can only supply 16 bits to the EEPROM, plus a separate "block select" bit.
  uint8_t block = BLOCKNUM(curpos);

  uint8_t ret = I2c16.write((uint8_t)(devaddr | (block << 2)), TO_PAGEADDR(curpos), data);
  if (ret != 0) {
    // Looks like something failed. Reset the EEPROM counter "copy", since we're no longer
    // sure what it ACTUALLY is.
    eeprom_pos = 0xffffffff;
    return false;
  }

  curpos++;
  eeprom_pos = curpos; // We changed the internal counter when we wrote the address just above.
  if (curpos >= DEVICE_SIZE) {
    // Wrap around if we overflow the device capacity.
    curpos %= DEVICE_SIZE;
    eeprom_pos = 0xffffffff; // Not sure what the internal counter does. It PROBABLY resets to 0, but...
  }

  // Wait for the EEPROM to finish this write. To do so, we use acknowledge polling,
  // a technique described in the datasheet. We sent a START condition and the device address
  // byte, and see if the device acknowledges (pulls SDA low) or not. Loop until it does.
  uint32_t start = micros();
  while (I2c16.acknowledgePoll(devaddr | (block << 2)) == 0) {
    delayMicroseconds(20);
  }
  uint32_t end = micros();

  if (end - start < 500) {
    // This write took less than 500 us (typical is 3-4 ms). This most likely means
    // that the device is write protected, as it will acknowledge new commands at once
    // when write protect is active.
    Serial.println("WARNING: EEPROM appears to be write protected!");
    return 0;
  }

  return true; // success
}

uint32_t EEPROM_24XX1025::write(const void *data, uint32_t bytesToWrite) {
  return write(curpos, data, bytesToWrite);
}

uint32_t EEPROM_24XX1025::write(uint32_t fulladdr, const void *data, uint32_t bytesToWrite) {
  // Uses writeChunk to allow any-sized writes, not just <128 bytes

  if (bytesToWrite == 0)
    return 0;
  if (bytesToWrite <= 128)
    return writeChunk(fulladdr, data, bytesToWrite);
  if (fulladdr + bytesToWrite >= DEVICE_SIZE)
    bytesToWrite = DEVICE_SIZE - fulladdr; // constrain read size to end of device

  // If we get here, we have a >128 byte write that is now constrained to a valid range.
  uint32_t bytesWritten = 0;
  uint32_t t = 0;

  while (bytesWritten < bytesToWrite) {
    t = writeChunk(fulladdr + bytesWritten, (const void*)((byte *)data + bytesWritten), min(128, bytesToWrite - bytesWritten));
    if (t == min(128, bytesToWrite - bytesWritten))
      bytesWritten += t;
    else
      return bytesWritten; //Failure!
  }

  return bytesWritten;
}

//
// Helper functions for reading/writing other forms of data (floats and ints)
//

float EEPROM_24XX1025::readFloat(void) {
  float data;
  if (read(curpos, (const void*)&data, sizeof(float)) == sizeof(float))
    return data;
  else
    return NAN;
}

boolean EEPROM_24XX1025::writeFloat(float data) {
  if (write(curpos, (const void*)&data, sizeof(float)) == sizeof(float))
    return true;
  else
    return false;
}

uint32_t EEPROM_24XX1025::readUInt(void) {
  uint32_t data;
  if (read(curpos, (const void*)&data, sizeof(uint32_t)) == sizeof(uint32_t))
    return data;
  else
    return 0;
}

boolean EEPROM_24XX1025::writeUInt(uint32_t data) {
  if (write(curpos, (const void*)&data, sizeof(uint32_t)) == sizeof(uint32_t))
    return true;
  else
    return false;
}

int32_t EEPROM_24XX1025::readInt(void) {
  int32_t data;
  if (read(curpos, (const void*)&data, sizeof(int32_t)) == sizeof(int32_t))
    return data;
  else
    return 0;
}

boolean EEPROM_24XX1025::writeInt(int32_t data) {
  if (write(curpos, (const void*)&data, sizeof(int32_t)) == sizeof(int32_t))
    return true;
  else
    return false;
}
