# arduino-datamon
This program implements an Arduino analog data monitor. It constantly reads a number of analog inputs and stores samples into a continuous ("ring") buffer when those samples meet criteria that you can define.

The ring buffer can be accessed via a browser or via a JSON/HTTP interface.

## New Aug 23 2015
Fixed the PROGMEM stuff so it will compile and work once again with the newer build tools. Sorry about that!

## Features
* Sampling criteria are programmable. Input pins, recording thresholds, time parameters, "analog" vs "digital" mode.
* Optimized RAM Usage. Can store 248 samples (992 bytes) on the standard/small 2K RAM arduino.
* Web interface. You can manage the device completely via your web browser.
* JSON HTTP interface. For programmatic access to sample data sp you don't have to screen-scrape the web interface to read the data.
* Can be used simply to get real-time network access to the Arduino A/D.
* Can be programmed to simply sample a pin every N milliseconds and keep the last 248 or so samples.

## Documentation Wiki
See the [wiki](https://github.com/outofmbufs/arduino-datamon/wiki) for documentation.

