# Arduino Data Sampler - Overview
This program continuously monitors analog input pins and records events in an HTTP-accessible continuous ("ring") buffer. I developed this program to monitor an indicator light on a piece of equipment using a photo-resistor taped onto the light and interfaced to an Arduino. I also use the same idea to monitor whether the lights have been left on in my basement and, in fact, **when** they had been turned on or off.

## Sampled Data Storage Format
Each sample reading is stored as a triple packed into 32 bits:

    [ time : value : pin ]

where:

* time:  19 bits. This is in units of 128 millisecond ticks since boot. It wraps approximately every 18 hours.
* value: 10 bits. 0 .. 1023 corresponding to Arduino D/A analog input range.
* pin:    3 bits. 0 .. 7.

The goal of packing the data this way is to maximize the number of samples that can be stored in the ring buffer even on small (standard) 2K RAM Arduino boards. Currently the ring buffer can hold 248 entries (992 bytes of memory) even on a 2K Arduino (the remainder of memory being needed for program stack, HTTP/network buffers, etc).


## Network support
The code uses DHCP to get an IP address. I recommend you set up a static DHCP address in your home router and assign a hostname to the device that way. 

The examples in this document use "monitor" as the hostname/IP address of the device.

All access to the device is via HTTP on port 80. For example if you surf http://monitor/help you will see this:

    GET / -- just output a report
    GET /0 -- only output pin 0 values (works for 0 .. 9 only)
    GET /m -- available memory report
    GET /p -- output the program table
    GET /p?qstring -- load the program table from qstring
    GET /r -- resets the sample ring buffer
    GET /v0 -- immediately read value of input 0
      /v1, v2, v3 etc immediately read that pin
    GET /write-eeprom -- store program params into EEPROM
    GET /help -- this info
    GET /j -- JSON resource object

## Details
* [Programming Interface](https://github.com/outofmbufs/arduino-datamon/wiki/Programming-Interface)
* [Reading Data With a Browser](https://github.com/outofmbufs/arduino-datamon/wiki/Browser-Interface)
* [JSON Interface](https://github.com/outofmbufs/arduino-datamon/wiki/JSON)
* [Administration Commands](https://github.com/outofmbufs/arduino-datamon/wiki/Administration)
