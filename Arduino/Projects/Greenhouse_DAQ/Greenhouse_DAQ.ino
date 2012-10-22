/*
 * Temperature sampler for a greenhouse temperature monitor
 * Samples the temperature from 2 sensors, and sends it via Ethernet to a
 * Python server. The Python server saves it to an RRD database,
 * which is accessed by a PHP/jQuery website to produce graphs according to 
 * user input.
 *
 * Hardware: 
 * * Custom PCB (also tested w/ Arduino Uno R3 + breadboard)
 * * Atmel Atmega328P-PU (DIP)
 * * WIZnet WIZ820io SPI Ethernet module
 * * 2x Maxim DS18S20 1-wire temperature sensors
 *
 * Thomas Backman, 2012
 * serenity@exscape.org
 * http://blog.exscape.org
 */
 
 ///
 /// TODO: Improve stability; use warn() instead of panic(), so that operation can resume
 /// for transient events!
 ///

#include <SPI.h>
#include <Ethernet.h>
#include <OneWire.h>

// Pin definitions
#define WIZRST 8
#define ONEWIRE_PIN 9
#define NET_LED A0
#define STATUS_LED A1

// If the reading differs by more degrees than this,
// re-read it to make sure it's not a one-off error.
// Set to a low value since there's little harm in re-reading,
// and rapid changes are unexpected here.
#define MAX_DELTA 4

// The current UNIX timestamp. Updated from the server now and then,
// and updated by the 1 Hz timer
volatile uint32_t current_time = 0;

// Simple alternative to a true associative array
typedef struct {
  byte addr[8];
  boolean found;
} device_list_t;

// A list of the DS18S20 sensors used.
// All addresses need to be listed here, so that we can be sure
// that the sensors won't "change place" as with a dynamic detection.
// "Sensor 1" must always remain the same sensor, etc.
#define NUM_SENSORS 2
device_list_t devices[NUM_SENSORS] = 
{
//  { { 0x10, 0x3e, 0x3a, 0x2f, 0x02, 0x08, 0x00, 0xef }, false }, // Old sensor 0
//  { { 0x10, 0x05, 0x5d, 0x2f, 0x02, 0x08, 0x00, 0xba }, false }  // Old sensor 1
    { { 0x10, 0x66, 0x5d, 0x8d, 0x02, 0x08, 0x00, 0x8e }, false }, // Sensor 0
    { { 0x10, 0x88, 0xbd, 0x8d, 0x02, 0x08, 0x00, 0x6e }, false } // Sensor 1
};

// Store the last reading, to remove outliers.
// The sensors sometimes return 85 C (their default value). Since this *may* be valid
// (though that is insanely unliksetupely for *this* project), we compare it to the previous reading.
float last_reading[NUM_SENSORS];

OneWire ds(ONEWIRE_PIN);
volatile boolean done_starting_up = false;

// Used to give each data packet (and response packet) a unique ID.
// Won't wrap: 2^32 times 10 seconds per packet = ~1361 years!
// ... and that would also require 100% consecutive uptime!
uint32_t seq = 0;

// Set up the Ethernet connection
// 00:08:DC = WIZnet; the rest is randomized
byte mac[] = { 0x00, 0x08, 0xDC, 0x14, 0xCE, 0x08 };
IPAddress ip(192, 168, 2, 177);
IPAddress serverip(192, 168, 2, 60); // TODO: make this IP static on the server side!
const uint16_t serverport = 40100;
const uint16_t localport = 40100; // why not?
EthernetUDP udp;

void setStatusLED(boolean on) {
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, on ? LOW : HIGH);
}

void setNetLED(boolean on) {
  pinMode(NET_LED, OUTPUT);
  digitalWrite(NET_LED, on ? LOW : HIGH);
}

void blinkStatusOnce(unsigned short ms_lit) {
  setStatusLED(true);
  delay(ms_lit);
  setStatusLED(false);
}

void blinkNetOnce(unsigned short ms_lit) {
  setNetLED(true);
  delay(ms_lit);
  setNetLED(false);
}

void sendPing(void) {
  // Sends a "PING" UDP packet and hopes to receive a "PONG". Simple connectivity test,
  // nothing more.
  udp.begin(localport);
  udpSendPacket("PING");
  char buf[4] = {0};
  int ret = udpRecvPacket(buf, 4, 2000);
  udp.flush();
  udp.stop();

  if (ret > 0 && strncmp(buf, "PONG", 4) == 0) {
    setNetLED(true);
    Serial.println("PONG received!");
  }
  else {
    setNetLED(false);
    Serial.println("WARNING: PONG not received!");
  }
}

void panic(const char *str) {
  // Attempt to tell the server, which then sends an email
  const int BUFSIZE = 96;
  char buf[BUFSIZE] = {0};
  sprintf(buf, "PANIC SEQ %lu MSG %s", seq, str);
  Serial.print("Sending panic message: ");
  Serial.println(buf);

  udp.flush();
  udp.begin(localport);
  udpSendPacket(buf);

  memset(buf, 0, BUFSIZE);
  int ret = udpRecvPacket(buf, BUFSIZE, 2000);
  if (ret <= 0 || (buf[0] != 'O' && buf[1] != 'K')) {
    Serial.println("Unable to send panic message!");
  }

  udp.flush();
  udp.stop();

  for(;;) {
    Serial.print("PANIC: ");
    Serial.println(str);

    setStatusLED(true);
    setNetLED(true);
    delay(500);

    setStatusLED(false);
    setNetLED(false);
    delay(500);
  }
}

void resetWiznet(void) {
  pinMode(WIZRST, OUTPUT);
  digitalWrite(WIZRST, LOW);
  delayMicroseconds(250); // hold at least 2 µs, though more won't hurt
  digitalWrite(WIZRST, HIGH);
  delay(500); // wait 150+ ms for PLL to stabilize
}

void addr_to_str(const byte *addr, char *addr_str) {
  // Ugh. Well, this was the quick way to do it.
  sprintf(addr_str, "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
  addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], addr[6],
  addr[7]);
}

// Searches for the temperature sensors, and makes sure
// that they are all found
void findTemperatureSensors(void) {
  int tries = 0;
  restart:
  
  // Reset the bus, so that we "start over" at the first device
  ds.reset_search();

  // All 1-wire comms start with a bus reset
  ds.reset();

  byte addr[8]; // addresses are 64 bits each
  while (ds.search(addr)) {
    if (OneWire::crc8(addr, 7) != addr[7]) {
      tries++;
      if (tries > 5) {
        panic("Device address CRC error, multiple times! Halting!");
      }
      goto restart; // Yay. Still, adding a second loop to "make it acceptable" (no goto)
      // is not really better, is it?
    }

    Serial.print("found sensor: ");
    char buf[40] = {0};
    addr_to_str(addr, buf);
    Serial.println(buf);

    for (int i=0; i < NUM_SENSORS; i++) {
      if (memcmp(devices[i].addr, addr, 8) == 0) {
        // Found one ofsetup the sensors
        devices[i].found = true;
      }
    }
  }

  // Did we find them all?
  for (int i=0; i < NUM_SENSORS; i++) {
    if (devices[i].found == false) {
      char buf[64] = {0};
      char *s = "Could not find sensor with address ";
      strcpy(buf, s);
      addr_to_str(devices[i].addr, buf + strlen(s));
      panic(buf);      
    }
  }
}

float readTemperature(int dev) {
  if (dev >= NUM_SENSORS) {
    panic("Invalid device number given to readTemperature()!");
  }
  
  int crc_tries = 0;
  bool have_retried_reading = false;
  restart_temp:
  
  ds.reset();
  ds.select(devices[dev].addr);
  ds.write(0x44); // CONVERT T command = 44h

  // Devices sends all 0 bits while converting (not on parasitic power!)
  while (ds.read() == 0) {
    delayMicroseconds(50);
  }
  delay(5);

  ds.reset();
  ds.select(devices[dev].addr);
  ds.write(0xBE); // READ SCRATCHPAD command = BEh

  // Read back the data
  byte data[8] = {0};
  byte crc;

  for (int i=0; i<8; i++) {
    data[i] = ds.read();
  }
  crc = ds.read();
  
  // Check for all ones (eight 0xff bytes)
  char count = 0;
  for (int i=0; i < 8; i++) {
    if (data[i] == 0xff)
      count++;
    else break;
  }
  if (count == 8) {
    panic("All ones received - a sensor is most likely disconnected!");
  }

  if (OneWire::crc8(data, 8) != crc) {
    crc_tries++;
    if (crc_tries > 5) {
      panic("Invalid data CRC, multiple times!");
    }
    goto restart_temp;
  }

  float temp; // The actual temperature

// Prettier names without extra RAM use. Ugly, yes, but this *is* embedded after all
#define LSB (data[0])
#define MSB (data[1])
#define count_remain (data[6])
#define count_per_c (data[7])

  // Is the temperature negative?
  boolean below_zero = (MSB == 0xff) ? true : false;

  if (below_zero) {
    // Temperature is below 0! Use the simpler, less precise formula:
    temp = -1 * ((0xff - LSB + 1)/2.0f);
  }
  else {
    // Temperature is positive; use the full-resolution (1/16 C) formula,
    // mostly to get "less digital-looking" graphs, even though the added 
    // resolution doesn't mean added *accuracy*.
    temp = (LSB >> 1) - 0.25 + (count_per_c - count_remain)/((float)count_per_c);

    // NEW: shift away one LSB, since that bit is only noise anyway - the graph often jumps
    // between two values, back and forth, for hours.
    // temp = (LSB >> 1) - 0.25 + ((count_per_c - count_remain) >> 1)/((float)(count_per_c >> 1));
  }

  // Remove extreme outliers. Since we sample very often, big changes between two readings
  // are very unlikely.
  if ((data[0] == 0xAA || fabs(last_reading[dev] - temp) >= MAX_DELTA) && have_retried_reading == false) {
    // 0xAA (85 C) is the sensor's default value (before taking readings), and is suspect,
    // since it's higher than we'd expect for this sensor. Retry if that value is read.
    // Also retry if the difference between the last reading and this reading is too big.
    // (This will always retry the first time after boot; doesn't really matter.)
    Serial.print("WARNING: retrying reading for sensor ");
    Serial.print(dev);
    Serial.print("; reading differs too much from previous reading (delta C: ");
    Serial.print(fabs(last_reading[dev] - temp));
    Serial.println(").");
    have_retried_reading = true;
    goto restart_temp;
  }

  last_reading[dev] = temp;

  return temp;
}

void setup() {
  setStatusLED(true);
  pinMode(WIZRST, OUTPUT);
  setStatusLED(false);
  setNetLED(false);
  resetWiznet();
  delay(1000);
  
  // Enable internal pullups for all unused pins
  // 3 through A5 (i.e. the right-hand side) are the addon pins
  unsigned char unused_pins[] = {4, 5, 6, 7, 3, A2, A3, A4, A5};
  for (int i=0; i < sizeof(unused_pins); i++) {
    pinMode(i, INPUT);
    digitalWrite(i, HIGH);
  }

  Serial.begin(115200);

  Ethernet.begin(mac, ip, dns);
  sendPing();
  
  findTemperatureSensors();

  // Set up Timer1 for 1 Hz operation
  cli();
  TCCR1A = 0;
  TCCR1B = 0;
  OCR1A = 15624; // 16 MHz / 1024 / 15625 [sic] = exactly 1 Hz
  TCCR1B |= (1 << WGM12);
  TCCR1B |= (1 << CS10) | (1 << CS12); // 1024 prescaler
  TIMSK1 |= (1 << OCIE1A);
  sei();
}

volatile boolean time_to_work = false;
// ISR is called every second; exactly once every 10 seconds
// isn't possible (at 16 MHz clock) without custom code:
ISR(TIMER1_COMPA_vect) {
  // Blink the status LED until we're up and running
  static boolean prev_led = false;
  if (!done_starting_up) {
    setStatusLED(!prev_led);
    prev_led = !prev_led;
  }

  static byte count = 0;
  count++;
  current_time++;
  if (count >= 10) {
    count = 0;
  
  // Wake the task loop!
  time_to_work = true;  
  }
}

void udpSendPacket(const char *str) {
  udp.beginPacket(serverip, serverport);
  udp.write(str);
  udp.endPacket();
}

int udpRecvPacket(char *buf, uint16_t maxsize, uint16_t timeout) {
  // buf: Where to store the data
  // maxsize: how many bytes to store (i.e. typically the buffer size)
  // timeout: timeout in milliseconds
  // Return value: negative for errors, 0 for timeout, otherwise number of bytes
  // received.

  int packetSize = 0;
  uint32_t start = millis();

  // Wait until we receive a reply, with a timeout, and also break
  // if the millis() counter wraps (every 49.7 days)
  // We *might* time out early once every 49.7 days, which I won't bother fixing.
  do {
    packetSize = udp.parsePacket();
  }
  while(packetSize <= 0 && millis() < start + timeout && millis() >= start);

  if (packetSize == 0) {
    // We timed out! No data was received in time.
    return 0;
  }

  return udp.read(buf, maxsize);
}

void loop() {
  while (time_to_work == false) { 
    // Flag is cleared by ISR
  }

  Serial.println("Starting temperature sampling...");

  Serial.print("Sensor 0: ");
  float sensor_0 = readTemperature(0);
  Serial.print(sensor_0);
  Serial.println(" C");

  Serial.print("Sensor 1: ");
  float sensor_1 = readTemperature(1);
  Serial.print(sensor_1);
  Serial.println(" C");
  
  // .print() only shows 2 decimals, and
  // sprintf doesn't accept floats at all.
  // Workaround: convert to signed integer, transmit,
  // convert back on receiving end
  char buf[34] = {0};
  sprintf(buf, "%ld:%ld SEQ %lu",
    (int32_t)(sensor_0 * 10000),
    (int32_t)(sensor_1 * 10000),
    seq);

  udp.begin(localport);
  udpSendPacket(buf);

  memset(buf, 0, 34);
  int ret = 0;
  ret = udpRecvPacket(buf, 33, 2000);
  if (ret <= 0) {
    setNetLED(false);
    Serial.println("Failed to receive data: timeout/error");
    blinkNetOnce(150);

    // TODO: "cache" data if current_time is set properly (> 1348512615 for example.
    // since it starts out at 0, any big number means it'll have synced the time prior
    // to the downtime.)
    goto loop_end;
  }
  /* else success */

  setNetLED(true);

  Serial.print("Response: [");
  Serial.print(buf);
  Serial.println("]");

  uint32_t recv_seq;
  char stat[8];
  memset(stat, 0, 8);

  // Disable interrupts while modifying the timestamp, just in case
  cli();
  sscanf(buf, "%s SEQ %lu TIME %lu", stat, &recv_seq, &current_time);
  sei();
//  Serial.print("Time is now: ");
//  Serial.println(current_time);

  if (strcmp(stat, "OK") != 0) {
    Serial.println("Invalid response! Status is not OK");
    // TODO: handle error. For now, ignore it
  }
  else {
    blinkStatusOnce(150);
    done_starting_up = true;
  }

  if (recv_seq != seq) {
    Serial.print("Invalid response! SEQ = "); Serial.print(recv_seq);
    Serial.print(", but should have been "); Serial.println(seq);
    // TODO: handle error
  }

loop_end:
  seq++;
  udp.flush();
  udp.stop();

  Serial.println("");
  time_to_work = false;
}
