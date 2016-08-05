// echo -n "@255000000#" | nc -4u -w0 192.168.5.10 8888
#include <ESP8266WiFi.h>
#include <WiFiUDP.h>

#include "Secrets.h"

#define DEBUG true

// WIFI
IPAddress ip(192, 168, 5, 10);
IPAddress gateway(192,168,5,1);
IPAddress subnet(255,255,255,0);

// mac address
uint8_t MAC_array[6];
char MAC_char[18];

// time between connection status checks (seconds)
#define WIFICHECKTIMEOUT 60
unsigned long lastCheckedWifiStatus = millis();

// UDP
WiFiUDP UDP;
#define UDP_PORT 8888

// LED
const double BRIGHTNESS = 1.0;//1.0;
#define NUM_LEDS     10  // maximum LED node number we can receive
// 10W nodes do not have a zero address
#define BEGIN_LED 0
// duration to (approx) run an sd card animation for
#define ANIMATIONDURATION 60
// wait this long for udp packets before fallback
#define SILENCETIMEOUT 10
uint8_t incoming_leds[NUM_LEDS*3] = {0};
uint8_t incoming_state = 0; // 0=none, 1=node
uint8_t incoming_index = 0;
uint8_t incoming_led;
uint8_t incoming_red;
uint8_t incoming_green;
uint8_t incoming_blue;
uint8_t outgoing_leds[NUM_LEDS*3] = {0};
int udpAvail = 0;

// use this to calibrate when re-assembled
//uint8_t led_map[NUM_LEDS] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19};
//uint8_t led_map[NUM_LEDS] = {12,19, 0, 9,17, 4, 5,16, 7, 6, 1,11,15, 3,13, 8,10,14,18, 2};
uint8_t led_map[NUM_LEDS] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

#define LEDPIN 2

// prevent LED, power savings, "off"
boolean lights_out = false;

// fallback animation
unsigned long lastHeardDataAt = millis();
int globalHue = 0;

#ifdef DEBUG
 #define DP(x)  Serial.print(x)
 #define DPL(x)  Serial.println(x)
 #define DPLD(x,y)  Serial.println(x,y)
#else
 #define DP(x)
 #define DPL(x)
 #define DPLD(x,y)
#endif

void setup() {

  Serial.begin(115200);
  Serial1.begin(115200);  // TX1 is GPIO2

  delay(100); // iono

  connectToWifi();

  // UDP  ----------------------------------------------------------------------
  while (!UDP.begin(UDP_PORT)) {
    delay(250);
  }
  Serial.println("UDP ready");

}

// copy data from incoming_leds[] to outgoing_leds[]
// this code determines which LEDs are mapped to Serial1
// remember, the 10W leds begin at position 1! our arrays are 0 indexed
// basically, the address written on the led is a lie, if it says 1, the bits
// have it at position 0. organize the outgoing data accordingly
void reorder_nodes_serial1(void)
{
  // zero the array
  memset(outgoing_leds, 0, sizeof(outgoing_leds));

  for (int i=0; i < NUM_LEDS; i++) {

    if (led_map[i] > 0) {
      outgoing_leds[(led_map[i] - BEGIN_LED)*3+0] = int(incoming_leds[i*3+0] * BRIGHTNESS);
      outgoing_leds[(led_map[i] - BEGIN_LED)*3+1] = int(incoming_leds[i*3+1] * BRIGHTNESS);
      outgoing_leds[(led_map[i] - BEGIN_LED)*3+2] = int(incoming_leds[i*3+2] * BRIGHTNESS);
    }

  }
}

void parseIncoming() {

  while (udpAvail > 0) {

    unsigned char c = UDP.read();

    // turn out the lights
    if (c == '$') {

      lights_out = true;
      // send a black frame to make sure they all turn off
      setColor(0,0,0);

      Serial.println("off");

    } else if (c == '%') {

      lights_out = false;

      Serial.println("on");

    } else if (!lights_out) {

      lastHeardDataAt = millis();

      if (c == '@') {

        incoming_state = 1;
        incoming_index = 0;
        incoming_led = 0;

      } else if (c == '#') {

        startFrame();
        //reorder_nodes_serial1();
        memcpy(outgoing_leds, incoming_leds, sizeof(incoming_leds));
        Serial1.write(outgoing_leds, sizeof(outgoing_leds));
        Serial1.flush();

        incoming_state = 0;
      } else {
        if (incoming_state == 1) {

          // decode incoming LED data as it arrives
          if (c >= '0' && c <= '9') {
            int n = c - '0';
            switch (incoming_index) {
            case  0: incoming_red   = n * 100; break;
            case  1: incoming_red   += n * 10; break;
            case  2: incoming_red   += n;      break;
            case  3: incoming_green = n * 100; break;
            case  4: incoming_green += n * 10; break;
            case  5: incoming_green += n;      break;
            case  6: incoming_blue  = n * 100; break;
            case  7: incoming_blue  += n * 10; break;
            case  8: incoming_blue  += n;      break;
            default: incoming_index = 0;       break;
            }
            incoming_index++;
            if (incoming_index >= 9) {
              if (incoming_led < NUM_LEDS) {
                n = incoming_led * 3;
                incoming_leds[n+0] = incoming_red;
                incoming_leds[n+1] = incoming_green;
                incoming_leds[n+2] = incoming_blue;
                incoming_led++;
              }
              incoming_index = 0;
            }
          }
        }
      }

    }

    udpAvail = udpAvail - 1;
  }
}



void loop() {

  if (udpAvail) {
    parseIncoming();
  } else {
    udpAvail = UDP.parsePacket();
  }

  // nothing heard in a while.. do SOMETHING
  if (!lights_out && (lastHeardDataAt + SILENCETIMEOUT * 1000 < millis())) {

    fadeNext();
    delay(10);

  }

  // see if we need to reconnect to the wifi
  if (lastCheckedWifiStatus + WIFICHECKTIMEOUT * 1000 < millis()) {
    if (WiFi.status() != WL_CONNECTED) {
      connectToWifi();
    }
    lastCheckedWifiStatus = millis();
  }

  // allow the ESP to do what it do
  yield();
}



void startFrame() {
  Serial1.begin(38400);
  Serial1.write(0); // start of frame message
  Serial1.flush();
  delay(1); // ESP specific
  Serial1.begin(115200);
  Serial1.write(0);
}


void setColor(int r, int g, int b) {

  startFrame();

  for (int i=1; i <= NUM_LEDS; i++) {
    Serial1.write(int(r * BRIGHTNESS));
    Serial1.write(int(b * BRIGHTNESS));
    Serial1.write(int(g * BRIGHTNESS));
  }

  Serial1.flush();
}

void setLedColorHSV(int h, double s, double v) {
  //this is the algorithm to convert from RGB to HSV
  double r=0;
  double g=0;
  double b=0;

  double hf=h/60.0;

  int i=(int)hf;
  double f = h/60.0 - i;
  double pv = v * (1 - s);
  double qv = v * (1 - s*f);
  double tv = v * (1 - s * (1 - f));

  switch (i)
  {
  case 0: //rojo dominante
    r = v;
    g = tv;
    b = pv;
    break;
  case 1: //verde
    r = qv;
    g = v;
    b = pv;
    break;
  case 2:
    r = pv;
    g = v;
    b = tv;
    break;
  case 3: //azul
    r = pv;
    g = qv;
    b = v;
    break;
  case 4:
    r = tv;
    g = pv;
    b = v;
    break;
  case 5: //rojo
    r = v;
    g = pv;
    b = qv;
    break;
  }

  //set each component to a integer value between 0 and 255
  int red=constrain((int)255*r,0,255);
  int green=constrain((int)255*g,0,255);
  int blue=constrain((int)255*b,0,255);

  setColor(red,green,blue);
}

void fadeNext() {
  int hue = globalHue + 1;
  if (hue == 360) {
    hue = 0;
  }
  setLedColorHSV(hue, 1, 1); //We are using Saturation and Value constant at 1
  globalHue = hue;
}

void connectToWifi() {
  // WIFI Client ---------------------------------------------------------------
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  //bool config(IPAddress local_ip, IPAddress gateway, IPAddress subnet, IPAddress dns1 = (uint32_t)0x00000000, IPAddress dns2 = (uint32_t)0x00000000);
  WiFi.config(ip, gateway, subnet);
  WiFi.begin(ssid, password);

  pinMode(LEDPIN, OUTPUT);

  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LEDPIN, LOW);
    delay(250);
    Serial.print(".");
    digitalWrite(LEDPIN, HIGH);
    delay(250);
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.print("MAC address: ");

  memset(MAC_array, 0, sizeof(MAC_array)); // zero the array
  memset(MAC_char, 0, sizeof(MAC_char)); // zero the array

  WiFi.macAddress(MAC_array);
  for (int i = 0; i < sizeof(MAC_array); ++i){
    sprintf(MAC_char,"%s%02x:",MAC_char,MAC_array[i]);
  }
  Serial.println(MAC_char);
}
