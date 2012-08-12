Microchip 24XX1025 I2C EEPROM driver for Arduino
Tested with: Arduino Uno R3, 24LC1025 (5 V, 400 kHz I2C)
Should work with: all Arduino compatible boards, 24XX1025 models

Version 1.0 (August 12th, 2012)

Written by Thomas Backman <serenity@exscape.org>

Uses a modified version of Wayne Truchsess' I2C Master library:
http://dsscircuits.com/articles/arduino-i2c-master-library.html
Changes were made to support 16-bit addresses and acknowledge polling.
The unmodified version WILL NOT WORK with this code!
The modified library is available here:
https://github.com/exscape/electronics/tree/master/Arduino/Libraries/EEPROM/I2C16

License: MIT/BSD. Basically, do whatever you want, but credit me. Don't say
you wrote this. You CAN modify the code (and spread the modifications) IF you 
credit me for the original code, AND add a note that you've changed it.

Bare-minimum example (see the example folder for a slightly larger one)

/////////////////////////////////////////////////////////////

#include <I2C16.h>           // Don't miss this line!
#include <EEPROM_24XX1025.h>

EEPROM_24XX1025 eeprom (0, 0); // A0 and A1 address pins, see datasheet sec. 2

void setup() {
	Serial.begin(115200); // Make sure to use 115200 in the Serial Monitor too
}

void loop() {
	const int read_size = 16;
	char buf[read_size + 1] = {0}; /* + 1 for a terminating NULL byte */
	uint32_t read_position = 256; // where to read (the position in bytes)
	eeprom.read(read_position, buf, read_size);

	Serial.println(buf);
	delay(1000);
}

/////////////////////////////////////////////////////////////

Below is a full description of each method, public and private.
(Users need only know about the public methods; developers might be interested
in the private ones.)

-------------------------------------
Public methods, i.e. the ones you use
-------------------------------------

Some notes on types used below...
  These are signed integers (can store positive and negative values): short, int, long, int8_t, int16_t, int32_t
  These are unsigned integers (can only store positive, but goes twice as high): unsigned short, unsigned int, unsigned long, uint8_t,
  uint16_t, uint32_t
  There are helper functions provided for writing bytes (same as uint8_t), ints (int32_t), uints (uint32_t) and floats. If you really
  need to store 16-bit ints and save those 2 bytes, you can use e.g. eeprom.write(&my_int, sizeof(int));

Constructor (byte A0, byte A1)
  The constructor takes two address bits as arguments. These are set via the IC
  pins; see the datasheet. For 0, tie them to ground. For 1, tie them to VDD.
  If the library doesn't work at all, make sure that these are correctly
  specified. If you only have one EEPROM, I recommend using 0, 0.

uint32_t getPosition(void)
  Returns the current pointer position, i.e. the place in the EEPROM
  where read and write calls will take place (unless you specify a position).

boolean setPosition(uint32_t pos)
  Sets the current pointer position. Valid range: 0 - 131071 (inclusive)
  Returns true for success (value is in the valid range), false otherwise.

byte readByte(void)
  Reads a single byte from the current position (see above) and returns it.
  Returns 0 on failure.

float readFloat(void)
  Reads a single float from the current position (see above) and returns it.
  Returns NAN on failure.

uint32_t readUInt(void)
  Reads a single unsigned 32-bit integer from the current position (see above)
  and returns it.
  Returns 0 on failure.

int32_t readInt(void)
  Reads a single signed 32-bit integer from the current position (see above)
  and returns it.
  Returns 0 on failure.

uint32_t read(const void *data, uint32_t bytesToRead)
  Reads a block of data from the current position (above) into the array
  at "data".

uint32_t read(uint32_t fulladdr, const void *data, uint32_t bytesToRead)
  As above, except reads from the address specified.
  Valid address range is 0 - 131071 (inclusive).

boolean writeByte(byte data)
  Writes a single byte to the current position (see above).
  Returns true on success, false on failure.

  !!! WARNING !!!
  When writing any amount of data, including a single byte using this function,
  the EEPROM will refresh the entire page (128 bytes) of data.
  EEPROMs have a limited write endurance (often 1 million cycles per page).
  Therefore, if you write one byte at a time, instead of writing a full block
  at once, you are *wasting the chip's lifetime* (doing so is also MUCH slower).
  ONLY use this if you really only need to write ONE byte, and avoid it
  when you can.
  Using a block write (below), you can write 128 bytes and only use a single
  page write (or *at most* two, if the write is not aligned to a page boundary).

uint32_t write(const void *data, uint32_t bytesToWrite)
  Writes a block of data from the array "data" to the current position (above).
  Returns the number of bytes successfully written - in best cases. Failures
  may cause 0 to be returned even if some bytes were written; I haven't
  been able to test this thoroughly. I have also had no failures to date.

uint32_t write(uint32_t fulladdr, const void *data, uint32_t bytesToWrite)
  As above, except writes to the address specified.
  Valid address range is 0 - 131071 (inclusive).

boolean writeFloat(float)
  Writes a single float to the current position (see above).
  Returns true if successful, false otherwise.
  The same warning as for writeByte applies.

boolean writeUInt(uint32_t)
  Writes a single 32-bit unsigned integer to the current position (see above).
  Returns true if successful, false otherwise.
  The same warning as for writeByte applies.

boolean writeInt(int32_t)
  Writes a single 32-bit signed integer to the current position (see above).
  Returns true if successful, false otherwise.
  The same warning as for writeByte applies.

-----------------------------------------------------------------------------
Private methods (only if you want to modify or fully understand this library)
-----------------------------------------------------------------------------

uint8_t devaddr
  Used to store the I2C device address, minus the R/W bit.
  The 24XX1025 has the format 1010 BXY (7 bits), where B is the block select bit
  (basically the 17th address bit), X is the A1 chip select pin and Y is the
  A0 chip select pin. The A1/A0 bits are selected via physical pin wiring.

uint32_t curpos
  Stores the current position where the next read/write will take place (unless
  the caller specifies an address, of course). This is automatically "tracked".
  This is the full 17-bit address, including the block select bit.
  The TO_PAGEADDR() macro is used to convert this to a 16-bit page address,
  while the BLOCKNUM() macro is used to extract the block select bit.

uint32_t eeprom_pos
  This variable attemps to "track" the EEPROM chip's internal address counter.
  Why? Because when we do single-byte reads, we can do so without sending
  the address bytes, and reduce the overhead (and thus increase the speed).

  When sending the page address bytes, we send a total of three bytes
  (device address + two page address bytes) and read back one data byte.
  When we don't send the address bytes, i.e. when using this optimization, we
  send one byte (the device address) and read back one data byte, so the 
  overhead is cut from 75% to 50%, or total read time from (roughly, and at 
  400 kHz) perhaps 90 microseconds to 45 (roughly 2.5 us/bit, total 
  9 bits (8 bits + ACK/NACK) per byte -> 18 bits at 2.5 us).

uint8_t readChunk(uint32_t fulladdr, const void *data, uint8_t bytesToRead)
  Used internally by the block read functions, mostly because the EEPROM
  can't handle reads across the block boundary (the 65536th byte) natively.
  We need to "split" such reads manually, which is what this method does.

uint8_t writeSinglePage(uint32_t fulladdr, const void *data, uint8_t bytesToWrite)
  One of the two functions (the other being single-byte write) that actually
  writes to the EEPROM. The other block-write functions call this one after
  some processing.
  Can only write within a single page, so 1 - 128 bytes (if the write address
  starts on a page boundary, i.e. the address is evenly divisible by 128).

uint8_t writeChunk(uint32_t fulladdr, const void *data, uint8_t byteToWrite)
  Essentially the same as readChunk above, except the EEPROM can't handle writes
  across *page* boundaries, either (reads have ONE such boundary, while writes 
  have 1023). Splits 1-255 byte writes into 1-2 writes that are each only on a 
  single page.

---------------------------

That's it, folks!
I think that's fairly extensively documented (for such a small project), but if
you need help, wonder something, find a bug etc., or anything else - email me 
(and don't worry about doing so!).

Thomas Backman
serenity@exscape.org
