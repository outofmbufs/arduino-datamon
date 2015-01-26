# arduino-datamon
This program implements an Arduino analog data monitor. It constantly reads a number of analog inputs and stores samples into a continuous ("ring") buffer when those samples meet criteria that you can define.

The ring buffer can be accessed via a browser or via a JSON/HTTP interface.

## Small memory footprint
Well, it's not quite true that this has a small memory footprint; the program itself is pretty large. But I spent a lot of time and effort to maximize the available ring-buffer size so that you can still store a lot of data even on the smallest Arduino that has only 2K of RAM.  You will be able to store 248 samples on an arduino with only 2K of RAM.  Of course, you could extend this storage by periodically reading data off the Arduino and storing it externally. Or buy an Arduino with more RAM storage.

## Documentation Wiki
See the [wiki](https://github.com/outofmbufs/arduino-datamon/wiki) for documentation.

