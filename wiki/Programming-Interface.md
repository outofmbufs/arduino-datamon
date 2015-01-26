# Event Programming
Readings go into the ring buffer based on an event program which operates either in ANALOG mode or DIGITAL mode.

In ANALOG ("SAMPLE_LEVEL") mode, samples are only recorded ("trigger") if they meet these criteria:
* at least `timeThreshold` msecs has elapsed since last trigger
* the value has changed by at least `deltaThreshold`

In DIGITAL ("SAMPLE_DIGITAL") mode, samples are only recorded if:
* at least `timeThreshold` msecs has elapsed since last trigger
* if current state is Low, the value is greater than or equal to `digitalHigh`
* if current state is High, the value is less than or equal to `digitalLow`

This allows you to implement digital hysteresis if desired by setting different thresholds for low->high vs high->low transition. Of course you can set both thresholds to be identical if that makes sense for your application.

In either mode a sample will be recorded at least every "timeMax" milliseconds. By default, timeMax is set to a number that is just below the time wraparound maximum (approximately 18 hours). This covers the case where an input doesn't trigger for longer than the wraparound time. If we didn't force a new sample entry before then you would lose the information of how long it really was -- it will alias to something modulo the maximum time. You can reprogram the timeMax via the network interface. Setting a short timeMax is one way to simply use the programming to sample an input every N milliseconds. For example if you want to get 10 samples per second on an input set timeMax to 100 (milliseconds). Note, however, that this will fill the sample buffer and cause it to wrap around quite rapidly (every 24 seconds if you did 10 samples per second).

### Example use case
I have a photosensor in my basement simply to detect whether the lights are on or off down there; I have connected this sensor to an analog input on my Arduino. I have experimentally determined what the appropriate threshold is for lights on versus lights off. I set up a DIGITAL mode program to record an event in the sample buffer whenever the light state changes. Given that the sample buffer has 248 entries and the lights don't (usually haha) go on/off very often, this gives me quite a bit of "look back" as to when my basement lights have been on or off. And, of course, I can poll the device periodically to extract this data and build an even longer external log if I so desire.

## Program Slots
There are five "slots", numbered 0..4, for programming the device; each slot represents one set of criteria for recording a sample. Each individual slot operates on one input pin which you set as part of the parameters for that program slot.

The number of slots is determined at compile time; you can change it for your application. Each additional slot reduces (slightly) the amount of memory available for the sample buffer.

You program a particular slot by doing a GET (sorry) on /p with a query string. There are three forms of the query string:
* `n={slotnum}.d.{ ... params ... }` - set a DIGITAL program
* `n={slotnum}.v.{ ... params ... }` - set an ANALOG program
* `n={slotnum}.x` - delete program from {slotnum}



## DIGITAL PROGRAM QUERY STRING FORMAT
To set up a DIGITAL monitoring program, the query string format is:

    n={slot}.d.{pin}.{hi}.{lo}.{timeDelta}.{name}.{timeMax}

where:
* {slot} is a single-digit slot number.
* {pin} is a single-digit input pin number 0 .. 7.
* {hi} is the digitalHigh threshold
* {lo} is the digitalLow threshold
* {timeDelta} is the minimum time that must elapse between samples
* {name} OPTIONAL - an up-to-eight character name string
* {timeMax} OPTIONAL - how often a sample is forced to be recorded regardless

All other characters (the dots, the 'n=' and the 'd') are literal.

If you don't wish to override the built-in timeMax you may omit it. If you aren't using the names feature you may omit the name (you MUST specify a name, however, if you want to set the timeMax; the parsing is purely sequential/positional)

So, for example, entering this into your browser:

    http://monitor/p?n=0.d.2.925.850.500.basement.36000000

would program slot 0 to operate in DIGITAL mode, with the digitalHigh threshold being 925, the digitalLow threshold 850, and timeDelta 500 milliseconds. The input is on pin 2 which has been given the name "basement". A sample is recorded every 36000 seconds (10 hours) no matter what (even if there has been no high/low transition during that time). DIGITAL programs start out in the Low state, so if we set that program no data will be recorded in the sample buffer until pin reaches a value of 925 (or higher), at which point the pin reading (probably 925, but it might slew higher before it gets sampled) will be stored in the sample buffer along with the timestamp. At that point the pin is in High state, and again no data will be recorded in the sample buffer until the pin goes down to 850 (or below). Each time a sample is recorded no further samples will be recorded for 500 milliseconds. Finally, as mentioned, every 10 hours (36000000 milliseconds) a sample will be recorded no matter what.

In digital mode we also accumulate the total time in High and Low state. This is bordering on something that YOU should do rather than the sensor. However, it's useful sometimes because this will accumulate the correct time for the states regardless of whether you have been polling the device frequently enough to avoid ring buffer recycling. Time is accumulated in seconds.msecs granularity but only reported in seconds (it won't wrap around 32 bits for 130 years). This data is only reported in the JSON object, not the human-readable web page.

## ANALOG PROGRAM QUERY STRING FORMAT
To set up an ANALOG monitoring program, the query string format is:

    n={slot}.v.{pin}.{valDelta}.{timeDelta}.{name}.{timeMax}

This is similar to the DIGITAL format except there is only one threshold, the valDelta. Any time the current input value differs by at least valDelta from the previously recorded value, a sample will be recorded. After a sample is recorded at least timeDelta milliseconds must elapse before another sample will be recorded.

So, for example, entering this into your browser:

    http://monitor/p?n=1.v.5.30.500.sensor.36000000

would program slot 1 to operate in ANALOG mode, recording samples from input pin 5 any time the value changed by at least 30, but never recording more than one value every 500 milliseconds. The name associated with this pin is "sensor" and a sample is recorded every 36000 seconds (10 hours) no matter what.

## DELETING PROGRAMS
To delete a program from a slot:

    /p?n={slotnum}.x

So, for example, entering this into your browser window:

    http://monitor/p?n=1.x

deletes the program in slot 1.

## DISPLAYING PROGRAMS
Just surfing /p with no query string displays the current programs. For example:

    http://monitor/p

might display something like this:

    0 front: pin=3 DGTL; hi=400 lo=100 timeThresh=150 timeMax=43200000 /p?n=0.d.3.400.100.150.front.43200000
    1 back: pin=6 DGTL; hi=400 lo=100 timeThresh=150 timeMax=43200000 /p?n=1.d.6.400.100.150.back.43200000
    2: NULL
    3: NULL
    4: NULL

That part at the end is the URL suitable for cut-and-pasting into a browser to recreate the displayed program. Of course you would be better off writing the programs to eeprom (see "write-eeprom" command elsewhere in this document) but you may also wish to save that query string as a hard backup too.

## Programming Notes
The times that are recorded in the sample buffer are in ticks of 128 milliseconds but the times used in these programs are in milliseconds. A program operates with millisecond granularity but because the recorded timestamps are in 128msec units you may end up with multiple sample points that appear to be "at the same time" depending on how you program your sampling. So, for example, if you set the time delay between samples to less than 128 milliseconds and you have a bouncing input, you may record a lot of samples that all will appear to have happened within the same 128 msec time. They will, of course, still be in proper time-order in the sample buffer, you just won't be able to tell exactly when they happened with less than 128msec resolution. Obviously this is a compromise I made to be able to pack more data into the sample buffer; it works well for my application of recording relatively slow-moving events.

State information is stored per-program slot, so each program operates independently. So it works just fine if you want two separate programs to monitor the same pin for some reason. Perhaps one in DIGITAL mode for a definitive reading and one in ANALOG mode for a debugging/testing/analysis reading. This will work, and the two slot programs will each maintain their own separate ideas of state. HOWEVER note that the sample buffer entries are only identified by pin number (not slot number), so you (probably) won't be able to tell which particular program triggered a specific data log entry for a pin. You will just get a stream of pin readings merged from both programs.

