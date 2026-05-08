# ESP32 Smart Adapter Hardware

## Overview

This hardware system uses an ESP32 as the main controller for wireless load control and current monitoring.

Main functions:
- BLE GATT Server communication
- ACS712 current sensing
- Relay load switching
- MIC5219 low-dropout voltage regulator
- Remote load ON/OFF control

---

# System Overview

```text
      Input Power
            |
            v
      +-------------+
      |   MIC5219   |
      |  3.3V LDO   |
      +------+------+ 
             |
             v
         +-------+
         | ESP32 |
         +---+---+
             |
      +------+------+
      |             |
      v             v
   ACS712         Relay
(Current Sense) (Load Control)
                       |
                       v
                Electrical Load