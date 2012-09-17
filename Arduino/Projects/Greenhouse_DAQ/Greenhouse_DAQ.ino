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
 */

#include <SPI.h>
#include <Ethernet.h>
#include <OneWire.h>

// Pin definitions
#define WIZRST 8
#define ONEWIRE_PIN 9

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
  { { 0x10, 0x3e, 0x3a, 0x2f, 0x02, 0x08, 0x00, 0xef }, false },
  { { 0x10, 0x05, 0x5d, 0x2f, 0x02, 0x08, 0x00, 0xba }, false }
};

OneWire ds(ONEWIRE_PIN);

// Set up the Ethernet connection
byte mac[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
IPAddress ip(192, 168, 2, 177);
IPAddress serverip(192, 168, 2, 60); // TODO: make this server IP static!
const uint16_t serverport = 40100;
EthernetClient client;

void panic(const char *str) {
  for(;;) {
    Serial.print("PANIC: ");
    Serial.println(str);
    delay(2000);
    // TODO: blink status LEDs, when on the final PCB
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
      goto restart; //yaay. Still, adding a second loop to "make it acceptable" (no goto)
      // is not really better, is it?
    }

    for (int i=0; i < NUM_SENSORS; i++) {
      if (memcmp(devices[i].addr, addr, 8) == 0) {
        // Found one of the sensors
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
  
  int tries = 0;
  restart_temp:
  
  ds.reset();
  ds.select(devices[dev].addr);
  ds.write(0x44); // CONVERT T command = 44h

  // Devices sends all 0 bits while converting (not on parasitic power!)
  while (ds.read() == 0) {
    delayMicroseconds(50);
  }

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
  
  if (OneWire::crc8(data, 8) != crc) {
    tries++;
    if (tries > 5) {
      panic("Invalid data CRC, multiple times!");
    }
    goto restart_temp;
  }

  float temp; // The actual temperature

// Prettier names without extra RAM use. Ugly, yes, but this *is* embedded after all
#define LSB (data[0])
#define MSB (data[0])
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
  }

  return temp;
}

void setup() {
  resetWiznet();
  delay(1000);
  
  Serial.begin(115200);
  
  findTemperatureSensors();
  
  Ethernet.begin(mac, ip, dns);

  // Set up Timer1 for 1 Hz operation
  cli();
  TCCR1A = 0;
  TCCR1B = 0;
  OCR1A = 15624;
  TCCR1B |= (1 << WGM12);
  TCCR1B |= (1 << CS10) | (1 << CS12); // 1024 prescaler
  TIMSK1 |= (1 << OCIE1A);
  sei();
}

volatile boolean time_to_work = false;

// ISR is called every second; exactly once every 10 seconds
// isn't possible (at 16 MHz clock) without custom code:
ISR(TIMER1_COMPA_vect) {
static byte count = 0;
  count++;
  if (count >= 10) {
    count = 0;
  
  // Wake the task loop!
  time_to_work = true;  
  }
}

void loop() {
  while (time_to_work == false) { 
    // Flag is cleared by ISR
  }
  
  Serial.println("Starting temperature sampling...");
  float sensor_0 = readTemperature(0);
  float sensor_1 = readTemperature(1);
  
  Serial.print("Sensor 0: ");
  Serial.print(sensor_0);
  Serial.println(" C");

  Serial.print("Sensor 1: ");
  Serial.print(sensor_1);
  Serial.println(" C");
  
  if (client.connected()) {
    Serial.println("Still connected! Disconnecting...");
    client.flush();
    client.stop();
  }
    
  if (client.connect(serverip, serverport)) {
    Serial.println("Connected to server");
    Serial.print("Sending data... ");
    // .print() only shows 2 decimals, and
    // sprintf doesn't accept floats at all.
    // Workaround: convert to signed integer, transmit,
    // convert back on receiving end
    client.print((int32_t) (sensor_0 * 10000));
    client.print(":");
    client.print((int32_t) (sensor_1 * 10000));
    client.print((char)0x0);
    Serial.println("data sent, waiting for reply...");

   uint32_t start = millis();
   while (client.available() == 0 && millis() < start + 2000 && millis() >= start) { 
     // millis() >= start prevents an overflow to lock up the loop
     // Wait until server has responded, with a timeout of 2 seconds
   }
   uint32_t end_ = millis();
   Serial.print(end_ - start);
   Serial.println(" ms waited for available() to become nonzero");
   
   Serial.print("Reply: [");
   while (client.available()) {
     Serial.print((char)client.read());
   }
   Serial.println("]");
   while (client.connected()) {
     Serial.println("Waiting for disconnect...");
   }
   Serial.println("Calling client.stop()");
   client.stop();
  }
  else {
    Serial.println("Connection failed!");
    client.stop();
  }
  Serial.println("");
  time_to_work = false;
}
