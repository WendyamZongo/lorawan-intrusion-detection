/**
 * LoRaWAN PIR Motion Detection Firmware
 *
 * Hardware : ATmega328P + RFM95W + AM312 PIR
 * Radio    : RFM95W 868MHz (LoRaWAN ABP EU868)
 *
 * Pin Configuration:
 * AM312 PIR OUT -> D3 (interrupt)
 * RFM95 NSS     -> D10
 * RFM95 RST     -> D9
 * RFM95 DIO0    -> D2
 * RFM95 SCK     -> D13
 * RFM95 MISO    -> D12
 * RFM95 MOSI    -> D11
 *
 * Behaviour:
 * - MCU sits in power-down sleep, woken only by the PIR interrupt
 * - On wake, presence must persist past the sensor's own hold time
 *   before an uplink is sent (see confirmPresence)
 * - Cooldown after a confirmed alert to bound uplink rate
 *
 * Author: Wendyam Clovis Dubois Zongo
 * License: MIT
 */

#include <SPI.h>
#include <LoRa.h>
#include <EEPROM.h>
#include <avr/sleep.h>
#include <avr/power.h>

#include "secrets.h"   // NwkSKey, AppSKey, DevAddr. Gitignored.

// --- PIR PIN -------------------------------------------------
#define PIR_PIN 3   // Interrupt pin

// --- DETECTION FILTERING -------------------------------------
//
// The AM312 drives its output high for roughly 2 s after any trigger,
// however brief the movement was. Sampling the pin for less than that
// therefore proves nothing: a bird and an intruder produce the same
// initial pulse.
//
// The filter waits past the sensor's own hold time. If the output is
// still high at DWELL_MS, the sensor has retriggered, which means the
// presence actually continued. Transients drop out at ~2 s and are
// discarded without spending airtime or battery.
#define DWELL_MS        3000UL
#define DWELL_SAMPLE_MS 50UL

// Minimum interval between two confirmed alerts.
#define COOLDOWN_MS     30000UL

// --- FRAME COUNTER PERSISTENCE -------------------------------
// SRAM survives power-down sleep but not a power cycle. The network
// server rejects a frame counter that goes backwards, so it is mirrored
// to EEPROM. Intrusion events are infrequent, so EEPROM endurance is
// not a practical concern here.
#define EEPROM_FCNT_ADDR 0

uint16_t frameCounter = 0;

volatile bool motionFlag = false;

// --- AES-128 S-BOX -------------------------------------------
const uint8_t sbox[256] PROGMEM = {
  0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
  0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
  0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
  0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
  0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
  0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
  0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
  0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
  0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
  0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
  0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
  0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
  0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
  0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
  0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
  0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

// --- AES HELPER FUNCTIONS ------------------------------------
uint8_t gmul(uint8_t a, uint8_t b) {
  uint8_t p = 0;
  for (int i = 0; i < 8; i++) {
    if (b & 1) p ^= a;
    bool hbs = a & 0x80;
    a <<= 1;
    if (hbs) a ^= 0x1b;
    b >>= 1;
  }
  return p;
}

void aes128_encrypt(uint8_t* key, uint8_t* in, uint8_t* out) {
  uint8_t state[16], w[176];
  memcpy(state, in, 16);
  memcpy(w, key, 16);
  const uint8_t rcon[10] = {0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};
  for (int i=4;i<44;i++) {
    uint8_t tmp[4];
    memcpy(tmp,w+(i-1)*4,4);
    if (i%4==0) {
      uint8_t t=tmp[0];
      tmp[0]=pgm_read_byte(&sbox[tmp[1]])^rcon[i/4-1];
      tmp[1]=pgm_read_byte(&sbox[tmp[2]]);
      tmp[2]=pgm_read_byte(&sbox[tmp[3]]);
      tmp[3]=pgm_read_byte(&sbox[t]);
    }
    for (int j=0;j<4;j++) w[i*4+j]=w[(i-4)*4+j]^tmp[j];
  }
  for (int i=0;i<16;i++) state[i]^=w[i];
  for (int round=1;round<=10;round++) {
    for (int i=0;i<16;i++) state[i]=pgm_read_byte(&sbox[state[i]]);
    uint8_t tmp;
    tmp=state[1]; state[1]=state[5]; state[5]=state[9]; state[9]=state[13]; state[13]=tmp;
    tmp=state[2]; state[2]=state[10]; state[10]=tmp;
    tmp=state[6]; state[6]=state[14]; state[14]=tmp;
    tmp=state[3]; state[3]=state[15]; state[15]=state[11]; state[11]=state[7]; state[7]=tmp;
    if (round<10) {
      for (int c=0;c<4;c++) {
        uint8_t s0=state[4*c],s1=state[4*c+1],s2=state[4*c+2],s3=state[4*c+3];
        state[4*c]   = gmul(s0,2)^gmul(s1,3)^s2^s3;
        state[4*c+1] = s0^gmul(s1,2)^gmul(s2,3)^s3;
        state[4*c+2] = s0^s1^gmul(s2,2)^gmul(s3,3);
        state[4*c+3] = gmul(s0,3)^s1^s2^gmul(s3,2);
      }
    }
    for (int i=0;i<16;i++) state[i]^=w[round*16+i];
  }
  memcpy(out,state,16);
}

void xor16(uint8_t* a, uint8_t* b, uint8_t* out) {
  for (int i=0;i<16;i++) out[i]=a[i]^b[i];
}

void computeMIC(uint8_t* msg, uint8_t msgLen, uint8_t* mic) {
  uint8_t b0[16] = {
    0x49,0x00,0x00,0x00,0x00,0x00,
    (uint8_t)(DevAddr),(uint8_t)(DevAddr>>8),
    (uint8_t)(DevAddr>>16),(uint8_t)(DevAddr>>24),
    (uint8_t)(frameCounter),(uint8_t)(frameCounter>>8),
    0x00,0x00,0x00,(uint8_t)msgLen
  };
  uint8_t K1[16]={0},K2[16]={0},L[16]={0},tmp[16]={0};
  aes128_encrypt(NwkSKey,tmp,L);
  bool msb=L[0]&0x80;
  for (int i=0;i<15;i++) K1[i]=(L[i]<<1)|(L[i+1]>>7);
  K1[15]=(L[15]<<1)^(msb?0x87:0x00);
  msb=K1[0]&0x80;
  for (int i=0;i<15;i++) K2[i]=(K1[i]<<1)|(K1[i+1]>>7);
  K2[15]=(K1[15]<<1)^(msb?0x87:0x00);
  uint8_t buf[32]={0};
  memcpy(buf,b0,16);
  memcpy(buf+16,msg,msgLen);
  int totalLen=16+msgLen;
  int nBlocks=(totalLen+15)/16;
  uint8_t X[16]={0},Y[16],last[16]={0};
  for (int i=0;i<nBlocks-1;i++) {
    xor16(X,buf+i*16,Y);
    aes128_encrypt(NwkSKey,Y,X);
  }
  int lastLen=totalLen%16;
  if (lastLen==0) lastLen=16;
  memcpy(last,buf+(nBlocks-1)*16,lastLen);
  if (lastLen==16) xor16(last,K1,last);
  else { last[lastLen]=0x80; xor16(last,K2,last); }
  xor16(X,last,Y);
  aes128_encrypt(NwkSKey,Y,X);
  memcpy(mic,X,4);
}

void encryptPayload(uint8_t* payload, uint8_t len, uint8_t* encrypted) {
  uint8_t A[16] = {
    0x01,0x00,0x00,0x00,0x00,0x00,
    (uint8_t)(DevAddr),(uint8_t)(DevAddr>>8),
    (uint8_t)(DevAddr>>16),(uint8_t)(DevAddr>>24),
    (uint8_t)(frameCounter),(uint8_t)(frameCounter>>8),
    0x00,0x00,0x00,0x01
  };
  uint8_t S[16];
  aes128_encrypt(AppSKey,A,S);
  for (int i=0;i<len;i++) encrypted[i]=payload[i]^S[i];
}

// --- INTERRUPT -----------------------------------------------
void onMotion() {
  motionFlag = true;
}

// --- SEND LORAWAN --------------------------------------------
void sendLoRa(uint8_t status) {
  uint8_t plainPayload[1] = { status };
  uint8_t encPayload[1];
  encryptPayload(plainPayload, 1, encPayload);

  uint8_t msg[10];
  msg[0] = 0x40;
  msg[1] = DevAddr & 0xFF;
  msg[2] = (DevAddr >> 8) & 0xFF;
  msg[3] = (DevAddr >> 16) & 0xFF;
  msg[4] = (DevAddr >> 24) & 0xFF;
  msg[5] = 0x00;
  msg[6] = frameCounter & 0xFF;
  msg[7] = (frameCounter >> 8) & 0xFF;
  msg[8] = 0x01;
  msg[9] = encPayload[0];

  uint8_t mic[4];
  computeMIC(msg, 10, mic);

  LoRa.beginPacket();
  LoRa.write(msg, 10);
  LoRa.write(mic, 4);
  LoRa.endPacket();

  frameCounter++;
  EEPROM.put(EEPROM_FCNT_ADDR, frameCounter);
}

// --- PRESENCE CONFIRMATION -----------------------------------
// Returns true only if the PIR output is still asserted after the
// sensor's own hold time has elapsed, meaning it retriggered and the
// presence is sustained rather than a transient.
bool confirmPresence() {
  uint32_t start = millis();
  while (millis() - start < DWELL_MS) {
    if (digitalRead(PIR_PIN) == LOW) {
      return false;   // sensor released: transient, discard
    }
    delay(DWELL_SAMPLE_MS);
  }
  return true;
}

// --- SLEEP ---------------------------------------------------
void enterSleep() {
  LoRa.sleep();

  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  noInterrupts();
  sleep_enable();
  attachInterrupt(digitalPinToInterrupt(PIR_PIN), onMotion, RISING);
  interrupts();
  sleep_cpu();

  // Execution resumes here after the PIR interrupt fires.
  sleep_disable();
  detachInterrupt(digitalPinToInterrupt(PIR_PIN));
}

// --- SETUP ---------------------------------------------------
void setup() {
  Serial.begin(9600);
  Serial.println(F("=== LoRaWAN PIR node ==="));

  pinMode(PIR_PIN, INPUT);

  // Restore the frame counter so uplinks are not rejected as replays
  // after a power cycle.
  EEPROM.get(EEPROM_FCNT_ADDR, frameCounter);
  if (frameCounter == 0xFFFF) frameCounter = 0;   // blank EEPROM
  Serial.print(F("Frame counter restored: "));
  Serial.println(frameCounter);

  // Reset RFM95
  pinMode(9, OUTPUT);
  digitalWrite(9, LOW);
  delay(10);
  digitalWrite(9, HIGH);
  delay(10);

  LoRa.setPins(10, 9, 2);
  if (!LoRa.begin(868.1E6)) {
    Serial.println(F("LoRa init failed"));
    // Do not spin forever on a battery node: sleep and let the
    // watchdog-free reset path retry on the next power event.
    while (1) {
      set_sleep_mode(SLEEP_MODE_PWR_DOWN);
      sleep_enable();
      sleep_cpu();
    }
  }
  LoRa.setSyncWord(0x34);
  LoRa.enableCrc();

  // The ADC is unused and would otherwise draw current in sleep.
  ADCSRA = 0;

  Serial.println(F("Armed. Sleeping until motion."));
  Serial.flush();
}

// --- LOOP ----------------------------------------------------
void loop() {
  enterSleep();

  if (motionFlag) {
    motionFlag = false;

    if (confirmPresence()) {
      Serial.println(F("Presence confirmed, sending alert"));
      sendLoRa(1);
      Serial.print(F("Alert sent, next frame "));
      Serial.println(frameCounter);
      Serial.flush();
      delay(COOLDOWN_MS);
    } else {
      Serial.println(F("Transient discarded"));
      Serial.flush();
    }
  }
}
