# Reading Data with a Browser
The sensor implements a web server. Assuming your arduino's hostname is "monitor", then the following URLs are available:

* `http://monitor` displays an overall report.
* `http://monitor/help` displays a help screen.
* `http://monitor/0` displays a report for pin 0. Use `/1`, `/2`, etc for other pins.
* `http://monitor/v0` reads and displays the current value of pin 0 and displays it.
* `http://monitor/m` displays a memory report.
* `http://p` displays the program table.
* `http://j` displays the top-level JSON object. See [JSON](https://github.com/outofmbufs/arduino-datamon/wiki/JSON)

In addition to the display commands, several administrative functions can be invoked via browser access. See [Administration](https://github.com/outofmbufs/arduino-datamon/wiki/Administration) and [Programming](https://github.com/outofmbufs/arduino-datamon/wiki/Programming-Interface).

# Full Ring Buffer General Data Report
Surfing `http://monitor/` will return something like this:
``` 
See /help for complete command list.
Now: 377888337 pack-masked: 42343936 uptime wrap-arounds: 0
pin 2 = 232 @ 25314176 deltaT 17029760 ID 0.42
pin 3 = 1001 @ 24468224 deltaT 845952 ID 0.41
pin 2 = 850 @ 21540992 deltaT 2927232 ID 0.40
pin 1 = 1022 @ 10068224 deltaT 11472768 ID 0.39
pin 0 = 1023 @ 10068096 deltaT 128 ID 0.38
pin 2 = 1023 @ 65276288 deltaT 11900672 ID 0.37
pin 3 = 1022 @ 55577088 deltaT 9699200 ID 0.36
pin 1 = 1023 @ 33977088 deltaT 21600000 ID 0.35
  .
  .
  .
```
The `Now` value reported is milliseconds since boot of the device. It wraps in just under 50 days; every time it wraps the "uptime wrap-arounds" value is incremented by 1. You can use this to compute a precise uptime for the device (you will need more than 32 bits for this calculation or you will need to scale it appropriately):

   uptimeInMsec = ("wrap-arounds" * (4 * 1024 * 1024 * 1024)) + "Now"

The `pack-masked` value isn't really useful for anything; I needed to know that at one point during development and debugging but it really shouldn't be in the report any more. Ignore it.

The format of the sample data is: 

   pin {pin#} = {inputReading} @ {timestamp} deltaT {dT} ID {Major.Minor}

The data points are reported most-recent-first (time goes backwards going down the page).

The timestamp is reported as milliseconds since boot of the device. Like uptime it wraps in just under 50 days.

The deltaT value on each line is just a computation of the delta of each line with the line above it, and for the very first entry it is a delta of that entry's timestamp and "now". This deltaT computation is not very useful for the mixed report shown above because the deltaT values usually end up being between timestamps from different pin data points. The deltaT will be discussed in more detail below for the individual pin report where it is actually useful.

The ID uniquely identifies this data point (as long as the device has not been rebooted). The _Minor_ number is basically a position within the ring buffer. It starts at zero and runs up to the maximum size of the buffer (usually around 247 or so) and then wraps around back to zero. Every time the _Minor_ wraps to zero the _Major_ is incremented by one. You can use these numbers to help you chain-together multiple buffer readings from the device if you are using an external program as they will help you figure out where the current data should be matched together with any previous data you read from the device. However, if the device reboots note that the numbers start over again.

# Pin-specific Data Report
You can (and usually should) limit the report to one data pin at a time. This makes the deltaT computation useful and just makes more sense in general. 

Surfing `http://monitor/2` will limit the output to just pin 2 (for example):

```
Now: 379724551 pack-masked: 44180224 uptime wrap-arounds: 0
pin 2 = 232 @ 25314176 deltaT 18866048 ID 0.42
pin 2 = 850 @ 21540992 deltaT 3773184 ID 0.40
pin 2 = 1023 @ 65276288 deltaT 23373568 ID 0.37
pin 2 = 1023 @ 29276288 deltaT 36000000 ID 0.33
pin 2 = 1023 @ 60385152 deltaT 36000000 ID 0.31
pin 2 = 1019 @ 24385152 deltaT 36000000 ID 0.27
pin 2 = 1021 @ 55494016 deltaT 36000000 ID 0.23
pin 2 = 1019 @ 19494016 deltaT 36000000 ID 0.19
pin 2 = 926 @ 50602880 deltaT 36000000 ID 0.15
pin 2 = 850 @ 30816256 deltaT 19786624 ID 0.13
pin 2 = 1019 @ 4903168 deltaT 25913088 ID 0.9
pin 2 = 925 @ 36012032 deltaT 36000000 ID 0.5
pin 2 = 850 @ 10547072 deltaT 25464960 ID 0.4
pin 2 = 1021 @ 12416 deltaT 10534656 ID 0.2
```

Using this data and the deltaT values we can reconstruct the full history of pin 2 as recorded in the sample buffer (how or why specific values were recorded is controlled by the programming; see [Programming](https://github.com/outofmbufs/arduino-datamon/wiki/Programming-Interface) for details of that).

* 18866048 milliseconds ago, or approximately 5.2 hours ago, a programming event triggered and recorded pin 2 with a value of 232.
* 3773184 milliseconds prior to THAT, pin 2 had value 850.
* 23373568 msec prior to THAT, pin 2 was 1023.
* 36000000 msec prior to THAT, pin 2 was also 1023. Indeed at this point in the log we see a number of entries that were made with the timeMax deltaT; this represents a long stretch of time that the pin did not meet a sampling criteria but entries were forced into the sample buffer anyway because the 19 bit timestamps in the sample buffer "wrap" approximately every 18 hours and if entries were not forced then we wouldn't be able to tell the different between, for example, a 1 hour delta and a 19 hour delta (both would alias to the same timestamp in the sample buffer). In this particular example the timeMax setting was 36000000 which is 10 hours (more often than necessary; the exact wrap time is 0x7FFFF units of 128 msecs which is 67108736 milliseconds or just slightly more than 18.6 hours).

If you plan to write any programs to interpret this data, be sure to look at the [JSON interface](https://github.com/outofmbufs/arduino-datamon/wiki/JSON). JSON is a much better way to get at this data programmatically instead of "screen scraping" the HTML in this browser report.

## Reading Current Values
You can ask the arduino to simply tell you the current value of an input. This turns the device into an A/D converter with a network interface, which might be useful in and of itself.

Surfing `http://monitor/v2` would generate output such as:

    pin 2 value 408 @ 380969528

The timestamp shown (the `@ 380969528` part) is the "current time" reported as milliseconds-since-device-boot.

There is, unfortunately, no JSON interface for this so you will be stuck screen-scraping this HTML output if you want programmatic access to these values. The actual string value returned for the GET request is:
```
<HTML><HEAD><TITLE>Arduino Sensor: Current Value Reading</TITLE></HEAD><BODY>
pin 2 value 408 @ 380969528<br>
</BODY></HTML>
```
