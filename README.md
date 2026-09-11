# 🚨 LoRaWAN PIR Motion Detection System

A complete IoT security system for real-time motion detection using LoRaWAN technology, designed for African livestock farm security.

## 📋 Overview

This system detects :
- **Motion** via AM312 PIR sensor
- **Instant alerts** via LoRaWAN EU868
- **Web dashboard** with sound alarm
- **Multiple locations** support

---

## 🏗️ System Architecture

```
ATmega328P Node (Security Node)
├── AM312 PIR Sensor
└── RFM95W LoRa Radio (EU868)
        │
        │ LoRaWAN ABP
        ▼
RAK5146 Gateway (Raspberry Pi 4B)
        │
        ▼
ChirpStack v4 Network Server
        │
        └── PIR Security Dashboard (port 5001)
```

---

## 🛠️ Hardware

| Component | Specification |
|-----------|--------------|
| MCU | ATmega328P-P (DIP-28) |
| LoRa Radio | RFM95W 868MHz |
| PIR Sensor | AM312 (3.3V) |

---

## 📌 Pin Configuration

```
AM312 PIR    ATmega328P
─────────    ──────────
VCC      →   3.3V
GND      →   GND
OUT      →   D3 (interrupt pin)

RFM95W     ATmega328P
───────    ──────────
VCC    →   3.3V
GND    →   GND
SCK    →   D13
MISO   →   D12
MOSI   →   D11
NSS    →   D10
RST    →   D9
DIO0   →   D2
```

---

## 🚀 Quick Start

### 1. Gateway Setup
```bash
cd ~/chirpstack-docker
docker compose up -d
cd ~/sx1302_hal/packet_forwarder
sudo ./lora_pkt_fwd
```

### 2. Dashboard Setup
```bash
pip install flask paho-mqtt --break-system-packages
nohup python3 webapp/app.py > /dev/null 2>&1 &
```

### 3. ChirpStack Setup
- Create device profile : `ATmega-EU868-PIR` (ABP, no OTAA)
- Add decoder from `chirpstack/decoders/pir_decoder.js`
- Register device and get ABP keys
- Update firmware with new keys

---

## 🌐 Dashboard

| URL | Description |
|-----|-------------|
| http://yam.local:5001 | PIR Motion Dashboard |

### Features :
- 🚨 **Real-time motion alerts** with blinking red screen
- 🔔 **Sound alarm** when motion detected
- 🔕 **Stop Alert** button to silence alarm
- 📍 **Location tagging** per sensor
- 📊 **Event history** (last 20 events)
- 📡 **RSSI signal strength** display

---

## 📡 LoRaWAN Configuration

| Parameter | Value |
|-----------|-------|
| Region | EU868 |
| Activation | ABP |
| Spreading Factor | SF7 |
| Bandwidth | 125 kHz |
| Sync Word | 0x34 |
| Payload | 1 byte (0x01=DETECTED) |

---

## 🗂️ Repository Structure

```
pir-monitoring/
├── README.md
├── firmware/
│   └── atmega328p/
│       └── Intrusion.ino              # ATmega328P firmware
├── chirpstack/
│   └── Intrusion_decoder.js            # ChirpStack decoder
├── hardware/
│   └── Intrusion.kicad_pcb
|   └── Intrusion.kicad_pro
|   └── Intrusion.kicad_sch
├── webapp/
│   └── app.py                        # PIR motion dashboard
└── scripts/
    └── start_all.sh                  # Auto-start script
```

---

## 📍 Adding Locations

Edit `webapp/app.py` :
```python
DEVICE_LOCATIONS = {
    "PIR-node-1": "Main Gate JKUAT",
    "PIR-node-2": "Back Gate",
    "PIR-node-3": "Field A",
}
```

---

## 📄 License

MIT License — Free to use for educational and commercial purposes.

---

## 👨‍💻 Author

Wendyam Clovis Dubois Zongo MSc Electrical Engineering (Computer Engineering), PAUSTI / JKUAT, Nairobi Embedded systems and FPGA engineer
