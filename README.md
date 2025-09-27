# home-assistant-adapter
Firmware for the ESP32C3-based Home Assistant Adapter.

This code is modified from https://github.com/geappliances/home-assistant-adapter with the change to use GEA2 protocol instead of GEA3 (GEA2 is used by older GE appliances)

This code currently will:
 - Execute a setup process if
   1. The adapter is plugged into an appliance that it can communicate with (by reading the model, serial number, and appliance type), and
   2. Either: a) All of the valid ERD addresses haven't already been discovered, or b) the appliance model/serial/type has changed from what was stored in memory
 - If executed, the setup process will:
   1. Read the model, serial number, and appliance type and publish to MQTT and store in the adapter's flash memory
   2. Reqest each of the 2386 ERD addresses, one at a time, to determine which are valid (this takes about 2 hours)
   3. Store the valid ERD addresses in memory, and also publish them to MQTT
 - When setup is completed, the code will then do nothing.

### To-do:
- [x] Add support for determining which ERDs are available from the connected appliance
- [ ] Subscribe to the valid ERDs and publish the data to MQTT
- [ ] Add Home Assistant MQTT auto-discovery for the supported ERDs

### Not Planned:
- [ ] Add support for writing to the ERDs which support writes

## Hardware
The Home Assistant Adapter consists of a [Xiao ESP32C3](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/) and [carrier board](doc/schematic-v1.0.pdf) that breaks out the serial interface of the Xiao to an RJ45 jack.

## Setup
- Install [PlatformIO](https://platformio.org/)
- Copy `config/Certificate.h.sample` to `config/Certificate.h` and add your certificate (if any)
- Copy `config/Config.h.sample` to `config/Config.h` and add your WiFi credentials, MQTT configuration, and your device ID


