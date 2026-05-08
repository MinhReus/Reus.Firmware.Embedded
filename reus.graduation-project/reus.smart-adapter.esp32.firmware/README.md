# ESP32 Smart Adapter Firmware

## Overview

This firmware project is developed using the ESP-IDF framework for the ESP32 platform.

The system operates as a BLE GATT Server for wireless communication and remote load control.

Main functions:
- BLE GATT Server communication
- ACS712 current monitoring
- Relay load switching
- GPIO and ADC control
- Real-time embedded processing

This project is fully developed with ESP-IDF and does not use the Arduino framework.

---

# System Overview

```text
 Mobile App
     |
    BLE
     |
+----------------+
| ESP32 (ESP-IDF)|
| BLE GATTServer |
+--------+-------+
         |
    +----+----+
    |         |
    v         v
 ACS712    Relay Control
(Current)   (GPIO Output)