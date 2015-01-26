#include <SPI.h>
#include <Ethernet.h>
#include <EEPROM.h>
#include <avr/pgmspace.h>

// The MAC address this Arduino will use
// Usually the Arduino Ethernet shields come with an address
// (mine were labeled on the down side of the board). 
// Use that (edit it in here) if so.
//
// Otherwise, make one up like this. If you are making it up then
// the first byte MUST be even and it SHOULD NOT be a
// multiple of 4 (if you got an official one
// somewhere then that will be a multiple of 4). 
// 
// (google MAC address format definition for explanation)

byte mac[] = { 0x52, 0x52, 0x52, 0x12, 0x34, 0x56 };

// web server on port 80
EthernetServer server(80);

// the millis() function wraps approximately every 50 days of uptime
// for reporting purposes (only) we extend that by counting wraps here
int uptime_wraps = 0;

// the main loop fills this in every iteration
// it's only used for reporting (to check for memory leaks)
int mainMemavail = 0;
int memAvailChanges = 0;

// version string
prog_char revision[] PROGMEM = "20141024.1";

// ---------------------------
// Storage of sampled data
// ---------------------------
//
// Each sample reading is stored as a triple packed into 32 bits: 
//    [ time : value : pin ]
//
// where:
//     time:  19 bits
//     value: 10 bits
//     pin:    3 bits
//
// The time reading is 19 bits in units of 128 msecs since boot.
// It wraps approximately every 18 hours
//
// We pack the data this way because of limited RAM (2K) on the device
//
// *** SEE the "SPECIAL CASE FLAG" note after all these defines ***
//

#define PACKEDTIME_SHIFT 7         // this is the 128 msecs
#define PACKEDTIME_MASK 0x7FFFFL   // this is the 19 bits

// convert back and forth between 19 bit 128msec tick counts and msecs
#define MSECS_TO_PACKEDTIME(x)  (((x) >> PACKEDTIME_SHIFT) & PACKEDTIME_MASK)
#define PACKEDTIME_TO_MSECS(x)  ((x) << PACKEDTIME_SHIFT)

// pin field: low 3 bits
#define SRPIN_MASK 0x7
#define SRPIN(u)  ((u) & SRPIN_MASK)

// this is useful in a few places and is clearer than using the mask
// conceptually the max pin# might differ from the mask anyway 
#define PINMAX       SRPIN_MASK     // the maximum pin number allowed

// value field: next 10 bits
#define SRVAL_MASK 0x3FF           
#define SRVSHIFT 3
#define SRVAL(u)  (((u) >> SRVSHIFT) & SRVAL_MASK)

// packed time field: top 19 bits
#define SRPTSHIFT 13
#define SRPACKEDTIME(u) (((u) >> SRPTSHIFT) & PACKEDTIME_MASK)

//
// ****** NOTE THIS SPECIAL CASE ******
// It's helpful to be able to know when a sample buffer entry has nothing
// in it. To flag that, the specific combination of 
//
//            pin=7, value=1022, time=0
//
// is NEVER stored into the samplebuffer as an actual reading.
//
// If you are using pin 7 and get this reading, the 1022 reading will be
// changed to 1023. If that's important to you, don't use pin 7.
// ************************************
//
// [ I've considered some features that might require expanding this hack
//   in the future to flag other events... by restricting the 1022 reading
//   on more time=x values you could flag more types of things encoded into
//   the time field, all while only giving up the resolution between
//   readings of 1022 and 1023 on pin7 ]
//
//
#define SR_RESERVED_VALUE ( /* (0 << SRPTSHIFT) | */ (1022L << SRVSHIFT) | 7)
#define SR_REPLACEMENT    ( /* (0 << SRPTSHIFT) | */ (1023L << SRVSHIFT) | 7)


// A ring buffer of sample readings
struct samplebuffer {
  unsigned long *sampleReadings;     // these are the triples
  int bsize;                         // size (# of triples) of sampleReadings
  int nextsample;                    // index of next one to fill in
  int wrapcount;                     // for reporting only; counts wraparounds
};


//
// Add a sample to the buffer
//
void add_sample(int v, unsigned long t, int pin, struct samplebuffer *sb) {

  // clamp the value to the min/max that we can store 
  // (this should be a no-op in practice)
  if (v > SRVAL_MASK) v = SRVAL_MASK;
  if (v < 0) v = 0;

  unsigned long packedT = MSECS_TO_PACKEDTIME(t);

  // pack according to format (see prior comments on sample storage format)
  unsigned long sval =
          (packedT << SRPTSHIFT) | (v << SRVSHIFT) | (pin & SRPIN_MASK);

  // make sure we never store the reserved value; tweak reading if so
  if (sval == SR_RESERVED_VALUE)
    sval = SR_REPLACEMENT;

  sb->sampleReadings[sb->nextsample] = sval;

  // bump "pointer" to next sample and wrap if needed
  sb->nextsample++;                    
  if (sb->nextsample == sb->bsize) {
    sb->nextsample = 0;
    sb->wrapcount++;           // we count sample wraps just for reporting
  }
}


//
// Initialize a sample buffer
//
void init_samplebuffer(struct samplebuffer *sb) {
  sb->nextsample = 0;
  sb->wrapcount = 0;

  // fill with the special reserved "not yet recorded" value
  for (int i = 0; i < sb->bsize; i++)
    sb->sampleReadings[i] = SR_RESERVED_VALUE;
}

//
// Determine amount of RAM remaining
//
// obviously this is an implementation-specific hack
//
// It is used for two things:
//
//   1) info/debugging
//   2) sizing the samplebuffer at startup
//

int freeRam () {
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}



//
// Allocate the ring buffer portion of a sample buffer
// (just an array of 32 bit unsigned longs)
//
// There is much implementation-specificness implied here.
// The issue is we want the buffer to be large and the standard Arduino
// doesn't have much ram (2K). We'll try for what is asked, OR
// whatever number leaves a "safe" amount of memory behind.
// The safe limit determined empirically (it works, but obviously
// this is an empirical hack and is a bit fragile. 
//

#define ALLOCREMAINLIMIT 350

void alloc_samplebuffer(struct samplebuffer *sb, int bsizemax) {
  int m = freeRam();

  int nsamples = (m - ALLOCREMAINLIMIT) / sizeof (unsigned long);

  if (nsamples < 10) nsamples = 10; // hope for the best, yikes
  if (nsamples > bsizemax) nsamples = bsizemax;

  sb->sampleReadings =
             (unsigned long *)malloc(nsamples * sizeof (unsigned long));
  sb->bsize = nsamples;

  init_samplebuffer(sb);
}


// the actual sample buffer control structure
struct samplebuffer sampleRing;

// -----------------
// "Programming"
// -----------------
//
// Instead of hardcoding in the 'to-do' list of which pins to sample,
// how often, etc, those parameters can be programmed via the web interface.
// This is done by having multiple "programs", each providing specific
// parameters for sampling a particular pin.
//
// Samples are only recorded ("trigger") if they meet these criteria:
// SAMPLE_LEVEL mode:
//   at least "timeThreshold" msecs has elapsed since last trigger
//   the value has changed by at least "deltaThreshold" 
//
// SAMPLE_DIGITAL mode:
//   at least "timeThreshold" msecs has elapsed since last trigger
//   if current state is Low, the value is at least digitalHigh
//   if current state is High, the value is less or equal to digitalLow
//
// Choose appropriate values in digital mode for hysteresis (if desired)
// or identical High/Low thresholds for no hysteresis.
//
// In either mode a sample will be recorded at least every "timeMax"
// milliseconds. By default, timeMax is set to a number that is just
// below the time wraparound maximum in the log (otherwise once a sample
// doesn't change for longer than the wraparound time you will lose the
// information of how long it really is -- it will alias to something
// modulo the maximum time file). If you want a reading more often than
// that mandatory maximum, use a smaller "timeMax".
//
// In digital mode we also accumulate the total time in High and Low state.
// This is bordering on something that YOU should do rather than the sensor.
// However, it's useful sometimes because this will accumulate the correct
// time even if you don't poll the device often enough. Time is accumulated
// in seconds.msecs granularity but only reported in seconds (it won't
// wrap around 32 bits for 130 years). You have to surf/parse the
// programming table JSON object to get this reading.
//
// State information is stored per-program, so each program operates
// independently. So it works just fine if you want two seperate programs
// to monitor the same pin for some reason. HOWEVER note that the log is
// only recorded per-pin, so you (probably) won't be able to tell which
// particular program triggered a specific data log entry for a pin.
//
// finally, note that the time recorded in the log is in ticks of 128msec
// but the times used in these instructions are in msecs. The program
// will operate at the msec granularity but the timestamp data you
// see will only have 128msec granularity (i.e., if you set it to trigger
// every 50msec no matter what, it will in fact trigger but you will end
// up with multiple sample points with an identical 128msec granularity 
// time stamp. They will still be in time order of course.
//
//


#define SAMPLE_NULL 0      // for unused program entries
#define SAMPLE_LEVEL 1     // record an analog reading
#define SAMPLE_DIGITAL 2   // record a digital/thresholded reading

#define PGMNAMELEN    8    // you can "name" a program entry, up to 8 chars

struct timeAccumulator {
  unsigned long secs;
  int msecs;
};

struct samplingProgram {
  int mode;           // NULL, LEVEL, or DIGITAL
  int pin;            // which pin to read
  int deltaThreshold; // for LEVEL mode
  int digitalHigh;    // above this is ON in DIGITAL mode
  int digitalLow;     // below this is OFF in DIGITAL mode
  int previousVal;    

                      // accumulate the total time in high/low state
  struct timeAccumulator accumHigh;
  struct timeAccumulator accumLow;

  boolean firstTime;            // forces one first reading (initial oneshot)
  unsigned long previousTime;
  unsigned long timeThreshold;  // see note about stored time resolution
  unsigned long timeMax;        // force a reading at least this often
  char pgmname[PGMNAMELEN];     // not necessarily NUL terminated!
};



// you could hardcode initial values here, but much better to
// program via web and write to eeprom

#define NPROGRAMS 5
samplingProgram todoList[NPROGRAMS] = {
 { SAMPLE_NULL }
};

//
// The default timeMax value is calculated based on the fact that the
// stored packed time wraps every approx 18 hours. If we didn't do something
// special, something off for 1 hour and something off for 19 hours would
// look the same when a transition to on finally happens. In other words,
// the time recorded for the "on" transition would seem to be only 1 hour 
// later, even if it really had been 19 hours. This is effectively
// a sample aliasing problem and so we enforce a max sample interval.
//
// Thus, a new log entry is forced every timeMax milliseconds.
// Obviously you can also use that field to get a sample more often for
// whatever other reason might make sense in your application.
//
// NOTE: This means there can be adjacent entries of the same state and
//       you have to recognize this when processing the log data.
//
// This is the default value for timeMax, calculated to provide
// a 10 minute safety factor (out of the 18 hour wrap time); way 
// overkill of course but we just want to be sure to get it a reading even
// if there is some unknown delay (e.g., a timeout hang in the tcp stack
// at exactly the wrong moment though I don't know whether that's even
// a possibility in the TCP stack code)
//
#define MAXTIMEMAX (PACKEDTIME_TO_MSECS(PACKEDTIME_MASK)-(600*1000L))




void init_paramState() {
  struct samplingProgram *params;
  unsigned long now = millis();

  // set the "firstTime" to ensure an initial reading will be made
  // for each valid program entry (otherwise your first recorded
  // transition occurs after an unknown duration)
  for (params = todoList; params < &todoList[NPROGRAMS]; params++) {
    if (params->mode != SAMPLE_NULL) {
      params->firstTime = true;   // make it read next time
      params->previousTime = now; // should be irrelevant, but just in case
      if (params->mode == SAMPLE_DIGITAL) {
        params->accumHigh.secs = 0;
        params->accumHigh.msecs = 0;
        params->accumLow.secs = 0;
        params->accumLow.msecs = 0;
      }
    }
  }
}



//
// more "not very much memory" hackery.
//
// These two functions, tchar_alloc and tchar_free, are a front-end to
// malloc. They manage a reusable character buffer to avoid constantly
// calling malloc/free. I'm not sure if malloc/free is really a problem
// to be honest, but given the embedded environment and the desire for 
// this thing to run forever, I'd rather manage the memory allocation
// a bit more explicitly. Hence these functions.
//
// tchar_alloc is just like malloc. Give it a size in bytes and it 
// gives you a buffer. If you ask for a "small enough" size, you get
// the statically managed buffer -- IF it is available. If you ask for
// something too big, or you ask for something when the static buffer is
// already allocated, then tchar_alloc just calls malloc().
//
// tchar_free frees the allocated buffer, handling the case where it is
// the static buffer or the case where it was actually malloced. As a 
// notational convenience it returns NULL so you can p = tchar_free(p)
// if you want p to be set to NULL as a side effect.
//

#define TMPCHARBUFSIZ 70
char tmpchar[TMPCHARBUFSIZ];
char tmpchar_avail = 1;
char *tc_q = NULL;              // for saving queued free() calls

char *tchar_alloc(int nbytes) {
  char *p = NULL;

  // if we have a queued buffer to free, free it
  if (tc_q) {
    free(tc_q);
    tc_q = NULL;
  }


  // if we can give you the tmpchar buf, do so
  // otherwise, fall through to malloc

  if ((nbytes <= TMPCHARBUFSIZ) && tmpchar_avail) {
    p = tmpchar;
    tmpchar_avail = 0;
  } else {
    p = (char *)malloc(nbytes);
  }
  return p;
} 

// as a notational convenience this returns NULL
char *tchar_free(char *p) {
  if (p == tmpchar) 
    tmpchar_avail = 1;
  else {
    if (tc_q)
      free(tc_q);

    tc_q = p;
  }
  return NULL;
}


// --------------------
// EEPROM storage
// --------------------
//
// we save two things to EEPROM storage:
//   1) The mac address of this Arduino. This allows you to deal
//      with having more than one of these things on your network
//      albeit it's still very clumsy to get them bootstrapped (you
//      will have to edit the code the first time to initialize the
//      mac address data but after that the stored mac addr will override
//      the staticly init'd data)
//   2) The "programming" instructions
//
// The EEPROM format is:
//    Magic String (N bytes) -- let's us know this stuff is there
//    6 bytes of mac addr
//    then the programming data (see below for its format)
//
// The "programming" instructions are saved/restored from EEPROM
// so that they will survive reboot. This is done by simply writing
// them into EEPROM in the same ASCII format they'd be sent to us in
// a from-the-web programming request, then reading those strings from
// EEPROM at startup time and processing them.
//
//


static char EEPROM_MAGIC_STRING[] = "xyzzy!";
#define EEPROMMACBYTES 6

//
// called once at startup
// If we find a previously stored mac addr, use it
// If we don't, then use the programmed in mac addr AND store it in EEPROM
// 
// Clumsy, but: if you have more than one of these on your network, then
// alter the staticly stored MAC addr initialization and the first time
// you start up this code the altered MAC will get stored; subsequent boots
// will use that MAC addr instead of the compiled in one (it's up to you
// to manage this process for your multiple boards; like I said .. clumsy)
//
// instead of having a way to clear EEPROM magic data, just frob the 
// magic string if you need to force a miscompare to start over
//

void EEPROMmac(byte *m) {

  //
  // look for the magic string at the start of the EEPROM.
  // If found, we previously saved program info there
  // note that the magic string is just N characters, no NUL in EEPROM
  //
  char *s = EEPROM_MAGIC_STRING;
  int EEaddr;
  int i;

  boolean foundit = true; 
  for (EEaddr = 0; *s; EEaddr++, s++) {
    if (EEPROM.read(EEaddr) != *s)   
      foundit = false;                      // miscompare, no stored program
  }

  if (foundit) {
    // ok just read the mac addr and believe that it's correct
    for (i = 0; i < EEPROMMACBYTES; i++)
      *m++ = EEPROM.read(EEaddr++);
  } else {
    // didn't find it, use the supplied mac and write THAT to EEPROM
    EEaddr = strlen(EEPROM_MAGIC_STRING);

    for (i = 0; i < EEPROMMACBYTES; i++)
      EEPROM.write(EEaddr++, *m++);

    // Don't forget to zero out the stored programs count too
    // (doh!) because we're taking a non-matching EEPROM magic
    // about to set it to a valid magic. Really should have a generic
    // EEPROM init routine (basically, this is it)

    EEPROM.write(EEaddr, 0);

    // and put the correct magic in
    for (EEaddr = 0; EEaddr < strlen(EEPROM_MAGIC_STRING); EEaddr++)
      EEPROM.write(EEaddr, EEPROM_MAGIC_STRING[EEaddr]);
  }
}




//
// Functions to manage the program table (instructions on what/when to sample)
//

//
// Obtain saved programming information (if any) from EEPROM
//
void loadProgramTable(char *s);

void readProgramsFromEEPROM() {
  //
  // look for the magic string at the start of the EEPROM.
  // If found, we previously saved program info there
  // note that the magic string is just N characters, no NUL in EEPROM
  //
  char *s = EEPROM_MAGIC_STRING;
  int EEaddr;

  for (EEaddr = 0; *s; EEaddr++, s++) {
    if (EEPROM.read(EEaddr) != *s)   
      return;                      // miscompare, no stored program
  }

  // made it all the way to the end of the magic so we're good
  // now the format is:
  //   mac address (skip over this)
  //   one byte: number of program strings
  //   then N nul-terminated program strings (same fmt as web interface)

  EEaddr += EEPROMMACBYTES;              // skip the mac addr
  int pgmCount = EEPROM.read(EEaddr++);

  if (pgmCount < 0 || pgmCount > NPROGRAMS) // shouldn't happen, check anyway
    return;

  char *pgmString = tchar_alloc(TMPCHARBUFSIZ);
#define MAXPGMSTR (TMPCHARBUFSIZ-1)

  for (int i = 0; i < pgmCount; i++) {
    for (char *p = pgmString; *p = EEPROM.read(EEaddr++); p++) {
      if (p == &pgmString[MAXPGMSTR]) { // something's very very wrong
        tchar_free(pgmString);
        return;
      }
    }

 
    // process this program string, just like if it came from web
    loadProgramTable(pgmString);
  }

  tchar_free(pgmString);
}

// destroy the magic string in the eeprom

void zapEEPROMmagic() {
  EEPROM.write(0, EEPROM_MAGIC_STRING[0]+1);  // i.e., a mismatch 
}

/*
 * Write programming information to the EEPROM.
 */

void writeProgramsToEEPROM() {
 
 // overwrite the magic string to invalidate the current data during update
  zapEEPROMmagic();

  int EEaddr = strlen(EEPROM_MAGIC_STRING);   // start past the magic spot
  EEaddr += EEPROMMACBYTES;                   // and skip the mac addr
  int pgmCount = 0;
  int pgmAddr = EEaddr++;                     // will come back and write here

  char *buf = tchar_alloc(TMPCHARBUFSIZ);

  for (int i = 0; i < NPROGRAMS; i++) {
    struct samplingProgram *p = &todoList[i];

    if (p->mode != SAMPLE_NULL) {
      pgmCount++;
      samplingProgramToString(p, buf, TMPCHARBUFSIZ);

      for (char *s = buf; *s; s++) 
        EEPROM.write(EEaddr++, *s);

      EEPROM.write(EEaddr++, 0); // the NUL terminator
    }
  }

  tchar_free(buf);

  // go back and write the number of programs we wrote
  EEPROM.write(pgmAddr, pgmCount);

  // now fill in the complete magic string
  // could check to see if previous values don't need overwriting
  // but since we're frobbing the first every time anyway, why bother
  
  for (EEaddr = 0; EEPROM_MAGIC_STRING[EEaddr]; EEaddr++)
    EEPROM.write(EEaddr, EEPROM_MAGIC_STRING[EEaddr]);

}







//
// this function converts the current samplingProgram
// into a string appropriate for the GET /p?stuff query
// Used as part of updating the stored program in EEPROM
//

void samplingProgramToString(struct samplingProgram *p, char *buf, int bsiz) {

  // XXX this doesn't actually check bufsiz; be sure it's big enough! DOH!
  char *s = buf;

  // it's also a hack that we reverse engineer the slot number
  int pnum = p - todoList;

  if (p->mode != SAMPLE_NULL) {
    *s++ = 'n';
    *s++ = '=';
    *s++ = '0' + pnum;
    *s++ = '.';
    *s++ = ((p->mode == SAMPLE_LEVEL) ? 'v' : 'd');
    *s++ = '.';
    *s++ = '0' + p->pin;
    *s++ = '.';
    if (p->mode == SAMPLE_LEVEL) {
      itoa(p->deltaThreshold, s, 10);
      while (*s) s++;
    } else {
      itoa(p->digitalHigh, s, 10);
      while (*s) s++;
      *s++ = '.';
      itoa(p->digitalLow, s, 10);
      while (*s) s++;
    }
    *s++ = '.';
    ltoa(p->timeThreshold, s, 10);
    while (*s) s++;

    if (p->pgmname[0] != '\0')
      *s++ = '.';
    for (int i = 0; i < PGMNAMELEN && p->pgmname[i] != '\0'; i++)
      *s++ = p->pgmname[i];
    *s = '\0';
    
    //
    // Only need to encode the timeMax if not default
    //
    if (p->timeMax != MAXTIMEMAX) {
      *s++ = '.';
      ltoa(p->timeMax, s, 10);
    }
  }
}





//
// this function is slightly overly general in an attempt to insulate
// against malformed protocol requests. Unnecessarily careful. Per
// recommended practice, it accepts CRLF or LF as line terminators.
//
// The only HTTP line we care about is the GET string, which this returns.
// In a well-formed HTTP request, the GET should be the very first line
// although this does not require/enforce that (any line starting with
// GET will be returned - the first such line encountered
//
//
// To protect against a client that connects and then sends nothing
// (or just never sends a GET string) we will bail out after a defined
// amount of time. In practice no browser ever causes this but you can
// test it by telnet-ing to port 80, entering at least one character
// and then just waiting (i.e., don't send a GET and wait)
//
// any string not fitting within bufsiz is just truncated
// Make your buffer big enough for the GET string length you care about
//

#define HTTPGET_TIMEOUT_SECONDS    150L
#define HTTPGET_TIMEOUT            (HTTPGET_TIMEOUT_SECONDS * 1000)

char *obtainHTTPGET(EthernetClient &client, char *s, int bufsiz) {  
  char *p = s;               // p advances through output buffer
  char *plim = &s[bufsiz-1]; // maximum VALID p (where the NUL must go) 

  boolean prevCR = false;
  unsigned long start_time = millis();

  while (client.connected()) {
    unsigned long elapsed;
 
    elapsed = delta_millis(start_time, millis());
    if (elapsed > HTTPGET_TIMEOUT)
      return NULL;
    
    if (client.available()) {
      char c = client.read();

      if (c == '\n' &&  p - s > 3) {
        if (s[0] == 'G' &&
            s[1] == 'E' &&
            s[2] == 'T' &&
            s[3] == ' ') {
          *p = '\0';         // terminate the string (we left room for this)

          // null out the first space after the URL (the "HTTP/1.1" part)
          for (p = &s[4]; *p && *p != ' '; p++)
            ;
          *p = '\0';
          return s;
        }
      } else if (prevCR && p < plim)
        *p++ = '\r';         // this is where deferred naked CRs get appended


      // if it's a newline, start all over (discard non-GET strings)
      // otherwise, append the character except:
      //    -- if there's no room (just discard character)
      //    -- if it's a CR ... defer until we determine if CRLF vs naked CR
      if (c == '\n')
        p = s;                // i.e., discard and start over
      else if (p < plim && c != '\r')
        *p++ = c;        

      prevCR = (c == '\r');
    }
  }
  // did not ever find a GET string
  return NULL;
}

//
// Because program space and data space are separate, string literals use
// RAM which we don't have very much of. This function along with the
// PROGMEM directive (see Arduino docs) allows string literals to reside
// in program space (flash) instead of using the limited RAM space for them.
//
void
serveStringFromPROGMEM(EthernetClient& client, prog_char *PROGMEMSTR) {
#define PCOPYSIZE 30          // we do it a little at a time
  char *pcopybuf = tchar_alloc(PCOPYSIZE);

  int n = strlen_P(PROGMEMSTR);
  int pcpos = 0;

  for (int i = 0; i < n; i++) {
    char c = pgm_read_byte(PROGMEMSTR+i);

    pcopybuf[pcpos++] = c;


    if (pcpos == PCOPYSIZE-1 || i == (n-1)) {
      pcopybuf[pcpos] = '\0';
      client.print(pcopybuf);
      pcpos = 0;
    }
  }

  tchar_free(pcopybuf);
}   

// prog_char/PROGMEM hacks get this string stored in Flash memory
prog_char sr1[] PROGMEM = 
  "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n<!DOCTYPE HTML>\r\n<HTML><HEAD><TITLE>Arduino Sensor: ";

prog_char sr2[] PROGMEM = "</TITLE></HEAD><BODY>\r\n";
prog_char sr3[] PROGMEM = "</BODY></HTML>\r\n";

void serveHTTPstandardreply(EthernetClient &client, prog_char *title) {
  serveStringFromPROGMEM(client, sr1);
  serveStringFromPROGMEM(client, title);
  serveStringFromPROGMEM(client, sr2);
}

void serveStandardClosing(EthernetClient& client) {
  serveStringFromPROGMEM(client, sr3);
}

void serveProgramString(EthernetClient& client, struct samplingProgram *pgm) {
  char *sbuf = tchar_alloc(TMPCHARBUFSIZ);
  samplingProgramToString(pgm, sbuf, TMPCHARBUFSIZ);
  client.print(sbuf);
  tchar_free(sbuf);
}



prog_char apjson[] PROGMEM = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n";

void serveJSONproto(EthernetClient& client) {
  serveStringFromPROGMEM(client, apjson);
}

prog_char sr4[] PROGMEM = "<br>\r\n";

void serveEndOfLine(EthernetClient& client) {
  serveStringFromPROGMEM(client, sr4);
}

prog_char spt_pin[] PROGMEM = " pin=";
prog_char spt_NULL[] PROGMEM = "NULL";
prog_char spt_LEVEL[] PROGMEM = " LEVEL; thresh=";
prog_char spt_DIGITAL[] PROGMEM = " DIGITAL; high=";
prog_char spt_DIG2[] PROGMEM = " low=";
prog_char spt_time[] PROGMEM = " timeThreshold=";
prog_char spt_tmax[] PROGMEM = " timeMax=";
prog_char spt_qs[] PROGMEM = " /p?";

prog_char spt_title[] PROGMEM = "Program";

//
// display the current program table
//
void serveProgramTable(EthernetClient &client) {
  serveHTTPstandardreply(client, spt_title);
 
  for (int i = 0; i < NPROGRAMS; i++) {
    struct samplingProgram *p = &todoList[i];

    client.print(i); 
    if (p->mode != SAMPLE_NULL && p->pgmname[0] != '\0') {
      char ctmp[PGMNAMELEN+2];
      ctmp[0] = ' ';
      strncpy(&ctmp[1], p->pgmname, PGMNAMELEN);
      ctmp[PGMNAMELEN+1] = '\0';
      client.print(ctmp);
    }
    client.print(": ");
    if (p->mode == SAMPLE_NULL)
      serveStringFromPROGMEM(client, spt_NULL);
    else {
      serveStringFromPROGMEM(client, spt_pin);
      client.print(p->pin);
      if (p->mode == SAMPLE_LEVEL) {
        serveStringFromPROGMEM(client, spt_LEVEL);
        client.print(p->deltaThreshold);
      } else if (p->mode == SAMPLE_DIGITAL) {
        serveStringFromPROGMEM(client, spt_DIGITAL);
        client.print(p->digitalHigh);
        serveStringFromPROGMEM(client, spt_DIG2);
        client.print(p->digitalLow);
      }
      serveStringFromPROGMEM(client, spt_time);
      client.print(p->timeThreshold);
      serveStringFromPROGMEM(client, spt_tmax);
      client.print(p->timeMax);
      serveStringFromPROGMEM(client, spt_qs);
      serveProgramString(client, p);
    }
    serveEndOfLine(client);
    
  }

  serveStandardClosing(client);
}

//
// when there's nothing to output, still need some sort of response
//
prog_char pOK[] PROGMEM = "OK";

void serveOK(EthernetClient &client) {
  serveHTTPstandardreply(client, pOK);
  serveStringFromPROGMEM(client, pOK);
  serveEndOfLine(client);
  serveStandardClosing(client);
}

char *nextdotfield(char *s) {
  while (*s && (*s++ != '.'))
    ;
  return s;
}




//
// The query string scheme is:
//     n=1.d.3.600.200.500
// set slot 1 to digital mode, pin 3, hi/lo thresh (600/200), time delta (500)
// Note: the complete GET request is GET /p?n=1.d.3.600.200.500
//
// OR
//     n=1.v.2.100.500   
// set slot 1 to level mode, pin 2, thresh (100), time delta (500)
// for either mode, two additional parameters can be appended
//    .name.timeMax
// in that order (you must have a name if you have a timeMax)
// OR
//
//    n=1.x
//
// delete (set to SAMPLE_NULL) slot 1
//
// We're not trying to be forgiving or robust here; either you put the query
// string in correctly or you didn't. Don't mess up.
//
void loadProgramTable(char *s) {
  unsigned long ltmp;

  if (*s++ != 'n')
    return;
  if (*s++ != '=')
    return;

  // accept exactly one digit for the slot number
  char c = *s++;
  if ((c < '0') || (c > '9'))
    return;

  int slotnum = c - '0';
  if (slotnum > NPROGRAMS-1)
    return;

  if (*s++ != '.')
    return;

  struct samplingProgram *p = &todoList[slotnum];
  p->pgmname[0] = '\0';     // always wipe the previous name

  // ok, we have a slot number.
  // check for valid type indicator - d for digital, v for level

  int doCommon = 0;
  switch (*s++) {
  case 'd':
  case 'D':
    if (*s++ == '.') {
      // at this point we're just going to take your input; good luck if you mess up
      p->mode = SAMPLE_DIGITAL;
      p->pin = atol(s); s = nextdotfield(s);
      p->digitalHigh = atol(s); s = nextdotfield(s);
      p->digitalLow = atol(s); s = nextdotfield(s);
      p->timeThreshold = atol(s); s = nextdotfield(s);
      p->accumHigh.secs = 0;
      p->accumHigh.msecs = 0;
      p->accumLow.secs = 0;
      p->accumLow.msecs = 0;
      doCommon = 1;
    }
    break;

  case 'v':
  case 'V':
    if (*s++ == '.') {
      p->mode = SAMPLE_LEVEL;
      p->pin = atol(s); s = nextdotfield(s);
      p->deltaThreshold = atol(s); s = nextdotfield(s);
      p->timeThreshold = atol(s); s = nextdotfield(s);
      doCommon = 1;
    }
    break;

  case 'x':
  case 'X':
  default:
    p->mode = SAMPLE_NULL;
    break;
  }

  // the tail end of digital/level programming is common
  if (doCommon) {
    if (*s) { // we have a name
      int i;
      for (i = 0; i < PGMNAMELEN && isalnum(*s); i++)
        p->pgmname[i] = *s++;

      if (i < PGMNAMELEN)
        p->pgmname[i] = '\0';
    }

    s = nextdotfield(s);
    p->timeMax = ((*s == '\0') ? MAXTIMEMAX : atol(s));
    p->firstTime = true;
  }

  // any sanity checks on parameters go here
  if (p->pin > PINMAX)
    p->mode = SAMPLE_NULL;
  if (p->timeMax == 0)  // explicit .0 still means default
    p->timeMax = MAXTIMEMAX;


  // note that if you set timeMax to something greater than the 
  // 19 bit wrap value, you will allow the aliasing problem to occur.
  // I think this should be forbidden here with this commented out
  // code but I've chosen to leave that possibility open if you insist
  // on setting the parameters that way for some reason
/*
  if (p->timeMax > MAXTIMEMAX)
    p->timeMax = MAXTIMEMAX;
*/
}




// prog_char/PROGMEM directives to get this huge string into Flash memory
// rather than run-time data (limited RAM) memory

prog_char helpString[] PROGMEM = 
"GET / -- just output a report<br>GET /0 -- only output pin 0 values (works for 0 .. 9 only)<br>GET /m -- available memory report<br>GET /p -- output the program table<br>GET /p?qstring -- load the program table from qstring<br>GET /r -- resets the sample ring buffer<br>GET /v0 -- immediately read value of input 0<br>&nbsp;&nbsp;/v1, v2, v3 etc immediately read that pin<br>GET /write-eeprom -- store program params into EEPROM<br>GET /help -- this info<br>GET /j -- JSON resource object<p>qstring for programs:<ul><li>n=1.d.3.600.200.500.name.30000 -- set slot 1 to digital mode, pin 3, hi/lo thresh (600/200), time delta 500<p>If name is specified, it must be 8 characters maximum. The last number is the timeMax; a reading is forced if this many seconds has elapsed since last trigger. Use zero or omit it for default.<li>n=1.v.2.100.500 -- set slot 1 to level mode, pin 2, delta thresh 100, time delta 500; also allows the optional name and timeMax parameters<li>n=1.x -- delete slot 1<ul></BODY></HTML>\r\n";

prog_char pHelp[] PROGMEM = "Help";

void serveHelpString(EthernetClient& client) {
  serveHTTPstandardreply(client, pHelp);
  serveStringFromPROGMEM(client, helpString);
}  
 
prog_char sv1[] PROGMEM = "pin ";
prog_char sv2[] PROGMEM = " value ";
prog_char sv3[] PROGMEM = " @ ";
prog_char svtitle[] PROGMEM = "Current Value Reading";

void serveCurrentReading(EthernetClient& client, int pin)
{
  serveHTTPstandardreply(client, svtitle);
  int curVal = analogRead(pin);
  unsigned long now = millis();

  serveStringFromPROGMEM(client, sv1); 
  client.print(pin);
  serveStringFromPROGMEM(client, sv2); 
  client.print(curVal);
  serveStringFromPROGMEM(client, sv3);
  client.print(now);
  serveEndOfLine(client);
  serveStandardClosing(client);
}



prog_char smem1[] PROGMEM = "Memavail: ";
prog_char smem2[] PROGMEM = "allocated buffer size: ";
prog_char smem3[] PROGMEM = "bufwraps: ";
prog_char smemtitle[] PROGMEM = "Memory Information";

void serveMemavail(EthernetClient& client, struct samplebuffer *sb)
{
    serveHTTPstandardreply(client, smemtitle);

    serveStringFromPROGMEM(client, revision);
    serveEndOfLine(client);

    serveStringFromPROGMEM(client, smem1);
    client.print(mainMemavail);
    serveEndOfLine(client);

    serveStringFromPROGMEM(client, smem2);
    client.print(sb->bsize);
    serveEndOfLine(client);

    serveStringFromPROGMEM(client, smem3);
    client.print(sampleRing.wrapcount);
    serveEndOfLine(client);
    
    serveStandardClosing(client);
}

//
// compute the delta time for two "packed time" values
// pt2 is the later value
// handles wrap-around
//
unsigned long delta_packed_time(unsigned long pt1, unsigned long pt2) {
  //
  // just compute the normal difference if no wrap around
  // if there's wrap around, the fact that packed times are smaller
  // than unsigned longs lets us use a simple hack to deal with that
  // (add the wrap-around amount to the second time)
  //
  if (pt2 >= pt1) 
    return pt2 - pt1;
  else 
    return pt2 + (PACKEDTIME_MASK + 1) - pt1; 
}

//
// compute the delta time for two 32-bit msec counts
// m2 is the later time. Handles wrap-around.
//

unsigned long delta_millis(unsigned long m1, unsigned long m2) {
  if (m2 >= m1)
    return m2 - m1;
  else {              
    // probably some more clever way, but this works
    // basically we need to add 0x100000000 to the incorrectly "lower"
    // time m2 before doing the subtraction, but of course we need
    // to do this carefully to stay within 32 bits to make it work.
    //

    // compute m2-m1 but add in 0xFFFFFFFF
    unsigned long tmp = 0xFFFFFFFF; 

    tmp -= m1;     // the -m1 part
    tmp += m2;     // the +m2 part .. does not overflow bcs m2<m1

    tmp &= 0xFFFFFFFF;  // make sure it works on 64 bit machines (haha)
    return tmp + 1; // the rest of the 0x100000000 we needed to add
  }
}



prog_char srbt[] PROGMEM = "Report";
prog_char srbhelp[] PROGMEM = "See /help for complete command list.<br>";
prog_char srb1[] PROGMEM = "Now: ";
prog_char srb2[] PROGMEM = " pack-masked: ";
prog_char srb3[] PROGMEM = " uptime wrap-arounds: ";
prog_char srbDT[] PROGMEM = " deltaT ";


void serveRingBuffer(EthernetClient& client, struct samplebuffer *sb, int only) {
  serveHTTPstandardreply(client, srbt);
  
  unsigned long now = millis();

  // if serving the root/default page, also remind them of help string
  if (only < 0)
    serveStringFromPROGMEM(client, srbhelp);

  serveStringFromPROGMEM(client, srb1);
  client.print(now); 
  serveStringFromPROGMEM(client, srb2);
  client.print(PACKEDTIME_TO_MSECS(MSECS_TO_PACKEDTIME(now))); 
  
  serveStringFromPROGMEM(client, srb3);
  client.print(uptime_wraps);
  serveEndOfLine(client);

  unsigned long prev_time = MSECS_TO_PACKEDTIME(now);

  int i = sb->nextsample;
  int wrapID = sb->wrapcount;
  for (int cnt = sb->bsize; cnt > 0; cnt--) {
    --i;
    if (i < 0) {
      i = sb->bsize - 1;
      --wrapID;
    }
    
    unsigned long r = sb->sampleReadings[i];

    // When the sensor is first started or the ring buffer is reset
    // there will be a bunch of initialized entries. Don't
    // report these.

    if ((r != SR_RESERVED_VALUE) && (only < 0 || SRPIN(r) == only)) {
      unsigned long t1 = SRPACKEDTIME(r);

      client.print("pin "); client.print(SRPIN(r)); client.print(" = ");
      client.print(SRVAL(r)); client.print(" @ ");
      client.print(PACKEDTIME_TO_MSECS(t1));

      // PLEASE NOTE CAREFULLY:
      //    deltaT value is probably only useful on individual pin reports
      //    On "all pins" report it's a delta from the previous 
      //    reading (which might be an event on a different pin)
      //
      // RECOMMEND: Use the "only" report pin-by-pin if you need deltaT
      // We will, however, print it out regardless
      //
      serveStringFromPROGMEM(client, srbDT);
      client.print(PACKEDTIME_TO_MSECS(delta_packed_time(t1, prev_time)));

      // every entry gets an ID formulated from its ring buffer index
      // and the wrap-around count
      client.print(" ID ");
      client.print(wrapID);
      client.print(".");
      client.print(i);

      prev_time = t1;
      serveEndOfLine(client);
    }
  }  
  serveStandardClosing(client);
}


//
// JSON interfaces
//
// The root object returns a JSON description of all the resources
// that can be fetched. Arbitrarily, we only return log entry URIs
// for pins that have active programs. Of course, there could be older
// (will eventually be overwritten) log entries for programs that have
// since been deleted, AND there may not be any log entries for programs
// that are currently active. Nevertheless, the assumption is that in
// normal usage clients will be interested in log entries for active 
// programs and so those are the ones returned.
// 

prog_char jrLBrace[] PROGMEM = "{ ";
prog_char jrRBrace[] PROGMEM = " }";
prog_char jrLBrk[] PROGMEM = "[ ";
prog_char jrRBrk[] PROGMEM = " ]";
prog_char jrComma[] PROGMEM = ", ";

prog_char jrLogEntries[] PROGMEM = "{ \"LogEntries\" : [ ";
prog_char jrpin[] PROGMEM = "{\"pin\" : ";
prog_char jrpgmname[] PROGMEM = ", \"name\" : \"";
prog_char jrdata[] PROGMEM = "\", \"data\" : \"j/";
prog_char jrcloseData[] PROGMEM = "\" }";



// status object
prog_char jrsStatus[] PROGMEM = "\"Status\" : { \"Now\" : ";
prog_char jrsPM[] PROGMEM = ", \"PackMaskedNow\" : ";
prog_char jrsUpWrap[] PROGMEM = ", \"UptimeWraps\" : ";
prog_char jrsRevision[] PROGMEM = ", \"Revision\" : \"";
prog_char jrsMem[] PROGMEM = "\", \"MemAvail\" : ";
prog_char jrsMemChg[] PROGMEM = ", \"MemAvailChanges\" : ";
prog_char jrsBufsize[] PROGMEM = ", \"BufSize\" : ";
prog_char jrsBufwraps[] PROGMEM = ", \"BufWraps\" : ";
prog_char jrsPWC[] PROGMEM = ", \"PackWrapConstant\" : ";
// program object
prog_char jrpPrograms[] PROGMEM = "\"Programs\" : [ ";
prog_char jrpNum[] PROGMEM = "{ \"SlotNumber\" : ";
prog_char jrpQS[] PROGMEM = ", \"QString\" : \"";
prog_char jrpPin[] PROGMEM = "\", \"Pin\" : ";
prog_char jrpDigMode[] PROGMEM = ", \"Mode\" : \"DIGITAL\"";
prog_char jrpLvlMode[] PROGMEM = ", \"Mode\" : \"LEVEL\"";
prog_char jrpPrevVal[] PROGMEM = ", \"PreviousVal\" : ";
prog_char jrpPrevTime[] PROGMEM = ", \"PreviousTime\" : ";
prog_char jrpDHAcc[] PROGMEM = ", \"DigitalHighAccumulator\" : ";
prog_char jrpDLAcc[] PROGMEM = ", \"DigitalLowAccumulator\" : ";



void serveJSONresources(EthernetClient& client) {
  unsigned long havepins = 0;   // we just assume PINMAX < 32
  int i;
  int needComma = 0;

  serveJSONproto(client);

  serveStringFromPROGMEM(client, jrLogEntries);

  // figure out which pins have programs 
  for (i = 0; i < NPROGRAMS; i++) {
    if (todoList[i].mode != SAMPLE_NULL)
      havepins |= 1<<todoList[i].pin;
  }

  // output URI for any pins we have programs for

  needComma = 0;
  for (i = 0; i <= PINMAX; i++) {
    if (havepins & (1<<i)) {
      if (needComma)
        serveStringFromPROGMEM(client, jrComma);

      serveStringFromPROGMEM(client, jrpin);
      client.print(i);

      int j;

      // look for a program matching this pin
      // NOTE: This CANNOT fail to find one, because we are (by definition)
      // only looking at the "havepins" pins, which we got from the programs
      for (j = 0; j < NPROGRAMS; j++) {
        if (todoList[j].mode != SAMPLE_NULL && todoList[j].pin == i)
          break;
      }

      char ctmp[PGMNAMELEN+1];
      strncpy(ctmp, todoList[j].pgmname, PGMNAMELEN);
      ctmp[PGMNAMELEN] = '\0';
      serveStringFromPROGMEM(client, jrpgmname);
      client.print(ctmp);

      serveStringFromPROGMEM(client, jrdata);
      client.print(i);
      serveStringFromPROGMEM(client, jrcloseData);
      needComma = 1;
    }
  }
  serveStringFromPROGMEM(client, jrRBrk);
  serveStringFromPROGMEM(client, jrComma);

  // the status object
  serveStringFromPROGMEM(client, jrsStatus);
  unsigned long now = millis();
  client.print(now);
  serveStringFromPROGMEM(client, jrsPM);
  client.print(PACKEDTIME_TO_MSECS(MSECS_TO_PACKEDTIME(now))); 
  serveStringFromPROGMEM(client, jrsUpWrap);
  client.print(uptime_wraps);
  serveStringFromPROGMEM(client, jrsRevision);
  serveStringFromPROGMEM(client, revision);
  serveStringFromPROGMEM(client, jrsMem);
  client.print(mainMemavail);
  serveStringFromPROGMEM(client, jrsMemChg);
  client.print(memAvailChanges);
  serveStringFromPROGMEM(client, jrsBufsize);
  client.print(sampleRing.bsize);
  serveStringFromPROGMEM(client, jrsBufwraps);
  client.print(sampleRing.wrapcount);
  serveStringFromPROGMEM(client, jrsPWC);
  client.print(PACKEDTIME_MASK+1);
  serveStringFromPROGMEM(client, jrRBrace);
  serveStringFromPROGMEM(client, jrComma);

  // the program table info
  serveStringFromPROGMEM(client, jrpPrograms);
  needComma = 0;
  for (i = 0; i < NPROGRAMS; i++) {
    struct samplingProgram *pgm = &todoList[i];

    if (pgm->mode != SAMPLE_NULL) {
      if (needComma)
        serveStringFromPROGMEM(client, jrComma);

      serveStringFromPROGMEM(client, jrpNum);
      client.print(i);
      serveStringFromPROGMEM(client, jrpQS);
      serveProgramString(client, pgm);

      serveStringFromPROGMEM(client, jrpPin);
      client.print(pgm->pin);
      if (pgm->mode == SAMPLE_DIGITAL) {
        serveStringFromPROGMEM(client, jrpDigMode);
        serveStringFromPROGMEM(client, jrpDHAcc);

        // we have to include the current elapsed state time
        unsigned long deltaT = delta_millis(pgm->previousTime, millis());
        unsigned long Hadj = 0;
        unsigned long Ladj = 0;

        // we don't fuss with the msecs; within one second is good enough
        if (pgm->previousVal >= pgm->digitalHigh)
          Hadj = deltaT/1000;
        else
          Ladj = deltaT/1000;

        client.print(pgm->accumHigh.secs + Hadj);
        serveStringFromPROGMEM(client, jrpDLAcc);
        client.print(pgm->accumLow.secs + Ladj);
      }
      else if (pgm->mode == SAMPLE_LEVEL)
        serveStringFromPROGMEM(client, jrpLvlMode);

      serveStringFromPROGMEM(client, jrpPrevVal);
      client.print(pgm->previousVal);
      serveStringFromPROGMEM(client, jrpPrevTime);
      client.print(pgm->previousTime);
      serveStringFromPROGMEM(client, jrRBrace);
      needComma = 1;
    }
  }
  serveStringFromPROGMEM(client, jrRBrk);

  serveStringFromPROGMEM(client, jrRBrace);

  

}


//
// serve individual JSON objects
// Right now the only thing we serve are pin log readings so
// this is really just a flavor of serveRingBuffer and it would
// have been nice to make these two share commonality somehow
//
prog_char jsLog[] PROGMEM = "{ \"Log\" : [ ";
prog_char jsType[] PROGMEM = "{ \"Type\" : \"SAMPLE\" , \"Value\" : ";
prog_char jsTime[] PROGMEM = ", \"Time\" : ";
prog_char jsDT[] PROGMEM = ", \"DeltaT\" : ";
prog_char jsIDmaj[] PROGMEM = ", \"IDmajor\" : ";
prog_char jsIDmin[] PROGMEM = ", \"IDminor\" : ";


void serveJSONobject(EthernetClient &client, struct samplebuffer *sb, int objn) {
  serveJSONproto(client); 

  unsigned long prev_time = MSECS_TO_PACKEDTIME(millis());

  int i = sb->nextsample;
  int wrapID = sb->wrapcount;
  int needComma = 0;

  serveStringFromPROGMEM(client, jsLog);
  for (int cnt = sb->bsize; cnt > 0; cnt--) {
    --i;
    if (i < 0) {
      i = sb->bsize - 1;
      --wrapID;
    }
  
    unsigned long r = sb->sampleReadings[i];

    // When the sensor is first started or the ring buffer is reset
    // there will be a bunch of initialized entries. Don't
    // report these.

    if (r != SR_RESERVED_VALUE && SRPIN(r) == objn) {
      unsigned long t1 = SRPACKEDTIME(r);

      if (needComma) 
        serveStringFromPROGMEM(client, jrComma);
      serveStringFromPROGMEM(client, jsType);
      client.print(SRVAL(r));
      serveStringFromPROGMEM(client, jsTime);
      client.print(PACKEDTIME_TO_MSECS(t1));

      serveStringFromPROGMEM(client, jsDT);
      client.print(PACKEDTIME_TO_MSECS(delta_packed_time(t1, prev_time)));

      serveStringFromPROGMEM(client, jsIDmaj);
      client.print(wrapID);
      serveStringFromPROGMEM(client, jsIDmin);
      client.print(i);

      prev_time = t1;
      serveStringFromPROGMEM(client, jrRBrace);
      needComma = 1;
    }
  } 
  serveStringFromPROGMEM(client, jrRBrk);
  serveStringFromPROGMEM(client, jrRBrace);
}







//
// called by the main loop to check and process web requests periodically
//

void webProcessing() {

  // DHCP lease check/renewal (library only sends request if expired)
  Ethernet.maintain();

  // check for web requests
  EthernetClient client = server.available();
  if (client) {

    // 
    // URL scheme:
    //      GET /        -- output the full ring buffer report
    //
    //      GET /0       -- only output pin 0 values (works for 0 .. 9)
    //        .. GET /1, GET /2, etc
    //
    // JSON INTERFACE -------------------------------------------------------
    //      GET /j       -- json resources, returns summary object
    //                      From there follow the URIs 
    //                      defined in summary object, typically:
    //                         /j/0 to get pin 0 data log, /j/1 etc
    // ----------------------------------------------------------------------
    //
    //      GET /m       -- output memory avail (debug/sanity/stability check)
    //
    //      GET /p       -- output the program table
    //      GET /p?stuff -- load the program table ("stuff" detailed elsewhere)
    //
    //      GET /r       -- resets the sample ring buffer
    //
    //      GET /v0      -- immediately read value of input 0
    //          /v1 ...  -- v1, v2, v3 etc immediately read that value
    //
    //      GET /write-eeprom -- store program params into EEPROM
    //      GET /help    -- print out this info
    //
    // we are NOT parsing the URL very robustly; 
    // get it right or don't get what you want
    //        

    // if your request doesn't fit, too bad (it gets truncated/ignored)
    // all valid requests fit easily
    char *sbuf = tchar_alloc(TMPCHARBUFSIZ);

    char *s = obtainHTTPGET(client, sbuf, TMPCHARBUFSIZ);


    int sendHelp = 0;

    if (s != NULL) {
      int pinnum = -1;

      s += 4;                  // skip over "GET "
      while (*s == '/') s++;   // and any leading slashes


      // a note about rampant tchar_free() calls ... we are freeing
      // up the URL buffer "as soon as we can" so that the called functions
      // can re-use that tchar_alloc() space themselves. If we manually 
      // freed sbuf, sbuf becomes NULL. At the bottom we free the sbuf 
      // in those cases where it wasn't preemptively freed.
      // HOORAY FOR SMALL MEMORY FOOTPRINT HACKS!! :)

      switch (s[0]) {          // note: s[0] == '\0' if just "GET /"
      case 'h':
      case 'H':
        sendHelp = 1;
        break;

      case 'j':                // the JSON interface
      case 'J': 
        if (s[1] == '\0' || s[1] == ' ' ||
            (s[1] == '/' && s[2] == '\0')) {
          sbuf = tchar_free(sbuf); 
          serveJSONresources(client);
        } else if (s[1] == '/') {
          s += 2; // skip to the number part
          if (s[0] >= '0' && s[0] <= '0'+PINMAX && 
              (s[1] == '\0' || s[1] == ' ')) {
            pinnum = s[0] - '0';
            sbuf = tchar_free(sbuf);
            serveJSONobject(client, &sampleRing, pinnum);
          } else
            sendHelp = 1;
        } else
          sendHelp = 1;
        break;

      case 'm':
      case 'M':
        sbuf = tchar_free(sbuf);
        serveMemavail(client, &sampleRing);
        break;

      case 'p':
      case 'P':
        if (s[1] == '\0' || s[1] == ' ') {
          sbuf = tchar_free(sbuf);
          serveProgramTable(client);
        } else if (s[1] == '?') {
          loadProgramTable(s+2);
          sbuf = tchar_free(sbuf);
          serveProgramTable(client);
        } else {
          sendHelp = 1;
        }
        break;

      case 'r':
      case 'R':
        sbuf = tchar_free(sbuf);
        init_samplebuffer(&sampleRing);
        init_paramState();
        serveOK(client);
        break;

      case 'v':
      case 'V':
        pinnum = s[1] - '0';
        sbuf = tchar_free(sbuf);
        serveCurrentReading(client, pinnum);
        break;

      // for write-eeprom unforgiving on case
      // you don't want to do this more often than necessary
      case 'w':
        if (strncmp(s, "write-eeprom", 11) == 0) {
          sbuf = tchar_free(sbuf);
          writeProgramsToEEPROM();
          serveOK(client);
        } else
          sendHelp = 1;
        break;

      // this one is not documented... but you can erase the EEPROM
      // (really just zaps the magic string we look for) via /ZAP-eeprom
      // useful to force a new mac[] addr or other test porpoises
      case 'Z':
        if (strcmp(s, "ZAP-eeprom") == 0) {
          sbuf = tchar_free(sbuf);
          zapEEPROMmagic();
          serveOK(client);
        } else
          sendHelp = 1;
        break;


      default:
        // see if GET /0, GET /1, etc
        if (s[0] >= '0' && s[0] <= '0' + PINMAX)
          pinnum = s[0] - '0';
          
        sbuf = tchar_free(sbuf);
        serveRingBuffer(client, &sampleRing, pinnum);
        break;
      }

      if (sbuf)                       // wasn't freed in above 
        sbuf = tchar_free(sbuf);      // free it now

      if (sendHelp)
        serveHelpString(client);

    }

    client.stop();
  }
}


//
// execute one "sampling instruction" item. Called from main loop
//
void pinProcessing(struct samplingProgram *pgm) {
  unsigned long msec_now = millis();
  int curval = analogRead(pgm->pin);
  unsigned long deltaT = delta_millis(pgm->previousTime, msec_now);

  boolean addIt = false;

  // compute a big "addIt ||=" function, i.e., addIt will be true if
  // ANY of the following criteria are true:
  //
  //    - it's the first time
  //    - it's been more than "timeMax" since a previous sample
  //    - it's been at least timeThreshold and the sample "changed"
  //        (specifics of that depend on LEVEL vs DIGITAL)


  // happens whenever a samplingProgram is first started (no previous state)
  if (pgm->firstTime) 
    addIt = true; 

  if (! addIt) {

    // Don't even look unless it's been more than the bounce window
    // as specified in timeThreshold. NOTE: "timeMax" should be greater
    // than "timeThreshold" or else it won't apply (this is on purpose).

    if (deltaT >= pgm->timeThreshold) {

      // record it if it has been "too long" 
      addIt = (deltaT >= pgm->timeMax);

      // or record it if the value has changed
      if (! addIt) {
        switch (pgm->mode) {
          case SAMPLE_DIGITAL:
            // compare the above/below threshold states, record if different
            if (pgm->previousVal >= pgm->digitalHigh) 
              addIt = (curval <= pgm->digitalLow);
            else
              addIt = (curval >= pgm->digitalHigh);
            break;
      
          case SAMPLE_LEVEL:
            addIt = (abs(curval - pgm->previousVal) >= pgm->deltaThreshold);
            break;
        }
      }
    }
  }

  // For whatever reason, if we decided to record it, do so here
  if (addIt) {
    add_sample(curval, msec_now, pgm->pin, &sampleRing);
    if (pgm->mode == SAMPLE_DIGITAL) {
      // now accumulate the time recorded for the previous state
      if (! pgm->firstTime) {          // (no previous state firstTime)
        struct timeAccumulator *HighLow =
                          (pgm->previousVal >= pgm->digitalHigh) ? 
                          &pgm->accumHigh : &pgm->accumLow;

        int dTmsecs = deltaT % 1000;

        HighLow->msecs += dTmsecs;
        if (HighLow->msecs > 1000) {
          HighLow->secs++;
          HighLow->msecs -= 1000;
        }
        HighLow->secs += (deltaT/1000);
      }
    }
    pgm->previousVal = curval;
    pgm->previousTime = msec_now;
  }

  pgm->firstTime = false;
}


// the setup routine runs once when you press reset:
void setup() {
  EEPROMmac(mac);   // get MAC addr, or init it if not there

  Ethernet.begin(mac);
  server.begin();

  readProgramsFromEEPROM();
  
#define NSAMPLESMAX 250    // might not really get this many on small Arduinos

  alloc_samplebuffer(&sampleRing, NSAMPLESMAX); 
  init_paramState();
}

//
// the loop routine is called over and over again
//

unsigned long previous_now;


void loop() {
  unsigned long now = millis();
  struct samplingProgram *params;
  
  // entirely for reporting purposes only, count time wraps (~~50 days)
  if (now < previous_now)
    ++uptime_wraps;
  previous_now = now;

  // entirely for reporting/debugging only, determine available memory
  if (freeRam() != mainMemavail) {
    if (mainMemavail > 0)           // it's zero the first time/special case
      ++memAvailChanges;            // possible leak, count them
    mainMemavail = freeRam();       // new baseline
  }

  // once through each time look for web requests, where we
  // will report results and/or accept new "programming" instructions
  webProcessing();

  // go through the current list of "programming" instructions for 
  // what pins to read, how often to sample them, etc
  for (params = todoList; params < &todoList[NPROGRAMS]; params++) {
    if (params->mode != SAMPLE_NULL)
      pinProcessing(params);
  }
}

