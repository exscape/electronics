#include <I2C16.h>
#include <EEPROM_24XX1025.h>
#include <SPI.h>
#include <DAC_MCP49x1.h>

// For the interrupt timer
#include <avr/io.h>
#include <avr/interrupt.h>

// The wave format chunk. Usually located at 12 bytes into the file
struct WAVE_format {
  char fmt[4]; /* should be "fmt " for this chunk type */
  uint32_t fmtChunkSize;
  uint16_t audioFormat;
  uint16_t numChannels;
  uint32_t sampleRate;
  uint32_t byteRate;
  uint16_t blockAlign;
  uint16_t bitsPerSample;
  uint16_t extraFormatBytes; /* how many bytes follow in this chunk? */
} __attribute__((packed));

// Define a simple buffer type, that stores both the data, and the data length
#define BUFSIZE 128
typedef struct {
  byte buffer[BUFSIZE];
  uint8_t length;
} buffer_t;

// One buffer is used for writing to (EEPROM -> buffer), while the other
// is read from (buffer -> DAC), after which they are swapped.
buffer_t buf1, buf2;
buffer_t *readBuffer = &buf1, *writeBuffer = &buf2;

// Initialize the two libraries we're using
DAC_MCP49x1 dac(DAC_MCP49x1::MCP4901, 10);
EEPROM_24XX1025 eeprom(0, 0);

void error() {
  // Stop the timer, and beep until reset
  TCCR1B = 0;
  while(true) {
    for (int i=0; i < 70; i++) {
      dac.output(0);
      delayMicroseconds(2000);
      dac.output(127);
      delayMicroseconds(2000);
    }

    delay(1500);
  }
}

uint32_t waveDataPosition = 0, waveDataLength = 0;

void setup() {
  Serial.begin(115200);

  // Set up the DAC to the fastest possible operation
  dac.setSPIDivider(SPI_CLOCK_DIV2);
  dac.setPortWrite(true);

  // Read the wave format
  // 12 is the number of bytes in the first header; 16 a semi-magic number for
  // the number of bytes to read after the struct, covering some unknowns
  eeprom.setPosition(0);
  eeprom.read(0, buf1.buffer, 12 + sizeof(struct WAVE_format) + 16);

  // Parse the wave format data, to make sure we can understand it
  // etc. Also, find the location and length of the audio data.
  // This chunk is usually located 12 bytes in.
  struct WAVE_format *fmt = (struct WAVE_format *)(buf1.buffer + 12);
  uint32_t sampleRate = fmt->sampleRate; // cache, since the buffer will be overwritten soon
  if (strncmp(fmt->fmt, "fmt ", 4) == 0) {
    // Valid format chunk
    if (fmt->numChannels != 1 || fmt->audioFormat != 1
       || fmt->bitsPerSample != 8 || fmt->sampleRate > 20200) 
     {
      // Invalid format for this program
      Serial.println("ERROR: invalid format (not mono/PCM/8-bit/max 20.2 kHz sample rate)");
      error();
    }
  }
  else {
    // Not a format chunk. We can't deal with this.
    Serial.println("Wave data format chunk wasn't where I expected it to be!");
    error();
  }

  // Again, 12 is the size of the first chunk, which is always the same
  uint32_t waveChunkPosition = 12 + sizeof(struct WAVE_format) + fmt->extraFormatBytes;
  
  // Is this a data chunk?
  if (strncmp((const char *)(buf1.buffer + waveChunkPosition), "data", 4) != 0) {
    Serial.println("ERROR: the data chunk was not where I expected");
    error();
  }

  // These are the important things from the WAVE data (apart from sample rate):
  // where the stuff to play is!  
  waveDataPosition = waveChunkPosition + 8; /* 8 for "data" + data length (32 bits) */
  waveDataLength = *((uint32_t *)( (const char *)(buf1.buffer + waveChunkPosition + 4))); // Ugh
  
  // Read the first chunk before the timer is started, so that we always
  // have a full buffer ready  
  eeprom.setPosition(waveDataPosition);
  eeprom.read(buf1.buffer, min(BUFSIZE, waveDataLength));
  buf1.length = min(BUFSIZE, waveDataLength);
  readBuffer = &buf1;
  writeBuffer = &buf2;
  
  // Calculate how many cycles are between each sample update (ORC1A register)
  // Formula: (1/samplerate)/(clock cycle length) - 1
  float inv_sample = 1.0f / ((float)sampleRate);
  float cycles_tmp = inv_sample/(0.0000000625f); // 62.5 ns = clock cycle time at 16 MHz
  uint32_t cycles = (uint32_t)cycles_tmp;
  // Take care of the "-1" part, if that brings us closer to the real value
  if (cycles_tmp - cycles <= 0.5)
    cycles--;
    
  Serial.print("CPU cycles per sample: "); Serial.println(cycles);
  
  // Set up the timer. No prescaling, and fire every OCR1A + 1 (IIRC) cycles  
  cli();
  TCCR1A = 0;
  TCCR1B = 0;
  OCR1A = cycles;
  TCCR1B |= (1 << WGM12);
  TCCR1B |= (1 << CS10);
  TIMSK1 |= (1 << OCIE1A);
  sei();
}

volatile uint8_t waitForSwap = 1; // used to wait until the next buffer is "free to use"
uint32_t bytesRead = 0; // how many bytes read this (audio) loop

// So, the main idea here is rather simple:
// The main loop() reads from the EEPROM. When finished, it waits for the playback
// to finish with its current buffer. When it does so, it swaps the buffers,
// starts playing the one we last wrote, and clears waitForSwap so that the loop()
// function can start reading the next chunk. This repeats over and over.
// Oh, and the playback is called by a timer interrupt every 1/sampleRate seconds
// (approximately every 50 microseconds at ~20 kHz).

void loop() {
  // We need this temporary variable. If we do writeBuffer->length = eeprom.read(...)
  // and then bytesRead += writeBuffer->length, the results will be incorrect.
  // The ISR swaps the buffers *between* the execution of the two lines of code, and so
  // the bytesRead is incremented incorrectly, and bad playback results.
  uint32_t tmp = eeprom.read(writeBuffer->buffer, min(BUFSIZE, waveDataLength - bytesRead));
  writeBuffer->length = tmp;
  bytesRead += tmp;
  
  // Loop when we reach the end
  if (bytesRead >= waveDataLength) {
    eeprom.setPosition(waveDataPosition);
    bytesRead = 0;
  }
  
  waitForSwap = 1;
  while (waitForSwap) {
    // The ISR will break this loop when it's time to do so.
    // We do this so that we don't start overwriting data before
    // it's been played back.
  }
}

// How many bytes we are into the buffer.
uint8_t playbackPosition = 0;

ISR(TIMER1_COMPA_vect) {
  dac.output(readBuffer->buffer[playbackPosition++]);
  
  if (playbackPosition >= readBuffer->length) {
    // We've read the entire contents of this buffer; swap them!
    buffer_t *tmp = readBuffer;
    readBuffer = writeBuffer;
    writeBuffer = tmp;
    playbackPosition = 0;
    waitForSwap = 0; // Tell the EEPROM reader/buffer writer loop to keep going now
  }
}
