# home-assistant-adapter
Firmware for the ESP32C3-based Home Assistant Adapter.

This code is modified from https://github.com/geappliances/home-assistant-adapter with the following changes:
- Use GEA2 protocol instead of GEA3 (GEA2 is used by older GE appliances)

This code currently has very minimal function. It reads model and serial number and publishes to MQTT. 

### To-do:
- [ ] Add support for determining which ERDs are available from the connected appliance
- [ ] Add Home Assistant MQTT auto-discovery for the supported ERDs

### Not Planned:
- [ ] Add support for writing to the ERDs which support writes

## Hardware
The Home Assistant Adapter consists of a [Xiao ESP32C3](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/) and [carrier board](doc/schematic-v1.0.pdf) that breaks out the serial interface of the Xiao to an RJ45 jack.

## Setup
- Install [PlatformIO](https://platformio.org/)
- Copy `config/Certificate.h.sample` to `config/Certificate.h` and add your certificate (if any)
- Copy `config/Config.h.sample` to `config/Config.h` and add your WiFi credentials, MQTT configuration, and your device ID


