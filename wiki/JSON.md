# JSON Interface
You can obtain the JSON top-level object from `http://monitor/j`. Performing a GET on that URL will return raw JSON data as a string (no HTML adornment). The JSON will look something like this: 
```
{   "LogEntries": [
        { "data": "j/0", "name": "back", "pin": 0 }, 
        { "data": "j/2", "name": "basement", "pin": 2} 
    ], 
    "Programs": [
        { "DigitalHighAccumulator": 381829, "DigitalLowAccumulator": 0, 
            "Mode": "DIGITAL", "Pin": 0, 
            "PreviousTime": 345612516, "PreviousVal": 1023, 
            "QString": "n=1.d.0.400.100.150.back.43200000", "SlotNumber": 1
        }, 
        { "DigitalHighAccumulator": 311851, "DigitalLowAccumulator": 69977, 
            "Mode": "DIGITAL", "Pin": 2, 
            "PreviousTime": 381811504, "PreviousVal": 925, 
            "QString": "n=2.d.2.925.850.500.basement.36000000", "SlotNumber": 2
        }
    ], 
    "Status": {
        "BufSize": 248, "BufWraps": 0, 
        "MemAvail": 355, "MemAvailChanges": 0, 
        "Now": 381841576, "PackMaskedNow": 46297216, 
        "PackWrapConstant": 524288, 
        "Revision": "20141024.1", 
        "UptimeWraps": 0
    }
}
```
* `LogEntries` is an array telling you how to find the JSON objects for individual pin logs.
* `Programs` is an array of programming information. It shows you the query strings (QString) that represent each program parameters and it also provides you with some additional state information (described below). Unfortunately it does not break out the programming query strings for you; you will still have to understand how to parse those if you want the individual threshold parameters for example.
* `Status` reports informational values.

## Fetching LogEntries
Each entry in the `LogEntries` array is identified by name (if you programmed any names) and by pin number. The `data` item tells you the URL to use to GET the JSON for an individual pin log. For example, `http://monitor/j/2` would return a JSON object something like this:

```
{ "Log": [
  { "DeltaT": 18866048, "IDmajor": 0, "IDminor": 42, 
    "Time": 25314176, "Type": "SAMPLE", "Value": 232 }, 
  { "DeltaT": 3773184, "IDmajor": 0, "IDminor": 40, 
    "Time": 21540992, "Type": "SAMPLE", "Value": 850 }, 
  { "DeltaT": 23373568, "IDmajor": 0, "IDminor": 37, 
    "Time": 65276288, "Type": "SAMPLE", "Value": 1023 }, 
  { "DeltaT": 36000000, "IDmajor": 0, "IDminor": 33, 
    "Time": 29276288, "Type": "SAMPLE", "Value": 1023 }, 
  { "DeltaT": 36000000, "IDmajor": 0, "IDminor": 31, 
    "Time": 60385152, "Type": "SAMPLE", "Value": 1023 }, 
  { "DeltaT": 36000000, "IDmajor": 0, "IDminor": 27, 
    "Time": 24385152, "Type": "SAMPLE", "Value": 1019 }, 
     .
     .
     .
]}
```
These entries correspond to the data already discussed in the Browser interface and will not be further explained here. Obviously if you are writing a program to process data from the device you are much better off using this JSON interface to get the JSON object form of the data rather than trying to parse the HTML output of the human-friendly browser report.

The `Type` is always SAMPLE. For future expansion you should probably check for this but the current software never reports anything other than a SAMPLE entry.

It may be tempting to "just know" that the JSON object for pin N is at URL `/j/N` (e.g., `/j/2` for pin 2) but it is much better form to fetch the top-level JSON object from `/j` and use the entries within it to find the URLs for the pin-specific objects.

## Reminder about DeltaT
The software computes the DeltaT value for you so that you don't have to worry about "getting it right" when the timestamp values wrap-around. DeltaT is always correct. If you instead decide to perform the subtraction of two absolute time stamps yourself, be careful to make sure you handle the wrap-around case correctly. The easiest way to do that is to use DeltaT :)

If your power source is stable the device can stay up for months on end and since the timestamps wrap in just under 50 days wrap-around is something you will definitely see on occasion.

## Programming Information
In addition to the query strings, already explained elsewhere, the programming information contains:
* `DigitalHighAccumulator` is only meaningful for DIGITAL mode programs. The amount of time (in seconds) that the input has been monitored as being in the High state. Measured in seconds (it is accumulated in milliseconds internally but only reported out in seconds). The way this is stored internally it will not wrap for 130 years.
* `DigitalLowAccumulator` is the same as DigitalHighAccumulator but for the Low state.
* `Mode` is ANALOG or DIGITAL ('v' or 'd' in the query string)
* `PreviousVal` is the most recently sampled (recorded) value. In DIGITAL mode this is used to determine whether the state is High or Low and which hysteresis threshold to use. In ANALOG mode this is used to determine whether the current reading meets the threshold of change to trigger a new sample storage.
* `PreviousTime` is the timestamp associated with `PreviousVal`. It is also used to implement the timeMax policy.

## Status Information
* `BufSize` tells you how many data points can be stored in the ring buffer. You set the maximum at compile time but the program might reduce it at run time depending on how much RAM your Arduino has.
* `BufWraps` tells you how many times the ring buffer has wrapped around.
* `MemAvail` tells you how many bytes are left for malloc and the stack after the program has started operating. This is usually alarmingly small (like the 355 shown here) though "small" is relative (there is only 2K of memory in the entire device on a standard/small Arduino). It should never change.
* `MemAvailChanges` the program checks MemAvail every time through the main loop and records how many times it changes. It never should; this should always be zero.
* `PackWrapConstant` is simply 0x80000; i.e., it tells you how big the 19 bit field is. There's no really good reason to report this; it is leftover from early development/debugging.
* `Revision` is the revision string (duh)
* `Now` is the "current time" which is really just the uptime of the device, measured in milliseconds.
* `UptimeWraps` is incremented by one every time `Now` wraps around. This happens in just under 50 days.
   