/*
   phxSpeakingClock, a specking clock with the possibility of personalized and programmable
   announcements both as time and day of the week.

   (c) 2022 Guglielmo Braguglia
   Phoenix Sistemi & Automazione s.a.g.l. - Muralto - Switzerland

   Made for Seeedstudio XIAO M0 (ATSAMD21) and for WeMos ESP32 D1 Mini (ESP32)

*/

/******************************************************************************
   PROGRAM DEFINITIONS
 ******************************************************************************/

// #define DEBUG                                          // uncomment this line ONLY for debug and then keep the serial monitor opened

#if defined ( ESP_PLATFORM )

#pragma message "Compiling for ESP32 D1 Mini"          // Compiling for ESP32
#define PROG_VER  "1.2.1.ESP"                          // Program version
#define JQ8400_BUSY        26                          // pin on ESP32 to read the BUSY signal from JQ8400 (HIGH = BUSY, LOW = FREE)
#define BLTIN_LED_ON     HIGH                          // adjust for your board LED ON
#define BLTIN_LED_OFF     LOW                          // adjust for your board LED OFF

#elif defined ( ARDUINO_SEEED_XIAO_M0 )

#pragma message "Compiling for ATSAMD21 XIAO M0"       // Compiling for ATSAMD21
#define PROG_VER  "1.2.1 SAM"                          // Program version
#define JQ8400_BUSY         8                          // pin on XIAO to read the BUSY signal from JQ8400 (HIGH = BUSY, LOW = FREE)
#define BLTIN_LED_ON      LOW                          // adjust for your board LED ON
#define BLTIN_LED_OFF    HIGH                          // adjust for your board LED OFF

#else
#error "ERROR --- this program is made only for ESP32 D1 Mini or ATSAMD21 on SeeedStudio XIAO M0"
#endif

#define DS3231_I2C_ADDR  0x68                          // DS3231 I2C bus address
#define E24C32_I2C_ADDR  0x57                          // EEPROM 24C32 I2C bus address
#define DISP7S_I2C_ADDR  0x70                          // 7-segment display I2C bus address

#define BLOCK_ID     "$GB$17"                          // Remeber to change BLOCK_ID if you change the structure of EEPROM stored info
#define MAX_USR_MSG         6                          // maximum number or audio user messages
#define MAX_USR_LNG         8                          // maximum number, plus one, of single audio messages in a user message (max messages = MAX_USR_LNG - 1)
#define MAX_ANN_LNG        30                          // maximum number of audio message in an announcement

#define EEPROM_W_TIME  120000                          // EEPROM wait time before saving parameters in milliseconds (120000 ms. = 2 min.)

/******************************************************************************
   PROGRAM INCLUDES
 ******************************************************************************/

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <Wire.h>
#include <Eeprom24C32_64.h>
#include <Adafruit_GFX.h>
#include <Adafruit_LEDBackpack.h>
#include <JQ8400_Serial.h>
#include <SerialCmd.h>
#include "crc32.h"
#include "announcements.h"

#if defined ( ESP_PLATFORM )
#include <HardwareSerial.h>
#include <WiFi.h>
#elif defined ( ARDUINO_SEEED_XIAO_M0 )
#include <wdt_samd21.h>
#endif

/******************************************************************************
   VERIFY REQUIREMENTS
 ******************************************************************************/

#if !defined ( ESP_PLATFORM ) && !defined ( ARDUINO_SEEED_XIAO_M0 )
#error "ERROR --- unsupported platform"
#endif

#if ( SERIALCMD_MAXCMDNUM < 16 )
#error "ERROR --- in SerialCmd.h SERIALCMD_MAXCMDNUM must be >= 16"
#endif
#if ( SERIALCMD_MAXCMDLNG < 6  )
#error "ERROR --- in SerialCmd.h SERIALCMD_MAXCMDLNG must be >= 6"
#endif
#if ( SERIALCMD_MAXBUFFER < 64 )
#error "ERROR --- in SerialCmd.h SERIALCMD_MAXBUFFER must be >= 64"
#endif
#if ( ANNOUNCEMENTS_VER  != 2  )
#error "ERROR --- in announcements.h, wrong version number"
#endif

/******************************************************************************
   GLOBAL VARIABLES, ENUMS & STRUCT
 ******************************************************************************/

#if defined ( ESP_PLATFORM )

HardwareSerial MySerial ( 2 );                         // constructor for Serial2 on ESP32 (Serial2 on ESP32: TX IO17, RX IO16)
JQ8400_Serial  mp3 ( MySerial );                       // constructor for JQ8400 audio module (Serial2 on ESP32: TX IO17, RX IO16)
SerialCmd      mySerCmd ( Serial );                    // constructor for SerialCmd library (Serial port)
WiFiServer     server ( 80 );                          // create a WiFi WEB Server
const char*    ssid     = "SpeakingClock_ESP32";       // ESP32 AP SSID
const char*    pswd     = "123456789";                 // ESP32 AP password
IPAddress      ESP32_IP;

#elif defined ( ARDUINO_SEEED_XIAO_M0 )

JQ8400_Serial  mp3 ( Serial1 );                        // constructor for JQ8400 audio module (Serial1 on XIAO SAM D21: TX D6, RX D7)
SerialCmd      mySerCmd ( SerialUSB );                 // constructor for SerialCmd library (SerialUSB port)

#endif

static Eeprom24C32_64 eeprom ( E24C32_I2C_ADDR );     // constructor for external EEPROM library
Adafruit_7segment clockDisplay = Adafruit_7segment(); // constructor for 7-segment I2C display

uint8_t     fCmdFromUSB = true;                       // flag to indicate if a command is from USB or from HTTP (on ESP32)

time_t utcTime;
tm     theTime;
tm     rtcTime;

uint8_t  announce_msg[MAX_ANN_LNG];
uint8_t  led_status    = BLTIN_LED_OFF;
uint8_t  dsp_status    = 0;
uint8_t  last_minute   = 0;
uint8_t  announce_hour = 0;
uint8_t  announce_min  = 0;
uint8_t  announce_idx  = 0;

uint16_t displayValue  = 0;

uint32_t actual_time   = 0;
uint32_t RTC_millis    = 0;
uint32_t LED_millis    = 0;
uint32_t EEP_millis    = 0;

uint32_t paramsCRC     = 0;

enum ANNOUNCE_INCREMENT {
   INC_10_MIN = 10,
   INC_15_MIN = 15,
   INC_30_MIN = 30,
   INC_60_MIN = 60
};

struct message {
   uint8_t  valid_days;                                // message valid days (0 = all, 1 .. 254 days binary encoded, Sun = bit 1 ... Sat = bit 7, bit 0 = reserved)
   uint32_t start_time;                                // message start time
   uint32_t stop_time;                                 // message stop time
   uint8_t  msgs_list[MAX_USR_LNG + 1];                // up to MAX_USR_LNG chained messages, zero terminated (so +1)
};

struct params {
   char     blockID [7];                               // block_id to identify valid params
   bool     use_DST;                                   // automatically adjust the clock following DST (true/flase)
   uint8_t  display_bright;                            // display brightness during day   (0 .. 15);
   uint8_t  display_bright_night;                      // display brightness in the night (0 .. 15);
   uint8_t  audio_period;                              // period of audio announcements (10/15/30/60 minutes)
   uint32_t audio_on;                                  // audio-on time and day brightness display
   uint32_t audio_off;                                 // audio-off time and night brightness display
   uint8_t  audio_off_day;                             // audio off day (0 = none, 1 Sunday .. 7 Saturday)
   uint8_t  audio_volume;                              // audio volume (0 .. 30)
   bool     audio_add_day;                             // adds the date in the time announcement (true/false)
   message  user_msgs[MAX_USR_MSG];                    // Array of custom user message list (max MAX_USR_MSG)
} clock_params;

#ifdef DEBUG
char daysNames[][4] = {"---", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
volatile uint8_t k  = 0;                               // declared 'volatile' to avoid optimization problems
#endif

/******************************************************************************
   USER FUNCTIONS
 ******************************************************************************/

// ------------------ save_params -----------------------

void save_params ( void ) {
   eeprom.writeBytes ( 0x00, sizeof ( params ), ( byte* ) &clock_params );
   delay ( 25 );
   paramsCRC = calc_crc32 ( ( uint8_t* ) &clock_params, sizeof ( params ) );
   eeprom.writeBytes ( sizeof ( params ), 4, ( byte* )  &paramsCRC );
   delay ( 25 );
}

// ------------------ read_params -----------------------

void read_params ( void ) {
   eeprom.readBytes ( 0x00, sizeof ( params ), ( byte* ) &clock_params );
   delay ( 25 );
   eeprom.readBytes ( sizeof ( params ), 4, ( byte* )  &paramsCRC );
   //
   if ( ( strcmp ( clock_params.blockID, BLOCK_ID ) != 0 ) ||
         ( paramsCRC != calc_crc32 ( ( uint8_t* ) &clock_params, sizeof ( params ) ) ) ) {
#ifdef DEBUG
      mySerCmd.Print ( ( char * ) "INFO: Invalid parametrs or CRC, writing default values \r\n" );
#endif
      //
      // invalid blockID, EEPROM must be initialized with initial values
      strlcpy ( clock_params.blockID, BLOCK_ID, 7 );
      clock_params.use_DST = true;
      clock_params.display_bright = 15;
      clock_params.display_bright_night = 5;
      clock_params.audio_period = INC_15_MIN;
      clock_params.audio_on  = ( ( uint32_t )  9 * 3600 ) + ( ( uint32_t ) 0 * 60 );   // audio ON  at 09:00
      clock_params.audio_off = ( ( uint32_t ) 21 * 3600 ) + ( ( uint32_t ) 0 * 60 );   // audio OFF at 21:00
      clock_params.audio_off_day = 0;
      clock_params.audio_volume  = 20;
      clock_params.audio_add_day = false;
      for ( uint8_t i = 0; i < MAX_USR_MSG; i++ ) {
         clock_params.user_msgs[i].valid_days = 0;
         clock_params.user_msgs[i].start_time = 0;
         clock_params.user_msgs[i].stop_time  = 0;
         memset ( clock_params.user_msgs[i].msgs_list, 0x00, MAX_USR_LNG );
      }
      save_params();
   }
}

// ------------------- OnOffTime ------------------------

bool OnOffTime ( uint32_t actualTime, uint32_t onTime, uint32_t offTime ) {
   // All times are uint32_t and are expressed in seconds (0 .. 86399)
   // return true or false to indicate that is time to stay ON or OFF
   if ( onTime <= offTime ) {
      // The ON time is less or equal than the OFF time ...
      // 0 --------- ON +++++++++ OFF ----- 86399 -- 0 ----------- ON +++
      if ( ( actualTime < onTime ) || ( actualTime >= offTime ) ) return false;
      else return true;
   } else {
      // The ON time is greater than the OFF time ...
      // 0 +++ OFF ----- ON +++++++++++++ 86399 ++ 0 +++ OFF ----- ON +++
      if ( ( actualTime >= offTime ) && ( actualTime < onTime ) ) return false;
      else return true;
   }
}

void updateTheTime ( void ) {
   // declare a Lamba function to evaluate if is DST ...
   auto is_DST = [] ( uint8_t d, uint8_t m, uint16_t y, uint8_t h ) -> bool {
      y += 2000;
      // Day in March that DST starts on, at 2 am in Italy
      uint8_t dstOn = ( 31 - ( 5 * y / 4 + 4 ) % 7 );
      // Day in October that DST ends  on, at 3 am in Italy
      uint8_t dstOff = ( 31 - ( 5 * y / 4 + 1 ) % 7 );
      //
      if ( ( m > 3 && m < 10 ) ||
      ( m == 3 && ( d > dstOn || ( d == dstOn && h >= 2 ) ) ) ||
      ( m == 10 && ( d < dstOff || ( d == dstOff && h <= 3 ) ) ) )
         return true;
      else
         return false;
   };
   //
   rtcTime.tm_isdst = 0;                              // isdst   - use "standard time" (0 sdt, 1 dst, -1 use TZ)
   //
   rtcTime.tm_year += 100;                            // UTC start from 1900
   rtcTime.tm_mon  -= 1;                              // months goes from 0 to 11
   rtcTime.tm_wday -= 1;                              // week days goes from 0 to 6
   utcTime = mktime ( &rtcTime );                     // convert the tm structure to UTC
   rtcTime.tm_year -= 100;                            // RTC (DS3231) start from 2000
   rtcTime.tm_mon  += 1;                              // RTC (DS3231) months goes from 1 to 12
   rtcTime.tm_wday += 1;                              // RTC (DS3231) week days goes from 1 to 7
   //
   if ( is_DST ( rtcTime.tm_mday, rtcTime.tm_mon, rtcTime.tm_year, rtcTime.tm_hour ) ) {
      utcTime += 3600;                                // if DST add 1 hour (3600 sec) to utcTime
   }
   //
   theTime = *gmtime ( &utcTime );                    // convert the corrected UTC to tm structure
   theTime.tm_year -= 100;                            // RTC (DS3231) start from 2000
   theTime.tm_mon  += 1;                              // RTC (DS3231) months goes from 1 to 12
   theTime.tm_wday += 1;                              // RTC (DS3231) week days goes from 1 to 7
}

// ------------------ read_ds3231 -----------------------

void read_ds3231 ( void ) {
   // declare a Lamba function to convert from BCD to DEC ...
   auto bcdToDec = [] ( uint8_t val ) -> uint8_t {
      return ( ( val / 16 * 10 ) + ( val % 16 ) );
   };
   //
   Wire.beginTransmission ( DS3231_I2C_ADDR );
   Wire.write ( 0x00 );
   Wire.endTransmission();
   Wire.requestFrom ( DS3231_I2C_ADDR, 7 );
   rtcTime.tm_sec  = bcdToDec ( Wire.read() & 0x7f ); // seconds - byte 0x00, value 0 .. 59
   rtcTime.tm_min  = bcdToDec ( Wire.read() );        // minutes - byte 0x01, value 0 .. 59
   rtcTime.tm_hour = bcdToDec ( Wire.read() & 0x3f ); // hours   - byte 0x02, value 0 .. 23
   rtcTime.tm_wday = bcdToDec ( Wire.read() );        // wday    - byte 0x03, value 1 .. 7 (1 = Sun)
   rtcTime.tm_mday = bcdToDec ( Wire.read() );        // date    - byte 0x04, value 1 .. 31
   rtcTime.tm_mon  = bcdToDec ( Wire.read() );        // month   - byte 0x05, value 1 .. 12
   rtcTime.tm_year = bcdToDec ( Wire.read() );        // year    - byte 0x06, value 0 .. 99
   //
   updateTheTime();
}

// ------------------ write_ds3231 ----------------------

void write_ds3231 ( void ) {
   // declare a Lamba function to convert from DEC to BCD ...
   auto decToBcd = [] ( uint8_t val ) -> uint8_t {
      return ( ( val / 10 * 16 ) + ( val % 10 ) );
   };
   //
   Wire.beginTransmission ( DS3231_I2C_ADDR );
   Wire.write ( 0x00 );
   Wire.write ( decToBcd ( rtcTime.tm_sec ) );        // seconds 0 .. 59
   Wire.write ( decToBcd ( rtcTime.tm_min ) );        // minutes 0 .. 59
   Wire.write ( decToBcd ( rtcTime.tm_hour ) );       // hours   0 .. 23
   Wire.write ( decToBcd ( rtcTime.tm_wday ) );       // wday    1 .. 7 (1 = Sun)
   Wire.write ( decToBcd ( rtcTime.tm_mday ) );       // date    1 .. 31
   Wire.write ( decToBcd ( rtcTime.tm_mon ) );        // month   1 .. 12
   Wire.write ( decToBcd ( rtcTime.tm_year ) );       // year    0 .. 99
   Wire.endTransmission();
   //
   updateTheTime();
   //
   // Clock adjusted, so ... adjust announce_hour and announce_min for next announce
   announce_hour = theTime.tm_hour;
   if ( clock_params.audio_period == INC_60_MIN ) {
      announce_min = 0;
      announce_hour++;
   } else {
      announce_min = ( ( theTime.tm_min / clock_params.audio_period ) + 1 ) * clock_params.audio_period;
      if ( announce_min > 59 ) {
         announce_min = 0;
         announce_hour++;
      }
   }
#ifdef DEBUG
   mySerCmd.Print ( ( char * ) "INFO: The next announcement will be at time " );
   mySerCmd.Print ( announce_hour );
   mySerCmd.Print ( ':' );
   mySerCmd.Print ( announce_min );
   mySerCmd.Print ( ( char * ) " \r\n\r\n" );
#endif
}

// --------------- addDateToAnnounce --------------------

void addDateToAnnounce ( void ) {
   // Add the announcement of the current date to the announcement already prepared in the loop()
   // NOTE: This function requires ANNOUNCEMENTS_VER == 2
   announce_msg[announce_idx] = DI;                                       // on
   announce_idx++;
   announce_msg[announce_idx] = DOMENICA - 1 + theTime.tm_wday;           // day name
   announce_idx++;
   if ( theTime.tm_mday == 1 )
      announce_msg[announce_idx] = PRIMO;                                 // first (1)
   else if ( theTime.tm_mday <= 23 )
      announce_msg[announce_idx] = UNA - 1 + theTime.tm_mday;             // day number (2 .. 23)
   else
      announce_msg[announce_idx] = VENTIQUATTRO - 24 + theTime.tm_mday;   // day number (24 .. 31)
   announce_idx++;
   announce_msg[announce_idx] = GENNAIO - 1 + theTime.tm_mon;             // month name
   announce_idx++;
}

// ------------------ myDrawColon -----------------------

void myDrawColon ( bool state ) {
   uint8_t b = 0x00;
   //
   if ( EEP_millis != 0 ) b = 0x08;               // left colon - upper dot
   if ( state ) b |= 0x02;                        // center colon (both dots)
   clockDisplay.writeDigitRaw ( 2, b );           // update 7 seg display buffer
}

#if defined ( ESP_PLATFORM )

// ----------------- sendPARAMtoHTML --------------------

void sendPARMtoHTML ( WiFiClient client ) {
   // print to client all parameters
   uint8_t hh, mn;
   //
   hh = mn = 0;
   client.print   ( "INFO: All parameters list is:" );
   client.println ( "<br>" );
   //
   client.print   ( "&nbsp; - Block ID       :" );
   client.print   ( clock_params.blockID );
   client.println ( "<br>" );
   //
   client.print   ( "&nbsp; - Use DST        :" );
   if ( clock_params.use_DST ) client.print ( "true" );
   else client.print ( "false" );
   client.println ( "<br>" );
   //
   //
   client.print   ( "&nbsp; - Dis.bri. day   :" );
   client.print   ( clock_params.display_bright );
   client.println ( "<br>" );
   //
   //
   client.print   ( "&nbsp; - Dis.bri. night :" );
   client.print   ( clock_params.display_bright_night );
   client.println ( "<br>" );
   //
   //
   client.print   ( "&nbsp; - Period of ann. :" );
   client.print   ( clock_params.audio_period );
   client.println ( "<br>" );
   //
   //
   hh = ( uint8_t ) ( clock_params.audio_on / 3600 );
   mn = ( uint8_t ) ( ( clock_params.audio_on - ( hh * 3600 ) ) / 60 );
   client.print   ( "&nbsp; - Audio ON time  :" );
   client.print   ( hh );
   client.print   ( ',' );
   client.print   ( mn );
   client.println ( "<br>" );
   //
   //
   hh = ( uint8_t ) ( clock_params.audio_off / 3600 );
   mn = ( uint8_t ) ( ( clock_params.audio_off - ( hh * 3600 ) ) / 60 );
   client.print   ( "&nbsp; - Audio OFF time :" );
   client.print   ( hh );
   client.print   ( ',' );
   client.println ( mn );
   client.println ( "<br>" );
   //
   client.print   ( "&nbsp; - Audio off day  :" );
   client.print   ( clock_params.audio_off_day );
   client.println ( "<br>" );
   //
   //
   client.print   ( "&nbsp; - Audio volume   :" );
   client.print   ( clock_params.audio_volume );
   client.println ( "<br>" );
   //
   //
   client.print   ( "&nbsp; - Audio add day  :" );
   if ( clock_params.audio_add_day ) client.print ( "true" );
   else client.print ( "false" );
   client.println ( "<br>" );
   //
   //
   mySerCmd.Print ( ( char * ) "&nbsp; - User messages  :" );
   mySerCmd.Print ( ( char * ) " \r\n" );
   for ( uint8_t i = 0; i < MAX_USR_MSG; i++ ) {
      //
      client.print   ( "&nbsp; &nbsp; &nbsp; Msg:" );
      client.print   ( i );
      client.print   ( " - " );
      client.print   ( clock_params.user_msgs[i].valid_days );
      client.print   ( ',' );
      hh = ( uint8_t ) ( clock_params.user_msgs[i].start_time / 3600 );
      mn = ( uint8_t ) ( ( clock_params.user_msgs[i].start_time - ( hh * 3600 ) ) / 60 );
      client.print   ( hh );
      client.print   ( ':' );
      client.print   ( mn );
      client.print   ( ',' );
      hh = ( uint8_t ) ( clock_params.user_msgs[i].stop_time / 3600 );
      mn = ( uint8_t ) ( ( clock_params.user_msgs[i].stop_time - ( hh * 3600 ) ) / 60 );
      client.print   ( hh );
      client.print   ( ':' );
      client.print   ( mn );
      client.print   ( ',' );
      for ( uint8_t j = 0; j < MAX_USR_LNG; j++ ) {
         client.print ( clock_params.user_msgs[i].msgs_list[j] );
         client.print ( ',' );
      }
      client.println ( "<br>" );
      //
   }
   //
   client.print ( "&nbsp; - EEPROM status  :" );
   if ( EEP_millis == 0 ) {
      client.print ( "saved" );
   } else {
      client.print ( ( char * ) "NOT saved" );
   }
   client.println ( "<br>" );
}

// ---------------- checkWiFiCommand --------------------

char* checkWiFiCommand ( void ) {
   static const uint8_t  BUF_LNG      = 200;      // HTML buffer max lenght
   static const uint8_t  CMD_LNG      = 128;      // Command buffer max lenght
   static const uint16_t NET_TIMEOUT  = 2000;     // Browser session timeout in msec.
   static char           clientBuffer[BUF_LNG];   // HTML buffer where to receive HTML data
   static char           clientCommand[CMD_LNG];  // Command buffer to pass data to SerialCmd
   //
   uint8_t               buf_idx      =  0;       // clientBuffer index
   int8_t                lstCmdStatus = -1;       // indicate the last command status -1:none, 0:NOT valid, 1:valid
   uint32_t              lastMillis   =  0;       // used to calculate the browser session timeout
   char                  c            =  0;       // single char to receive characters from client
   char*                 cmdStart     =  NULL;    // pointer to the begin of a command (after "GET /")
   char*                 cmdStop      =  NULL;    // pointer to the end of a command (before "HTTP")
   char*                 retVal       =  NULL;    // pointer to the updated clientCommand if a command is available or NULL
   //
   WiFiClient client = server.available();
   if ( client ) {
      // Client connected ...
      memset ( clientBuffer,  0x00, BUF_LNG );
      memset ( clientCommand, 0x00, CMD_LNG );
      buf_idx = 0;
      lastMillis = millis();
      //
      while ( client.connected() ) {
         if ( client.available() ) {
            c = client.read();
            lastMillis = millis();
            //
            if ( c == '\n' ) {
               //
               // New Line received ...
               if ( strlen ( clientBuffer ) == 0 ) {
                  //
                  // ... empty line, send response to client
                  client.println ( "HTTP/1.1 200 OK" );
                  client.println ( "Content-type:text/html" );
                  client.println ( "Connection: close" );
                  client.println();
                  //
                  // Display the HTML web page
                  client.println ( "<!DOCTYPE html><html>" );
                  client.print   ( "<body><h1>phxSpeakingClock ver." );
                  client.print   ( PROG_VER );
                  client.println ( " </h1>" );
                  client.println ( "<p>Please, enter a valid command using the following syntax: http://192.168.4.1/command,parameters</p>" );
                  //
                  sendPARMtoHTML ( client );
                  //
                  if ( SERIALCMD_FORCEUC != 0 ) {
                     client.println ( "<p>Note: lower case characters will be converted to upper case.</p>" );
                  }
                  //
                  if ( lstCmdStatus == 0 ) {
                     client.println ( "<p>Last entered command was <b>NOT</b> recognized.</p>" );
                     lstCmdStatus = -1;
                     retVal = NULL;
                  } else if ( lstCmdStatus == 1 ) {
                     client.println ( "<p>Last entered command WAS recognized and will be executed.</p>" );
                     lstCmdStatus = -1;
                  }
                  client.println ( "</body></html>" );
                  //
                  client.println();
                  break;
               } else {
                  //
                  // ... search for HTTP GET line
                  cmdStart = strstr ( clientBuffer, "GET /" );
                  if ( cmdStart != NULL ) {
                     cmdStop = strstr ( clientBuffer, "HTTP" );
                     if ( cmdStop != NULL ) {
                        if ( ( int ) ( cmdStop - cmdStart - 5 ) < CMD_LNG ) {
                           strlcpy ( clientCommand, ( cmdStart + 5 ), ( int ) ( cmdStop - cmdStart - 5 ) );
                           if ( strcmp ( "favicon.ico", clientCommand ) != 0 ) {
                              retVal = clientCommand;
                              lstCmdStatus = mySerCmd.ReadString ( retVal, true );
                           }
                        }
                     }
                  }
                  memset ( clientBuffer,  0x00, BUF_LNG );
                  buf_idx = 0;
               }
            } else {
               if ( c != '\r' ) {
                  if ( buf_idx < ( BUF_LNG - 1 ) )
                     clientBuffer[buf_idx++] = c;
               }
            }
         }
         if ( millis() - lastMillis > NET_TIMEOUT ) break;
      }
      client.stop();
   }
   return retVal;
}

#endif

//
// -------------- SerialCommand functions ---------------
//

void send_OK ( void ) {
   if ( fCmdFromUSB )
      mySerCmd.Print ( ( char * ) "OK\r\n" );
}

void send_ERR ( void ) {
   if ( fCmdFromUSB )
      mySerCmd.Print ( ( char* ) "ERR\r\n" );
}

void set_DTTM ( void ) {
   // Command syntax : SETDT,YY,MM,GG,HH,MN,SS,DW
   // Set date/time
   uint8_t yy, mm, gg, hh, mn, ss, dw;
   //
   yy = mm = gg = hh = mn = ss = dw = 0;
   yy = atoi ( mySerCmd.ReadNext() );
   mm = atoi ( mySerCmd.ReadNext() );
   gg = atoi ( mySerCmd.ReadNext() );
   hh = atoi ( mySerCmd.ReadNext() );
   mn = atoi ( mySerCmd.ReadNext() );
   ss = atoi ( mySerCmd.ReadNext() );
   dw = atoi ( mySerCmd.ReadNext() );
   //
   if ( ( yy < 100 ) && ( mm < 13 ) && ( gg < 32 ) &&
         ( hh < 24 )  && ( mn < 60 ) && ( ss < 60 ) && ( dw < 8 ) ) {
      send_OK();
      rtcTime.tm_year = yy;
      rtcTime.tm_mon  = mm;
      rtcTime.tm_mday = gg;
      rtcTime.tm_wday = dw;
      rtcTime.tm_hour = hh;
      rtcTime.tm_min  = mn;
      rtcTime.tm_sec  = ss;
      write_ds3231();
   } else {
      send_ERR();
   }
}

void get_DTTM ( void ) {
   // Command syntax : GETDT
   // Get date/time
   mySerCmd.Print ( ( char * ) "INFO: RTC (STD) date/time: " );
   mySerCmd.Print ( rtcTime.tm_year );
   mySerCmd.Print ( ',' );
   mySerCmd.Print ( rtcTime.tm_mon );
   mySerCmd.Print ( ',' );
   mySerCmd.Print ( rtcTime.tm_mday );
   mySerCmd.Print ( ',' );
   mySerCmd.Print ( rtcTime.tm_hour );
   mySerCmd.Print ( ',' );
   mySerCmd.Print ( rtcTime.tm_min );
   mySerCmd.Print ( ',' );
   mySerCmd.Print ( rtcTime.tm_sec );
   mySerCmd.Print ( ',' );
   mySerCmd.Print ( rtcTime.tm_wday );
   mySerCmd.Print ( ( char * ) " \r\n" );
   send_OK();
}

void set_UDST ( void ) {
   // Command syntax : SETUD,DS
   // Set use DST
   uint8_t ds = 0;
   //
   ds = atoi ( mySerCmd.ReadNext() );
   //
   if ( ds < 2 ) {
      send_OK();
      if ( ds == 1 ) clock_params.use_DST = true;
      else clock_params.use_DST = false;
      EEP_millis = millis();
      if ( EEP_millis == 0 ) EEP_millis = 1;
   } else {
      send_ERR();
   }
}

void set_DIBT ( void ) {
   // Command syntax : SETDB,DB,DN
   // Set display brightness day,night
   uint8_t db, dn;
   //
   db = dn = 0;
   db = atoi ( mySerCmd.ReadNext() );
   dn = atoi ( mySerCmd.ReadNext() );
   //
   if ( ( db < 16 ) && ( dn < 16 ) ) {
      send_OK();
      clock_params.display_bright       = db;   // day brightness
      clock_params.display_bright_night = dn;   // night brightness
      clockDisplay.setBrightness ( db );
      EEP_millis = millis();
      if ( EEP_millis == 0 ) EEP_millis = 1;
   } else {
      send_ERR();
   }
}

void set_PERA ( void ) {
   // Command syntax : SETPA,MN
   // Set period of announcements
   uint8_t mn = 0;
   //
   mn = atoi ( mySerCmd.ReadNext() );
   //
   if ( ( mn == INC_10_MIN ) || ( mn = INC_15_MIN ) || ( mn = INC_30_MIN ) || ( mn = INC_60_MIN ) ) {
      send_OK();
      clock_params.audio_period = mn;
      //
      //Audio period is changed, read the DS3231 and adjust announce_hour & announce_min for next announce
      read_ds3231();
      //
      announce_hour = theTime.tm_hour;
      if ( clock_params.audio_period == INC_60_MIN ) {
         announce_min = 0;
         announce_hour++;
      } else {
         announce_min = ( ( theTime.tm_min / clock_params.audio_period ) + 1 ) * clock_params.audio_period;
         if ( announce_min > 59 ) {
            announce_min = 0;
            announce_hour++;
         }
      }
#ifdef DEBUG
      mySerCmd.Print ( ( char * ) "INFO: The next announcement will be at time " );
      mySerCmd.Print ( announce_hour );
      mySerCmd.Print ( ':' );
      mySerCmd.Print ( announce_min );
      mySerCmd.Print ( ( char * ) " \r\n\r\n" );
#endif
      //
      EEP_millis = millis();
      if ( EEP_millis == 0 ) EEP_millis = 1;
   } else {
      send_ERR();
   }
}

void set_TMON ( void ) {
   // Command syntax : SETON,HH,MN
   // Set audio ON time for announcements
   uint8_t hh, mn;
   //
   hh = mn = 0;
   hh = atoi ( mySerCmd.ReadNext() );
   mn = atoi ( mySerCmd.ReadNext() );
   //
   if ( ( hh < 24 ) && ( mn < 60 ) ) {
      send_OK();
      clock_params.audio_on = ( ( uint32_t ) hh * 3600 ) + ( ( uint32_t ) mn * 60 );
      EEP_millis = millis();
      if ( EEP_millis == 0 ) EEP_millis = 1;
   } else {
      send_ERR();
   }
}

void set_TMOF ( void ) {
   // Command syntax : SETOF,HH,MN
   // Set audio OFF time for announcements
   uint8_t hh, mn;
   //
   hh = mn = 0;
   hh = atoi ( mySerCmd.ReadNext() );
   mn = atoi ( mySerCmd.ReadNext() );
   //
   if ( ( hh < 24 ) && ( mn < 60 ) ) {
      send_OK();
      clock_params.audio_off = ( ( uint32_t ) hh * 3600 ) + ( ( uint32_t ) mn * 60 );
      EEP_millis = millis();
      if ( EEP_millis == 0 ) EEP_millis = 1;
   } else {
      send_ERR();
   }
}

void set_ODAY ( void ) {
   // Command syntax : SETOD,wd
   // Set audio off week day wd = 0 .. 7 (1 = Sunday, 7 = Saturday, 0 = all days on)
   uint8_t wd = 0;
   //
   wd = atoi ( mySerCmd.ReadNext() );
   //
   if ( wd <= 7 ) {
      send_OK();
      clock_params.audio_off_day = wd;
      delay ( 100 );
      EEP_millis = millis();
      if ( EEP_millis == 0 ) EEP_millis = 1;
   } else {
      send_ERR();
   }
}

void set_AVOL ( void ) {
   // Command syntax : SETAV,LV
   // Set audio volume
   uint8_t lv = 0;
   //
   lv = atoi ( mySerCmd.ReadNext() );
   //
   if ( lv <= 30 ) {
      send_OK();
      clock_params.audio_volume = lv;
      mp3.setVolume ( lv );
      delay ( 100 );
      EEP_millis = millis();
      if ( EEP_millis == 0 ) EEP_millis = 1;
   } else {
      send_ERR();
   }
}

void set_UDAY ( void ) {
   // Command syntax : SETDY,UD
   // Set flag to adds the date in the time announcement
   uint8_t ud = 0;
   //
   ud = atoi ( mySerCmd.ReadNext() );
   //
   if ( ud < 2 ) {
      send_OK();
      if ( ud == 1 ) clock_params.audio_add_day = true;
      else clock_params.audio_add_day = false;
      EEP_millis = millis();
      if ( EEP_millis == 0 ) EEP_millis = 1;
   } else {
      send_ERR();
   }
}

void set_AMSG ( void ) {
   // Command syntax : SETAM,MN,VD,HO,MO,HF,MF,M1,M2,... up to max of (MAX_USR_LNG - 1) messages
   // Set User defined announcement (played after the standard announcement)
   uint8_t mi, vd, ho, mo, hf, mf;
   uint8_t ms[MAX_USR_LNG];
   char *  rn;
   //
   mi = vd = ho = mo = hf = mf = 0;
   memset ( ms, 0x00, MAX_USR_LNG );
   //
   mi = atoi ( mySerCmd.ReadNext() );    // message index (0 .. (MAX_USR_MSG - 1))
   vd = atoi ( mySerCmd.ReadNext() );    // message valid days (0 = all, 1 .. 254 = binary encoded days)
   ho = atoi ( mySerCmd.ReadNext() );    // message start time - hours (0 .. 23)
   mo = atoi ( mySerCmd.ReadNext() );    // message start time - minutes (0 .. 59)
   hf = atoi ( mySerCmd.ReadNext() );    // message stop time  - hours (0 .. 23)
   mf = atoi ( mySerCmd.ReadNext() );    // message stop time  - minutes (0 .. 59)
   for ( uint8_t i = 0; i < ( MAX_USR_LNG - 1 ); i++ ) {
      rn = mySerCmd.ReadNext();
      if ( rn == NULL ) {
         break;
      }
      ms[i] = atoi ( rn );
      if ( ms[i] == 0 ) {
         for ( uint8_t j = i; j < ( MAX_USR_LNG ); j++ ) ms[j] = 0;
         break;
      }
   }
   //
   if ( ( mi < ( MAX_USR_MSG - 1 ) ) && ( vd < 255 ) && ( ho < 24 ) && ( mo < 60 ) && ( hf < 24 ) && ( mf < 60 ) ) {
      send_OK();
      clock_params.user_msgs[mi].valid_days = vd;
      clock_params.user_msgs[mi].start_time = ( ( uint32_t ) ho * 3600 ) + ( ( uint32_t )  mo * 60 );    // message start time
      clock_params.user_msgs[mi].stop_time  = ( ( uint32_t ) hf * 3600 ) + ( ( uint32_t )  mf * 60 );    // message stop  time
      memcpy ( clock_params.user_msgs[mi].msgs_list, ms, MAX_USR_LNG );
      EEP_millis = millis();
      if ( EEP_millis == 0 ) EEP_millis = 1;
   } else {
      send_ERR();
   }
}

void clr_AMSG ( void ) {
   // Command syntax : CLRAM,MN
   // Clear User defined announcement (played after the standard announcement)
   uint8_t mi;
   //
   mi = atoi ( mySerCmd.ReadNext() );    // message index (0 .. (MAX_USR_MSG - 1))
   if ( mi < ( MAX_USR_MSG - 1 ) ) {
      send_OK();
      clock_params.user_msgs[mi].valid_days = 0;
      clock_params.user_msgs[mi].start_time = 0;
      clock_params.user_msgs[mi].stop_time  = 0;
      memset ( clock_params.user_msgs[mi].msgs_list, 0x00, MAX_USR_LNG );
      EEP_millis = millis();
      if ( EEP_millis == 0 ) EEP_millis = 1;
   } else {
      send_ERR();
   }
}

void get_PARM ( void ) {
   // Command syntax : GETPM
   // Get (print) all parameters
   uint8_t hh, mn;
   //
   hh = mn = 0;
   mySerCmd.Print ( ( char * ) "INFO: All parameters list:" );
   mySerCmd.Print ( ( char * ) " \r\n" );
   //
   mySerCmd.Print ( ( char * ) " - Block ID       :" );
   mySerCmd.Print ( clock_params.blockID );
   mySerCmd.Print ( ( char * ) " \r\n" );
   //
   mySerCmd.Print ( ( char * ) " - Use DST        :" );
   if ( clock_params.use_DST ) mySerCmd.Print ( ( char * ) "true\r\n" );
   else mySerCmd.Print ( ( char * ) "false\r\n" );
   //
   mySerCmd.Print ( ( char * ) " - Dis.bri. day   :" );
   mySerCmd.Print ( clock_params.display_bright );
   mySerCmd.Print ( ( char * ) " \r\n" );
   //
   mySerCmd.Print ( ( char * ) " - Dis.bri. night :" );
   mySerCmd.Print ( clock_params.display_bright_night );
   mySerCmd.Print ( ( char * ) " \r\n" );
   //
   mySerCmd.Print ( ( char * ) " - Period of ann. :" );
   mySerCmd.Print ( clock_params.audio_period );
   mySerCmd.Print ( ( char * ) " \r\n" );
   //
   hh = ( uint8_t ) ( clock_params.audio_on / 3600 );
   mn = ( uint8_t ) ( ( clock_params.audio_on - ( hh * 3600 ) ) / 60 );
   mySerCmd.Print ( ( char * ) " - Audio ON time  :" );
   mySerCmd.Print ( hh );
   mySerCmd.Print ( ',' );
   mySerCmd.Print ( mn );
   mySerCmd.Print ( ( char * ) " \r\n" );
   //
   hh = ( uint8_t ) ( clock_params.audio_off / 3600 );
   mn = ( uint8_t ) ( ( clock_params.audio_off - ( hh * 3600 ) ) / 60 );
   mySerCmd.Print ( ( char * ) " - Audio OFF time :" );
   mySerCmd.Print ( hh );
   mySerCmd.Print ( ',' );
   mySerCmd.Print ( mn );
   mySerCmd.Print ( ( char * ) " \r\n" );
   //
   mySerCmd.Print ( ( char * ) " - Audio off day  :" );
   mySerCmd.Print ( clock_params.audio_off_day );
   mySerCmd.Print ( ( char * ) " \r\n" );
   //
   mySerCmd.Print ( ( char * ) " - Audio volume   :" );
   mySerCmd.Print ( clock_params.audio_volume );
   mySerCmd.Print ( ( char * ) " \r\n" );
   //
   mySerCmd.Print ( ( char * ) " - Audio add day  :" );
   if ( clock_params.audio_add_day ) mySerCmd.Print ( ( char * ) "true\r\n" );
   else mySerCmd.Print ( ( char * ) "false\r\n" );
   //
   mySerCmd.Print ( ( char * ) " - User messages  :" );
   mySerCmd.Print ( ( char * ) " \r\n" );
   for ( uint8_t i = 0; i < MAX_USR_MSG; i++ ) {
      //
      mySerCmd.Print ( ( char * ) "   Msg:" );
      mySerCmd.Print ( i );
      mySerCmd.Print ( ( char * ) " - " );
      mySerCmd.Print ( clock_params.user_msgs[i].valid_days );
      mySerCmd.Print ( ',' );
      hh = ( uint8_t ) ( clock_params.user_msgs[i].start_time / 3600 );
      mn = ( uint8_t ) ( ( clock_params.user_msgs[i].start_time - ( hh * 3600 ) ) / 60 );
      mySerCmd.Print ( hh );
      mySerCmd.Print ( ':' );
      mySerCmd.Print ( mn );
      mySerCmd.Print ( ',' );
      hh = ( uint8_t ) ( clock_params.user_msgs[i].stop_time / 3600 );
      mn = ( uint8_t ) ( ( clock_params.user_msgs[i].stop_time - ( hh * 3600 ) ) / 60 );
      mySerCmd.Print ( hh );
      mySerCmd.Print ( ':' );
      mySerCmd.Print ( mn );
      mySerCmd.Print ( ',' );
      for ( uint8_t j = 0; j < MAX_USR_LNG; j++ ) {
         mySerCmd.Print ( clock_params.user_msgs[i].msgs_list[j] );
         mySerCmd.Print ( ',' );
      }
      mySerCmd.Print ( ( char * ) " \r\n" );
   }
   //
   mySerCmd.Print ( ( char * ) " - EEPROM status  :" );
   if ( EEP_millis == 0 ) {
      mySerCmd.Print ( ( char * ) "saved" );
   } else {
      mySerCmd.Print ( ( char * ) "NOT saved" );
   }
   mySerCmd.Print ( ( char * ) " \r\n" );
   send_OK();
}

void do_RESET ( void ) {
   // Command syntax : RESET
   // Initialize the EEPROM to the default values and then restart the MCU
   strlcpy ( clock_params.blockID, BLOCK_ID, 7 );
   clock_params.use_DST = true;
   clock_params.display_bright = 15;
   clock_params.display_bright_night = 5;
   clock_params.audio_period = INC_15_MIN;
   clock_params.audio_on  = ( ( uint32_t )  9 * 3600 ) + ( ( uint32_t ) 0 * 60 );   // audio ON  at 09:00
   clock_params.audio_off = ( ( uint32_t ) 21 * 3600 ) + ( ( uint32_t ) 0 * 60 );  // audio OFF  at 21:00
   clock_params.audio_off_day = 0;
   clock_params.audio_volume  = 20;
   clock_params.audio_add_day = false;
   for ( uint8_t i = 0; i < MAX_USR_MSG; i++ ) {
      clock_params.user_msgs[i].valid_days = 0;
      clock_params.user_msgs[i].start_time = 0;
      clock_params.user_msgs[i].stop_time  = 0;
      memset ( clock_params.user_msgs[i].msgs_list, 0x00, MAX_USR_LNG );
   }
   save_params();
   //
   delay ( 250 );
#if defined ( ESP_PLATFORM )
   esp_restart();
#elif defined ( ARDUINO_SEEED_XIAO_M0 )
   NVIC_SystemReset();
#endif
   delay ( 100 );
}

/******************************************************************************
   MAIN STUFF
******************************************************************************/

// --------------------- setup --------------------------

void setup() {
   delay ( 500 );
   //
   // Initialize I/O pins
   pinMode ( LED_BUILTIN, OUTPUT );
   digitalWrite ( LED_BUILTIN, led_status );
   //
   pinMode ( JQ8400_BUSY, INPUT );
   //
   // Initialize announcement buffer to 0x00
   memset ( announce_msg, 0x00, sizeof ( announce_msg ) );
#if defined ( ESP_PLATFORM )
   //
   // Initialize the Serial port (open and configure the Serial port) for future use with SerialCmd
   Serial.begin ( 9600 );
   delay ( 50 );
   //
   // Initialize the Serial2 port (open and configure the Serial2 port) for future use with JQ8400
   MySerial.begin ( 9600, SERIAL_8N1, 16, 17 );
   delay ( 50 );
#elif defined ( ARDUINO_SEEED_XIAO_M0 )
   //
   // Initialize the SerialUSB port (open and configure the SerialUSBial port) for future use with SerialCmd
   SerialUSB.begin ( 9600 );
   delay ( 50 );
   //
   // Initialize the Serial1 port (open and configure the Serial1 port) for future use with JQ8400
   Serial1.begin ( 9600 );
   delay ( 50 );
#endif

#if defined ( ESP_PLATFORM ) && defined ( DEBUG )
   //
   // Wait, maximum 5 seconds, for the native USB to be available
   for ( uint8_t i = 0; i < 10; i++ ) {
      if ( !Serial ) {
         delay ( 500 );
         led_status = !led_status;
         digitalWrite ( LED_BUILTIN, led_status );
      } else break;
   }
   //
   //
   mySerCmd.Print ( ( char * ) "INFO: DEBUG flag active, keep serial monitor opened ... \r\n" );
   mySerCmd.Print ( ( char * ) "INFO: Program version v." );
   mySerCmd.Print ( ( char * ) PROG_VER );
   mySerCmd.Print ( ( char * ) "\r\n\r\n" );
#elif defined ( ARDUINO_SEEED_XIAO_M0 ) && defined ( DEBUG )
   //
   // Wait, maximum 5 seconds, for the native USB to be available
   for ( uint8_t i = 0; i < 10; i++ ) {
      if ( !SerialUSB ) {
         delay ( 500 );
         led_status = !led_status;
         digitalWrite ( LED_BUILTIN, led_status );
      } else break;
   }
   //
   //
   mySerCmd.Print ( ( char * ) "INFO: DEBUG flag active, keep serial monitor opened ... \r\n" );
   mySerCmd.Print ( ( char * ) "INFO: Program version v." );
   mySerCmd.Print ( ( char * ) PROG_VER );
   mySerCmd.Print ( ( char * ) "\r\n\r\n" );
#endif

#if defined ( ESP_PLATFORM )
   //
   // Initialize WiFi AP
   WiFi.softAP ( ssid, pswd );
   ESP32_IP = WiFi.softAPIP();
#ifdef DEBUG
   mySerCmd.Print ( ( char * ) "INFO: ESP32 access point IP: " );
   mySerCmd.Print ( ESP32_IP[0] );
   mySerCmd.Print ( '.' );
   mySerCmd.Print ( ESP32_IP[1] );
   mySerCmd.Print ( '.' );
   mySerCmd.Print ( ESP32_IP[2] );
   mySerCmd.Print ( '.' );
   mySerCmd.Print ( ESP32_IP[3] );
   mySerCmd.Print ( ( char * ) "\r\n\r\n" );
#endif                                                 // #endif DEBUG
   //
   // Start WEB server
   server.begin();
   delay ( 50 );
#endif                                                 // #endif ESP_PLATFORM

   //
   // Initialize Wire library to use I2C bus
   Wire.begin();
   delay ( 50 );
   //
   // Verify if DS3231 is present/working
   Wire.beginTransmission ( DS3231_I2C_ADDR );
   delay ( 25 );
   if ( Wire.endTransmission() != 0 ) {
      // DS3231 is NOT present or NOT working ...
#ifdef DEBUG
      mySerCmd.Print ( ( char * ) "ERROR: DS3231 RTC NOT found, program halted." );
#endif
      while ( true ) {
         delay ( 250 );
         led_status = !led_status;
         digitalWrite ( LED_BUILTIN, led_status );
      }
   }
   //
   // Initialize DS3231
   Wire.beginTransmission ( DS3231_I2C_ADDR );
   Wire.write ( 0x0E );    // select register 0x0E
   Wire.write ( 0x1C );    // reset  bit 7 /EOSC
   Wire.endTransmission();
   //
   // Verify if 24C32 EEPROM is present/working
   Wire.beginTransmission ( E24C32_I2C_ADDR );
   delay ( 25 );
   if ( Wire.endTransmission() != 0 ) {
      // 24C32 EEPROM is NOT present or NOT working ...
#ifdef DEBUG
      mySerCmd.Print ( ( char * ) "ERROR: 24C32 EEPROM NOT found, program halted." );
#endif
      while ( true ) {
         delay ( 125 );
         led_status = !led_status;
         digitalWrite ( LED_BUILTIN, led_status );
      }
   }
   //
   // Verify if 7-segment display is present/working
   Wire.beginTransmission ( DISP7S_I2C_ADDR );
   delay ( 25 );
   if ( Wire.endTransmission() != 0 ) {
      // 7-segment display is NOT present or NOT working ...
#ifdef DEBUG
      mySerCmd.Print ( ( char * ) "ERROR: 7-Seg display NOT found, program halted." );
#endif
      while ( true ) {
         delay ( 62 );
         led_status = !led_status;
         digitalWrite ( LED_BUILTIN, led_status );
      }
   }
   //
   // Initialize 7-segment display
   clockDisplay.begin ( DISP7S_I2C_ADDR );
   //
   // Verify if DFR0534 is present/working
   mp3.reset();
   if ( !mp3.sourceAvailable ( MP3_SRC_BUILTIN ) ) {
      // DFR0534 internal flash NON available ...
#ifdef DEBUG
      mySerCmd.Print ( ( char * ) "ERROR: DFR0534 MP3 NOT found, program halted." );
#endif
      while ( true ) {
         delay ( 30 );
         led_status = !led_status;
         digitalWrite ( LED_BUILTIN, led_status );
      }
   }
   //
   // Initialize DFR0534
   mp3.setSource ( MP3_SRC_BUILTIN );
   mp3.setLoopMode ( MP3_LOOP_NONE );
   //
   // Add the commands to SerialCmd ...
   mySerCmd.AddCmd ( "SETDT" , SERIALCMD_FROMALL, set_DTTM );    // set date & time to DS3231
   mySerCmd.AddCmd ( "GETDT" , SERIALCMD_FROMSERIAL, get_DTTM ); // get date & time from tm structure (updated each 15 sec.)
   mySerCmd.AddCmd ( "SETUD" , SERIALCMD_FROMALL, set_UDST );    // set use DST (true or false)
   mySerCmd.AddCmd ( "SETDB" , SERIALCMD_FROMALL, set_DIBT );    // set display brightness (0 .. 15)
   mySerCmd.AddCmd ( "SETPA" , SERIALCMD_FROMALL, set_PERA );    // set period of announcements (10, 15, 30, 60)
   mySerCmd.AddCmd ( "SETON" , SERIALCMD_FROMALL, set_TMON );    // set audio on time
   mySerCmd.AddCmd ( "SETOF" , SERIALCMD_FROMALL, set_TMOF );    // set audio off time
   mySerCmd.AddCmd ( "SETOD" , SERIALCMD_FROMALL, set_ODAY );    // set audio off day (0 .. 7)
   mySerCmd.AddCmd ( "SETAV" , SERIALCMD_FROMALL, set_AVOL );    // set audio volume (0 .. 30)
   mySerCmd.AddCmd ( "SETDY" , SERIALCMD_FROMALL, set_UDAY );    // set adds the date in the time announcement (true or false)
   mySerCmd.AddCmd ( "SETAM" , SERIALCMD_FROMALL, set_AMSG );    // set specific announcement
   mySerCmd.AddCmd ( "CLRAM" , SERIALCMD_FROMALL, clr_AMSG );    // clear specific announcement
   mySerCmd.AddCmd ( "GETPM" , SERIALCMD_FROMSERIAL, get_PARM ); // get all parameters
   mySerCmd.AddCmd ( "RESET" , SERIALCMD_FROMALL, do_RESET );    // do a software reset (reset the board)
   //
   // read the program parameters stored in EEPROM
   read_params();
   //
   // set the audio volume from readed parameters
   mp3.setVolume ( clock_params.audio_volume );
#ifdef DEBUG
   mySerCmd.Print ( ( char * ) "INFO: Audio volume set to: " );
   mySerCmd.Print ( clock_params.audio_volume );
   mySerCmd.Print ( ( char * ) " \r\n\r\n" );
#endif
   //
   // set the display brightnss from readed parameters
   clockDisplay.setBrightness ( clock_params.display_bright );
#ifdef DEBUG
   mySerCmd.Print ( ( char * ) "INFO: Display brightness set to: " );
   mySerCmd.Print ( clock_params.display_bright );
   mySerCmd.Print ( ( char * ) " \r\n\r\n" );
#endif
   //
   // test colon and all 4 digit or 7-segment display
#ifdef DEBUG
   mySerCmd.Print ( ( char * ) "INFO: Testing all display digits ... \r\n\r\n" );
#endif
   clockDisplay.print ( ( char * ) "    " );
   //
   myDrawColon ( 1 );
   clockDisplay.writeDisplay();
   delay ( 750 );
   myDrawColon ( 0 );
   clockDisplay.writeDisplay();
   delay ( 250 );
   //
   for ( uint8_t i = 0; i < 5; i++ ) {
      if ( i != 2 ) {
         clockDisplay.writeDigitAscii ( i, '8', false );
         clockDisplay.writeDisplay();
         delay ( 750 );
         clockDisplay.writeDigitAscii ( i, ' ', false );
         clockDisplay.writeDisplay();
         delay ( 250 );
      }
   }
   //
   // Read the actual date/time stored on DS3231
   read_ds3231();
   //
   // First time read, adjust announce_min for next announce
   announce_hour = theTime.tm_hour;
   if ( clock_params.audio_period == INC_60_MIN ) {
      announce_min = 0;
      announce_hour++;
   } else {
      announce_min = ( ( theTime.tm_min / clock_params.audio_period ) + 1 ) * clock_params.audio_period;
      if ( announce_min > 59 ) {
         announce_min = 0;
         announce_hour++;
      }
   }
#ifdef DEBUG
   mySerCmd.Print ( ( char * ) "INFO: The next announcement will be at time " );
   mySerCmd.Print ( announce_hour );
   mySerCmd.Print ( ':' );
   mySerCmd.Print ( announce_min );
   mySerCmd.Print ( ( char * ) " \r\n\r\n" );
#endif
   //
   //
   announce_idx = 0;
   announce_msg[announce_idx] = CONTROLLO_INIZIALE;                    // controllo iniziale completato con successo, l'orologio viene avviato
   announce_idx++;
   mp3.playSequenceByFileNumber ( announce_msg, announce_idx );
   delay ( 500 );
   while ( digitalRead ( JQ8400_BUSY ) ) {
      delay ( 100 );
   }
}

// --------------------- loop ---------------------------

void loop () {
#if defined ( ESP_PLATFORM )
   char* retVal = NULL;
#endif
   bool    f_dayOK = false;
   //
   //
   // check if the parameters saved in EEPROM need to be updated ...
   //
   if ( EEP_millis != 0 ) {
      //
      // ... YES, but only after EEPROM_W_TIME milliseconds since the last parameter change
      if ( millis() - EEP_millis > EEPROM_W_TIME ) {
         EEP_millis = 0;
         save_params();
#ifdef DEBUG
         mySerCmd.Print ( ( char * ) "INFO: EEPROM parameters updated.  \r\n\r\n" );
#endif
      }
   }
   //
   //
   // each 15 seconds update the time structure from DS3231
   //
   if ( millis() - RTC_millis >= 15000 ) {
      read_ds3231();
      RTC_millis += 15000;
   }
   //
   //
   // if minutes changes, update display values and verify if is time for announce
   if ( last_minute != theTime.tm_min ) {
      last_minute = theTime.tm_min;
      //
      displayValue = ( theTime.tm_hour * 100 ) + theTime.tm_min;
      clockDisplay.print ( displayValue, DEC );
      //
      if ( displayValue < 1000 ) {
         clockDisplay.writeDigitAscii ( 0, '0', false );
         if ( displayValue < 100 ) {
            clockDisplay.writeDigitAscii ( 1, '0', false );
            if ( displayValue < 10 ) {
               clockDisplay.writeDigitAscii ( 3, '0', false );
            }
         }
      }
      //
      // calculate the actual_time ...
      actual_time = ( ( uint32_t ) theTime.tm_hour * 3600 ) + ( ( uint32_t ) theTime.tm_min * 60 );
      //
      // evaluate display brightness
      if ( OnOffTime ( actual_time, clock_params.audio_on, clock_params.audio_off ) ) {
         clockDisplay.setBrightness ( clock_params.display_bright );
      } else {
         clockDisplay.setBrightness ( clock_params.display_bright_night );
      }
      //
      clockDisplay.writeDisplay();
      //
      // ... verify if is time for announce
      if ( ( clock_params.audio_on != clock_params.audio_off ) && ( theTime.tm_hour == announce_hour ) && ( theTime.tm_min == announce_min ) ) {
         //
         // ... calculate if is a speaking day
         f_dayOK = ( theTime.tm_wday != clock_params.audio_off_day );
         //
         // ... and verify if is silent or speaking time ...
         if ( OnOffTime ( actual_time, clock_params.audio_on, clock_params.audio_off ) && f_dayOK ) {
            //
            // ... YES; is speaking time, so ... prepare the standard announcement
            announce_idx = 0;
            announce_msg[announce_idx] = SONO_LE_ORE;                      // it's
            announce_idx++;
            if ( theTime.tm_hour == 0 ) announce_msg[announce_idx] = MEZZANOTTE;  // midnight
            else announce_msg[announce_idx] = theTime.tm_hour;             // hours
            announce_idx++;
            switch ( theTime.tm_min ) {
               case 0:
                  if ( clock_params.audio_add_day )
                     addDateToAnnounce();
                  break;
               case 10:
                  announce_msg[announce_idx] = E_DIECI;                   // ten past
                  announce_idx++;
                  break;
               case 15:
                  if ( actual_time >= 43200 ) {                           // ... from 12 onwards
                     announce_msg[announce_idx] = E_QUINDICI;             // fifteen past
                  } else {
                     announce_msg[announce_idx] = E_UN_QUARTO;            // a quarter past
                  }
                  announce_idx++;
                  break;
               case 20:
                  announce_msg[announce_idx] = E_VENTI;                   // twenty past
                  announce_idx++;
                  break;
               case 30:
                  if ( actual_time >= 43200 ) {                           // ... from 12 onwards
                     announce_msg[announce_idx] = E_TRENTA;               // half past
                  } else {
                     announce_msg[announce_idx] = E_MEZZA;                // half past
                  }
                  announce_idx++;
                  break;
               case 40:
                  announce_msg[announce_idx] = E_QUARANTA;                // forty
                  announce_idx++;
                  break;
               case 45:
                  if ( actual_time >= 43200 ) {                           // ... from 12 onwards
                     announce_msg[announce_idx] = E_QUARANTACINQUE;       // forty-five
                  } else {
                     announce_msg[announce_idx] = E_TRE_QUARTI;           // quarter past
                  }
                  announce_idx++;
                  break;
               case 50:
                  announce_msg[announce_idx] = E_CINQUANTA;               // and fifty
                  announce_idx++;
                  break;
               default:
                  break;
            }
            //
            // ... add specific announcements to standard announcement
            for ( uint8_t i = 0; i < MAX_USR_MSG; i++ ) {
               //
               // Verifiy if the announce is enabled (start time != stop time) ...
               if ( clock_params.user_msgs[i].start_time != clock_params.user_msgs[i].stop_time ) {
                  //
                  // ... then calculate if is a speaking day for the specific announcement ...
                  if ( clock_params.user_msgs[i].valid_days == 0 ) {
                     f_dayOK = true;
                  } else {
                     f_dayOK = ( bitRead ( clock_params.user_msgs[i].valid_days, theTime.tm_wday ) == 1 );
                  }
                  //
#ifdef DEBUG
                  if ( clock_params.user_msgs[i].valid_days > 0 ) {
                     mySerCmd.Print ( ( char * ) "INFO: Personal announcement n. " );
                     mySerCmd.Print ( i );
                     mySerCmd.Print ( ( char * ) " is only valid for: " );
                     for ( k = 1; k < 8; k++ ) {
                        if ( bitRead ( clock_params.user_msgs[i].valid_days, k ) == 1 ) {
                           mySerCmd.Print ( ( char * ) daysNames[k] );
                           mySerCmd.Print ( ( char * ) ", " );
                        }
                     }
                  } else {
                     mySerCmd.Print ( ( char * ) "INFO: Personal announcement n. " );
                     mySerCmd.Print ( i );
                     mySerCmd.Print ( ( char * ) " is valid all days," );
                  }
                  mySerCmd.Print ( ( char * ) " \r\n" );
#endif
                  //
                  // ... then verify also if is the time to add the announcement ...
                  if ( OnOffTime ( actual_time, clock_params.user_msgs[i].start_time, clock_params.user_msgs[i].stop_time ) && f_dayOK ) {
                     //
                     // ... then add all the announcements until find 0x00
                     for ( uint8_t j = 0; j < MAX_USR_LNG; j++ ) {
                        if ( clock_params.user_msgs[i].msgs_list[j] == 0 ) {
                           break;
                        }
                        announce_msg[announce_idx] = clock_params.user_msgs[i].msgs_list[j];
                        announce_idx++;
                     }
                  }
               }
            }
            //
#ifdef DEBUG
            mySerCmd.Print ( ( char * ) "INFO: Announcement sequence is: " );
            for ( uint8_t i = 0; i < announce_idx; i++ ) {
               mySerCmd.Print ( announce_msg[i] );
               mySerCmd.Print ( ',' );
            }
            mySerCmd.Print ( ( char * ) " \r\n" );
#endif
            //
            // ... output the announce using the audio speaker
            mp3.playSequenceByFileNumber ( announce_msg, announce_idx );
         } else {
            //
            // ... NO; is silent time, so ... do nothing
#ifdef DEBUG
            mySerCmd.Print ( ( char * ) "INFO: Announcement silenced." );
            mySerCmd.Print ( ( char * ) " \r\n" );
#endif
         }
         //
         // adjust hours and minutes for the next announcement
         if ( clock_params.audio_period == INC_60_MIN ) {
            announce_min  = 0;
            announce_hour++;
            if ( announce_hour > 23 ) announce_hour = 0;
         } else {
            announce_min += clock_params.audio_period;
            if ( announce_min == 60 ) {
               announce_min = 0;
               announce_hour++;
               if ( announce_hour > 23 ) announce_hour = 0;
            }
         }
#ifdef DEBUG
         mySerCmd.Print ( ( char * ) "INFO: The next announcement will be at time " );
         mySerCmd.Print ( announce_hour );
         mySerCmd.Print ( ':' );
         mySerCmd.Print ( announce_min );
         mySerCmd.Print ( ( char * ) " \r\n\r\n" );
#endif
      }
   }
   //
   //
   // each second change on-board led status and blink display dots
   //
   if ( millis() - LED_millis >= 1000 ) {
      led_status = !led_status;
      digitalWrite ( LED_BUILTIN, led_status );
      //
      dsp_status = !dsp_status;
      myDrawColon ( dsp_status );
      clockDisplay.writeDisplay();
      LED_millis += 1000;
   }
   //
   //
   // Initialize fCmdFromUSB flag before checking for command from USB or from HTTP (on ESP32)
   //
   fCmdFromUSB = true;
   //
   //
   // verify if there is a serial message
   //
   mySerCmd.ReadSer();
   //
#if defined ( ESP_PLATFORM )
   //
   // verify if there is an HTTP message
   //
   retVal = checkWiFiCommand ();
   if ( retVal != NULL ) {
#ifdef DEBUG
      mySerCmd.Print ( ( char * ) "INFO: Command received by HTML: " );
      mySerCmd.Print ( retVal );
      mySerCmd.Print ( ( char * ) " \r\n\r\n" );
#endif                                                 // #endif DEBUG
      fCmdFromUSB = false;
      mySerCmd.ReadString ( retVal );
   }
#endif                                                 // #endif ESP_PLATFORM   
   //
#if defined ( ARDUINO_SEEED_XIAO_M0 )
   //
   // reset the watchdog timer
   //
   wdt_reset();
#endif

}
