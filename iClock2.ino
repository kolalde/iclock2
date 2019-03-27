/*
 *
 * GPIO Pins Used
 * 0    WS2812
 * 1    TX0
 * 2    DS1820
 * 3    RX0   
 * 4    SDA   Button to display weather
 * 5    SCL   ?
 * 12   MAX7219
 * 13   MAX7219
 * 14   MAX7219
 * 15   GND    (was load for MAX, unstable, READ)
 * 16   
 * ADC  LDR
 * */

#include <ESP8266WiFi.h>                // in general
#include <sys/time.h>                   // struct timeval
#include <coredecls.h>                  // settimeofday_cb()
#include <MD_MAX72xx.h>                 // MAX7912
#include <SPI.h>
#include <pgmspace.h>                   // strings in flash to save on ram
#include <WiFiManager.h>                // captive portal for WiFi setup
#include <FastLED.h>                    // controlling WS2812s
#include <DallasTemperature.h>          // DS1820 temp sensor
#include <PubSubClient.h>               // MQTT
#include <ESP8266WebServer.h>           // For controlling clock
#include <ESP8266mDNS.h>                // For WebServer
#include <WiFiUdp.h>                    // For OTA
#include <ArduinoOTA.h>                 //
#include <OpenWeatherMap.h>             // external weather
#include <TimeLib.h>                    // used for hours (I think)
#include <Ticker.h>                     // Tickers to contol display and weather flags
#include <ESP8266HTTPClient.h>          // local driven OTA updates
#include <ESP8266httpUpdate.h>          //
#include "ThingSpeak.h"                 // Simple interface now


const char TSauth[] = "51165f4f92fa4c148bb43179b0347a9e";
const char TSWriteApiKey[] = "KPNEEM5L2OT999MA";  
const char nodeName[] = "iClock2";
const char awakeString[] = "ko_house/sensor/iClock2/awake";
const char connectString[] = "ko_house/sensor/iClock2/connect";
const char tempTopic[] = "ko_house/sensor/iClock2/board_temp";
const char inTopic[] = "ko_house/sensor/iClock2/inTopic";
const char* mqtt_server = "korpi.local";
const int  tempPubInterval = 30;
const char *ow_key      = "29317aa62133de8ffea08f388df205d4";
const char *servername = "api.openweathermap.org";
OWMconditions      owCC;
OWMfiveForecast    owF5;
OWMoneForecast    owF1;
Ticker getOWM;
Ticker displayTempTicker;
bool getOWMNow = true;
const int displayWeatherPin = 0;
WiFiClient  tsClient;


// Web variables  (switch from String the char*  and save space? )
String displayText = "Enter text here";
int displayTextTimes = 0;
String textOn = "time";
String intensity = "0"; 
String ledStrip_intensity = "10"; 
String sColor = "Purple";
char auto_brightness[5] = "On";
bool printTempFlag = false;

// Other runtime stuff
// for testing purpose:
extern "C" int clock_gettime(clockid_t unused, struct timespec *tp);
timeval tv;
struct timezone tz;
timespec tp;
time_t tnow;
char timeBuf[100];

char bootTime[30];

// Used to display the temp for 3 seconds
void printTempCountDown() {
  printTempFlag = false;
}

bool toggleOWM() {
  getOWMNow = getOWMNow ? false : true;
  return getOWMNow;
}

bool displayWeatherButton() {
  int buttonState = 0;

  buttonState = digitalRead(displayWeatherPin);
  if (buttonState == HIGH) {
    return false;
  } else {
    return true;
  }
}

////////////////////////////////////////////////////////
//
// OTA routines
//
WiFiClient OTAclient;

void setupOTA() {
  // The line below is optional. It can be used to blink the LED on the board during flashing
  // The LED will be on during download of one buffer of data from the network. The LED will
  // be off during writing that buffer to flash
  // On a good connection the LED should flash regularly. On a bad connection the LED will be
  // on much longer than it will be off. Other pins than LED_BUILTIN may be used. The second
  // value is used to put the LED on. If the LED is on with HIGH, that value should be passed
  ESPhttpUpdate.setLedPin(15, HIGH);

  ArduinoOTA.setHostname(nodeName);

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }
  
    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
    printString("OTA");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    char otaBuff[10];
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    sprintf( otaBuff, "%u%%", (progress / (total / 100)));
    printString( otaBuff );
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
}

////////////////////////////////////////////////////////
//
// LedMatrix defines for MAX7912s
//
// Turn on debug statements to the serial output
#define  DEBUG  1
#if  DEBUG
#define PRINT(s, x) { Serial.print(F(s)); Serial.print(x); }
#define PRINTS(x) Serial.print(F(x))
#define PRINTD(x) Serial.println(x, DEC)
#else
#define PRINT(s, x)
#define PRINTS(x)
#define PRINTD(x)
#endif

#define HARDWARE_TYPE MD_MAX72XX::GENERIC_HW
#define MAX_DEVICES 4

#define CLK_PIN   14  // or SCK
#define DATA_PIN  13  // or MOSI
#define CS_PIN    12  // or SS
// SPI hardware interface
MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);


////////////////////////////////////////////////////////
//
// FastLED setup for WS2812s
//
#define LED_PIN     5
#define NUM_LEDS    47
#define BRIGHTNESS  25
#define LED_TYPE    WS2812
#define COLOR_ORDER GRB
CRGB leds[NUM_LEDS];


////////////////////////////////////////////////////////
//
// WIFI
//
// WiFi setup   (SSID & PWD not really used. Captive portal instead)
//#define SSID            "Arcadia"
//#define SSIDPWD         "204Molly"

// Initial wifi setup, portal
void setupWifi() {
  // turn on WIFI using stored SSID/PWD or captive portal
  printString("WiFi");
  WiFiManager wifiManager;
  wifiManager.autoConnect( nodeName );
  Serial.println("\nWiFi connected");
  Serial.print("Connected: "); Serial.println(WiFi.SSID());    
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  printStringWithShift( (char*)WiFi.SSID().c_str(), 50 );
  printStringWithShift( (char*)F("       "), 50 );
}


////////////////////////////////////////////////////////
//
// PubSub   (MQTT)
//
WiFiClient espClient;
PubSubClient client(espClient);

void setupMQTT() {
//  client.setServer(server, 1883);
//  client.setCallback(callback);
  pubSub_connect_yield();
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // Quick message for testing
  if ((char)payload[0] == '1') {
    printString( "Got 1" ); 
    delay(100);
  } 
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = nodeName;
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish(connectString, "reconnected");
      // ... and resubscribe
      client.subscribe(inTopic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in .5 seconds");
      delay(500);
    }
  }
}

void pubSub_connect_yield() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
}


////////////////////////////////////////////////////////
//
// NTP and Time setup
//
timeval cbtime;                          // when time set callback was called
int cbtime_set = 0;

// callback function that is called whenever time is set
// could potentially be used to update RTC
void time_is_set (void)
{
  gettimeofday(&cbtime, NULL);
  cbtime_set++;
  Serial.println("------------------ settimeofday() was called ------------------");
}


////////////////////////////////////////////////////////
//
// LDR   (light and dark indicator)
//
#define sensorPin     A0    // select the input pin for ldr
#define lightThresh   700
int sensorValue = 0;        // variable to store the value coming from the sensor

bool isLight() {
  // read the value from the sensor:
  sensorValue = analogRead(sensorPin);    
  Serial.println(sensorValue); //prints the values coming from the sensor on the screen
  
  if(sensorValue < lightThresh-25)          //setting a threshold value
  {
    Serial.println("It is light");
    return true;
  }
  if(sensorValue > lightThresh+25)          //setting a threshold value
  {
    Serial.println("It is dark");
    return false;
  }
}


////////////////////////////////////////////////////////
//
// DS1820
//
// Data wire is plugged into port 2 on the Arduino
#define ONE_WIRE_BUS 2

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
DeviceAddress insideThermometer = { 0x10, 0x5A, 0xEA, 0x47, 0x00, 0x08, 0x00, 0x6D };

// function to print a device address
void printAddress(DeviceAddress deviceAddress)
{
  for (uint8_t i = 0; i < 8; i++)
  {
    if (deviceAddress[i] < 16) Serial.print("0");
    Serial.print(deviceAddress[i], HEX);
  }
}

// function to print the temperature for a device
void getTemperature(DeviceAddress deviceAddress, char* buff)
{
  float tempC = round( sensors.getTempC(deviceAddress) );
  Serial.print( F("Temp C: ") );
  Serial.print( tempC );
  Serial.print( F(" Temp F: ") );
  Serial.println(DallasTemperature::toFahrenheit(tempC)); // Converts tempC to Fahrenheit
  buff =  ftoa(buff, round(DallasTemperature::toFahrenheit(tempC)), 0);
}

////////////////////////////////////////////////////////
//
// Web Server
//
ESP8266WebServer wServer(80);
void webServerSetup() {
  if (!MDNS.begin( nodeName )) {
    Serial.println("Error setting up MDNS responder!");
    while(1) { 
      delay(1000);
    }
  }
  Serial.println("mDNS responder started");
  printString("mDNS");

  // Set up the endpoints for HTTP server,  Endpoints can be written as inline functions:
  wServer.on("/", []()
  {
    wServer.send(200, "text/html", setForm() );
  });
  wServer.on("/admin", []()
  {
    wServer.send(200, "text/html", setAdmin() );
  });
  wServer.on("/msgAdmin", handle_msgAdmin);                          // And as regular external functions:
  wServer.on("/msg", handle_msg);                          // And as regular external functions:
  wServer.begin();                                         // Start the server
  // Add service to MDNS-SD
  MDNS.addService("http", "tcp", 80);
  delay(200);
}  


////////////////////////////////////////////////////////
//
// SETUP
//
void setup() {
  const char compile_date[] = __DATE__ " " __TIME__ " " __FILE__;
    Serial.begin(115200); delay(2000);
  Serial.print("\n***** ");
  Serial.print( nodeName );
  Serial.println(" *****");
  Serial.println( compile_date );
  
  Serial.println("---------- Init and clear the MAX7912 -----------");
  initMDLib();
  
  Serial.println("----------- Init and clear the WS2812s ----------");
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection( TypicalLEDStrip );
  setLedColor("Purple", BRIGHTNESS);

  Serial.println("------------------ Init WiFi --------------------");
  setupWifi();

  Serial.println("------------------ Init MQTT --------------------");
  //setupMQTT();
  printString( (char*) F("MQTT") );
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  reconnect();
  client.publish(awakeString, "awake");
  delay( 200 );
  
  Serial.println("------------ Init NTP, TZ, and DST -------------");
  // set function to call when time is set
  // is called by NTP code when NTP is used
  printString( (char*) F("Time") );
  settimeofday_cb(time_is_set);

  time_t rtc_time_t = 1541267183;         // fake RTC time for now and TZ
  timezone tz = { 0, 0};
  timeval tv = { rtc_time_t, 0};      
  settimeofday(&tv, &tz);
  // TZ string information:
  // https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html
  setenv("TZ", "EST+5EDT,M3.2.0/2,M11.1.0/2", 1);
  tzset();                                // save the TZ variable
  
  // set both timezone offet and dst parameters to zero 
  // and get real timezone & DST support by using a TZ string
  configTime(0, 0, "pool.ntp.org");
  delay( 200 );

  Serial.println("--------------- Init and DS1820 ----------------");
  // Check for and get indoor temp, DS1820
  printString( (char*) F("Temp") );
  sensors.begin();
  if (!sensors.getAddress(insideThermometer, 0))
  {
    printString( (char*) F("No Temp!") );
    Serial.println("Unable to find address for Device 0"); 
    //ESP.reset();
  }
  delay( 200 );
  
  Serial.println("------------------ WebServer ------------------");
  printString( (char*) F("HTTP") );
  delay( 200 );
  webServerSetup();
  
  Serial.println("--------------------- OTA ---------------------");
  printString( (char*) F("OTA") );
  delay( 200 );
  setupOTA();

  Serial.println("------------------ Setup getOWM ---------------");
  Serial.println("Current Conditions: ");
  
  printString( (char*) F("OWM") );
  delay( 200 );
  currentConditions();
  Serial.println("One day forecast: ");
  oneDayFcast();      

  getOWMNow = false;
  getOWM.attach( 60, toggleOWM );

  // wait for settimeofday() to be called
  int x = 1; char printBuf[10];
  while ( cbtime_set < 2 ) {
    Serial.println("Waiting for settimeofday() to be called");
    sprintf(printBuf, "T%d", x++);
    printString(printBuf);
    delay(500);
    if ( x > 20 )             // This will eventually settle, this is quicker
      ESP.reset();
  }

  // Grab and stash the bootTime   what's really needed here?
  gettimeofday(&tv, &tz);
  clock_gettime(0, &tp); // also supported by esp8266 code
  tnow = time(nullptr);
  struct tm * timeinfo;

  timeinfo = localtime (&tnow);  
  strcpy( bootTime, asctime(localtime(&tnow)) );

  // When pushed
  pinMode( displayWeatherPin, INPUT );

  Serial.println("------------------ ThingSpeak ------------------");
  ThingSpeak.begin(tsClient);  // Initialize ThingSpeak


  Serial.println("------------------ All Setup ------------------");
  printStringWithShift("iClock2       ", 50);
  delay(200);
}




////////////////////////////////////////////////////////
////////////////////////////////////////////////////////
//
// LOOP
//
////////////////////////////////////////////////////////
////////////////////////////////////////////////////////
void loop()
{
  gettimeofday(&tv, &tz);
  clock_gettime(0, &tp); // also supported by esp8266 code
  tnow = time(nullptr);
  struct tm * timeinfo;
  timeinfo = localtime (&tnow);


  // Check for a button press, display external weather in a blocking call
  if ( displayWeatherButton( ) ) {
    printStringWithShift( displayOutsideWeather( timeBuf ), 50 );
    printStringWithShift( displayOutsideForecast( timeBuf ), 50 );
    printStringWithShift( "         ", 50 );
  }
  
  // localtime / gmtime every second change
  static time_t lastv = 0;
  if (lastv != tv.tv_sec && textOn.equals("time"))
  {
    lastv = tv.tv_sec;

    //
    // Get and print the time
    //
    Serial.println();
    printf(" local asctime: %s", asctime(localtime(&tnow))); // print formated local time

    // Usually display the time, every tempPubInterval display the temp for 3s 
    if  ( !printTempFlag ) {
      //sprintf( timeBuf, "%2d:%02d:%02d", hourFormat12(), minute(), second() );
      strftime (timeBuf,10,"%I:%M:%S",timeinfo);
      if ( *timeBuf == '0' ) 
        *timeBuf = ' ';         // replace leading 0 with a space
      Serial.println(timeBuf);
      printString(timeBuf);
    }
    
    //
    // Adjust display for brightness
    //
    if ( strcmp(auto_brightness, "On") == 0 ) {
      if ( isLight() ) 
        mx.control(MD_MAX72XX::INTENSITY, 15);
      else
        mx.control(MD_MAX72XX::INTENSITY, 0);
    }

    //
    // Get and print the indoor temp every tempPubInterval seconds
    //
    if ( tv.tv_sec % tempPubInterval == 0 ) 
    {
      Serial.println(F("Requesting temperatures..."));
      sensors.requestTemperatures(); // Send the command to get temperatures
      Serial.println(F("DONE"));
      
      // It responds almost immediately. Let's print out the data
      getTemperature( insideThermometer, timeBuf ); // Use a simple function to print out the data
      client.publish(tempTopic, timeBuf);
      strcat( timeBuf, "^F" );
      printString(timeBuf);
      printTempFlag = true;
      displayTempTicker.attach( 3, printTempCountDown );

      //
      //   Push to ThingSpeak if we're NOT getting OWM
      //
      if ( !getOWMNow ) {
        // Write to ThingSpeak. There are up to 8 fields in a channel, allowing you to store up to 8 different
        // pieces of information in a channel.  Here, we write to field 1.
        int x = ThingSpeak.writeField(77958, 1, timeBuf, TSWriteApiKey);
        if(x == 200){
          Serial.println("Channel update successful.");
        }
        else{
          Serial.println("Problem updating channel. HTTP error code " + String(x));
        }
      } else {
        //
        //   Get the weather from OpenWeatherMap.org  (every minute?)
        //
          Serial.println("Current Conditions: ");
          currentConditions();
          Serial.println("One day forecast: ");
          oneDayFcast();          
          getOWMNow = false;
      }
    }
    Serial.printf("heap size: %u\n", ESP.getFreeHeap());  
  }
  
  // Service MQTT often, but after the timed work
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  wServer.handleClient();                    // checks for incoming HTTP messages
  MDNS.update();
  ArduinoOTA.handle();

  // Display text if requested
  if ( textOn.equals("text") && displayTextTimes-- ) {
    printStringWithShift( (char*)displayText.c_str(), 50 );  // TODO Make non-blocking
    printStringWithShift( "   ", 50 );
  } else
    textOn = "time";

}



const uint8_t PROGMEM _sys_var_single[] = 
{
'F', 1, 1, 255, 8,
  5,0x3e,0x5b,0x4f,0x5b,0x3e, // 1 - 'Sad Smiley'
  5,0x3e,0x6b,0x4f,0x6b,0x3e, // 2 - 'Happy Smiley'
  5,0x1c,0x3e,0x7c,0x3e,0x1c, // 3 - 'Heart'
  5,0x18,0x3c,0x7e,0x3c,0x18, // 4 - 'Diamond'
  5,0x1c,0x57,0x7d,0x57,0x1c, // 5 - 'Clubs'
  5,0x1c,0x5e,0x7f,0x5e,0x1c, // 6 - 'Spades'
  4,0x00,0x18,0x3c,0x18,  // 7 - 'Bullet Point'
  5,0xff,0xe7,0xc3,0xe7,0xff, // 8 - 'Rev Bullet Point'
  4,0x00,0x18,0x24,0x18,  // 9 - 'Hollow Bullet Point'
  5,0xff,0xe7,0xdb,0xe7,0xff, // 10 - 'Rev Hollow BP'
  5,0x30,0x48,0x3a,0x06,0x0e, // 11 - 'Male'
  5,0x26,0x29,0x79,0x29,0x26, // 12 - 'Female'
  5,0x40,0x7f,0x05,0x05,0x07, // 13 - 'Music Note 1'
  5,0x40,0x7f,0x05,0x25,0x3f, // 14 - 'Music Note 2'
  5,0x5a,0x3c,0xe7,0x3c,0x5a, // 15 - 'Snowflake'
  5,0x7f,0x3e,0x1c,0x1c,0x08, // 16 - 'Right Pointer'
  5,0x08,0x1c,0x1c,0x3e,0x7f, // 17 - 'Left Pointer'
  5,0x14,0x22,0x7f,0x22,0x14, // 18 - 'UpDown Arrows'
  5,0x5f,0x5f,0x00,0x5f,0x5f, // 19 - 'Double Exclamation'
  5,0x06,0x09,0x7f,0x01,0x7f, // 20 - 'Paragraph Mark'
  4,0x66,0x89,0x95,0x6a,  // 21 - 'Section Mark'
  5,0x60,0x60,0x60,0x60,0x60, // 22 - 'Double Underline'
  5,0x94,0xa2,0xff,0xa2,0x94, // 23 - 'UpDown Underlined'
  5,0x08,0x04,0x7e,0x04,0x08, // 24 - 'Up Arrow'
  5,0x10,0x20,0x7e,0x20,0x10, // 25 - 'Down Arrow'
  5,0x08,0x08,0x2a,0x1c,0x08, // 26 - 'Right Arrow'
  5,0x08,0x1c,0x2a,0x08,0x08, // 27 - 'Left Arrow'
  5,0x1e,0x10,0x10,0x10,0x10, // 28 - 'Angled'
  5,0x0c,0x1e,0x0c,0x1e,0x0c, // 29 - 'Squashed #'
  5,0x30,0x38,0x3e,0x38,0x30, // 30 - 'Up Pointer'
  5,0x06,0x0e,0x3e,0x0e,0x06, // 31 - 'Down Pointer'
  2,0x00,0x00,  // 32 - 'Space'
  1,0x5f, // 33 - '!'
  3,0x07,0x00,0x07, // 34 - '"'
  5,0x14,0x7f,0x14,0x7f,0x14, // 35 - '#'
  5,0x24,0x2a,0x7f,0x2a,0x12, // 36 - '$'
  5,0x23,0x13,0x08,0x64,0x62, // 37 - '%'
  5,0x36,0x49,0x56,0x20,0x50, // 38 - '&'
  3,0x08,0x07,0x03, // 39 - '''
  3,0x1c,0x22,0x41, // 40 - '('
  3,0x41,0x22,0x1c, // 41 - ')'
  5,0x2a,0x1c,0x7f,0x1c,0x2a, // 42 - '*'
  5,0x08,0x08,0x3e,0x08,0x08, // 43 - '+'
  3,0x80,0x70,0x30, // 44 - ','
  5,0x08,0x08,0x08,0x08,0x08, // 45 - '-'
  2,0x60,0x60,  // 46 - '.'
  5,0x20,0x10,0x08,0x04,0x02, // 47 - '/'
  4,0x3e,0x41,0x41,0x3e,  // 48 - '0'
  3,0x42,0x7f,0x40, // 49 - '1'
  4,0x72,0x49,0x49,0x46,  // 50 - '2'
  4,0x21,0x41,0x4d,0x33,  // 51 - '3'
  4,0x1c,0x12,0x11,0x7f,  // 52 - '4'
  4,0x27,0x45,0x45,0x39,  // 53 - '5'
  4,0x3c,0x4a,0x49,0x31,  // 54 - '6'
  4,0x61,0x11,0x09,0x07,  // 55 - '7'
  4,0x36,0x49,0x49,0x36,  // 56 - '8'
  4,0x46,0x49,0x29,0x1e,  // 57 - '9'
  1,0x14, // 58 - ':'
  2,0x80,0x68,  // 59 - ';'
  4,0x08,0x14,0x22,0x41,  // 60 - '<'
  4,0x14,0x14,0x14,0x14,  // 61 - '='
  4,0x41,0x22,0x14,0x08,  // 62 - '>'
  4,0x02,0x59,0x09,0x06,  // 63 - '?'
  4,0x3e,0x41,0x59,0x4e,  // 64 - '@'
  5,0x7c,0x12,0x11,0x12,0x7c, // 65 - 'A'
  5,0x7f,0x49,0x49,0x49,0x36, // 66 - 'B'
  5,0x3e,0x41,0x41,0x41,0x22, // 67 - 'C'
  5,0x7f,0x41,0x41,0x41,0x3e, // 68 - 'D'
  5,0x7f,0x49,0x49,0x49,0x41, // 69 - 'E'
  5,0x7f,0x09,0x09,0x09,0x01, // 70 - 'F'
  5,0x3e,0x41,0x41,0x51,0x73, // 71 - 'G'
  5,0x7f,0x08,0x08,0x08,0x7f, // 72 - 'H'
  3,0x41,0x7f,0x41, // 73 - 'I'
  5,0x20,0x40,0x41,0x3f,0x01, // 74 - 'J'
  5,0x7f,0x08,0x14,0x22,0x41, // 75 - 'K'
  5,0x7f,0x40,0x40,0x40,0x40, // 76 - 'L'
  5,0x7f,0x02,0x1c,0x02,0x7f, // 77 - 'M'
  5,0x7f,0x04,0x08,0x10,0x7f, // 78 - 'N'
  5,0x3e,0x41,0x41,0x41,0x3e, // 79 - 'O'
  5,0x7f,0x09,0x09,0x09,0x06, // 80 - 'P'
  5,0x3e,0x41,0x51,0x21,0x5e, // 81 - 'Q'
  5,0x7f,0x09,0x19,0x29,0x46, // 82 - 'R'
  5,0x26,0x49,0x49,0x49,0x32, // 83 - 'S'
  5,0x03,0x01,0x7f,0x01,0x03, // 84 - 'T'
  5,0x3f,0x40,0x40,0x40,0x3f, // 85 - 'U'
  5,0x1f,0x20,0x40,0x20,0x1f, // 86 - 'V'
  5,0x3f,0x40,0x38,0x40,0x3f, // 87 - 'W'
  5,0x63,0x14,0x08,0x14,0x63, // 88 - 'X'
  5,0x03,0x04,0x78,0x04,0x03, // 89 - 'Y'
  5,0x61,0x59,0x49,0x4d,0x43, // 90 - 'Z'
  3,0x7f,0x41,0x41, // 91 - '['
  5,0x02,0x04,0x08,0x10,0x20, // 92 - '\'
  3,0x41,0x41,0x7f, // 93 - ']'
  5,0x04,0x02,0x01,0x02,0x04, // 94 - '^'
  5,0x40,0x40,0x40,0x40,0x40, // 95 - '_'
  3,0x03,0x07,0x08, // 96 - '`'
  5,0x20,0x54,0x54,0x78,0x40, // 97 - 'a'
  5,0x7f,0x28,0x44,0x44,0x38, // 98 - 'b'
  5,0x38,0x44,0x44,0x44,0x28, // 99 - 'c'
  5,0x38,0x44,0x44,0x28,0x7f, // 100 - 'd'
  5,0x38,0x54,0x54,0x54,0x18, // 101 - 'e'
  4,0x08,0x7e,0x09,0x02,  // 102 - 'f'
  5,0x18,0xa4,0xa4,0x9c,0x78, // 103 - 'g'
  5,0x7f,0x08,0x04,0x04,0x78, // 104 - 'h'
  3,0x44,0x7d,0x40, // 105 - 'i'
  4,0x40,0x80,0x80,0x7a,  // 106 - 'j'
  4,0x7f,0x10,0x28,0x44,  // 107 - 'k'
  3,0x41,0x7f,0x40, // 108 - 'l'
  5,0x7c,0x04,0x78,0x04,0x78, // 109 - 'm'
  5,0x7c,0x08,0x04,0x04,0x78, // 110 - 'n'
  5,0x38,0x44,0x44,0x44,0x38, // 111 - 'o'
  5,0xfc,0x18,0x24,0x24,0x18, // 112 - 'p'
  5,0x18,0x24,0x24,0x18,0xfc, // 113 - 'q'
  5,0x7c,0x08,0x04,0x04,0x08, // 114 - 'r'
  5,0x48,0x54,0x54,0x54,0x24, // 115 - 's'
  4,0x04,0x3f,0x44,0x24,  // 116 - 't'
  5,0x3c,0x40,0x40,0x20,0x7c, // 117 - 'u'
  5,0x1c,0x20,0x40,0x20,0x1c, // 118 - 'v'
  5,0x3c,0x40,0x30,0x40,0x3c, // 119 - 'w'
  5,0x44,0x28,0x10,0x28,0x44, // 120 - 'x'
  5,0x4c,0x90,0x90,0x90,0x7c, // 121 - 'y'
  5,0x44,0x64,0x54,0x4c,0x44, // 122 - 'z'
  3,0x08,0x36,0x41, // 123 - '{'
  1,0x77, // 124 - '|'
  3,0x41,0x36,0x08, // 125 - '}'
  5,0x02,0x01,0x02,0x04,0x02, // 126 - '~'
  5,0x3c,0x26,0x23,0x26,0x3c, // 127 - 'Hollow Up Arrow'
  5,0x1e,0xa1,0xa1,0x61,0x12, // 128 - 'C sedilla'
  5,0x38,0x42,0x40,0x22,0x78, // 129 - 'u umlaut'
  5,0x38,0x54,0x54,0x55,0x59, // 130 - 'e acute'
  5,0x21,0x55,0x55,0x79,0x41, // 131 - 'a accent'
  5,0x21,0x54,0x54,0x78,0x41, // 132 - 'a umlaut'
  5,0x21,0x55,0x54,0x78,0x40, // 133 - 'a grave'
  5,0x20,0x54,0x55,0x79,0x40, // 134 - 'a acute'
  5,0x18,0x3c,0xa4,0xe4,0x24, // 135 - 'c sedilla'
  5,0x39,0x55,0x55,0x55,0x59, // 136 - 'e accent'
  5,0x38,0x55,0x54,0x55,0x58, // 137 - 'e umlaut'
  5,0x39,0x55,0x54,0x54,0x58, // 138 - 'e grave'
  3,0x45,0x7c,0x41, // 139 - 'i umlaut'
  4,0x02,0x45,0x7d,0x42,  // 140 - 'i hat'
  4,0x01,0x45,0x7c,0x40,  // 141 - 'i grave'
  5,0xf0,0x29,0x24,0x29,0xf0, // 142 - 'A umlaut'
  5,0xf0,0x28,0x25,0x28,0xf0, // 143 - 'A dot'
  4,0x7c,0x54,0x55,0x45,  // 144 - 'E grave'
  7,0x20,0x54,0x54,0x7c,0x54,0x54,0x08, // 145 - 'ae'
  6,0x7c,0x0a,0x09,0x7f,0x49,0x49,  // 146 - 'AE'
  5,0x32,0x49,0x49,0x49,0x32, // 147 - 'o hat'
  5,0x30,0x4a,0x48,0x4a,0x30, // 148 - 'o umlaut'
  5,0x32,0x4a,0x48,0x48,0x30, // 149 - 'o grave'
  5,0x3a,0x41,0x41,0x21,0x7a, // 150 - 'u hat'
  5,0x3a,0x42,0x40,0x20,0x78, // 151 - 'u grave'
  4,0x9d,0xa0,0xa0,0x7d,  // 152 - 'y umlaut'
  5,0x38,0x45,0x44,0x45,0x38, // 153 - 'O umlaut'
  5,0x3c,0x41,0x40,0x41,0x3c, // 154 - 'U umlaut'
  5,0x3c,0x24,0xff,0x24,0x24, // 155 - 'Cents'
  5,0x48,0x7e,0x49,0x43,0x66, // 156 - 'Pounds'
  5,0x2b,0x2f,0xfc,0x2f,0x2b, // 157 - 'Yen'
  5,0xff,0x09,0x29,0xf6,0x20, // 158 - 'R +'
  5,0xc0,0x88,0x7e,0x09,0x03, // 159 - 'f notation'
  5,0x20,0x54,0x54,0x79,0x41, // 160 - 'a acute'
  3,0x44,0x7d,0x41, // 161 - 'i acute'
  5,0x30,0x48,0x48,0x4a,0x32, // 162 - 'o acute'
  5,0x38,0x40,0x40,0x22,0x7a, // 163 - 'u acute'
  4,0x7a,0x0a,0x0a,0x72,  // 164 - 'n accent'
  5,0x7d,0x0d,0x19,0x31,0x7d, // 165 - 'N accent'
  5,0x26,0x29,0x29,0x2f,0x28, // 166
  5,0x26,0x29,0x29,0x29,0x26, // 167
  5,0x30,0x48,0x4d,0x40,0x20, // 168 - 'Inverted ?'
  5,0x38,0x08,0x08,0x08,0x08, // 169 - 'LH top corner'
  5,0x08,0x08,0x08,0x08,0x38, // 170 - 'RH top corner'
  5,0x2f,0x10,0xc8,0xac,0xba, // 171 - '1/2'
  5,0x2f,0x10,0x28,0x34,0xfa, // 172 - '1/4'
  1,0x7b, // 173 - '| split'
  5,0x08,0x14,0x2a,0x14,0x22, // 174 - '<<'
  5,0x22,0x14,0x2a,0x14,0x08, // 175 - '>>'
  5,0xaa,0x00,0x55,0x00,0xaa, // 176 - '30% shading'
  5,0xaa,0x55,0xaa,0x55,0xaa, // 177 - '50% shading'
  5,0x00,0x00,0x00,0x00,0xff, // 178 - 'Right side'
  5,0x10,0x10,0x10,0x10,0xff, // 179 - 'Right T'
  5,0x14,0x14,0x14,0x14,0xff, // 180 - 'Right T double H'
  5,0x10,0x10,0xff,0x00,0xff, // 181 - 'Right T double V'
  5,0x10,0x10,0xf0,0x10,0xf0, // 182 - 'Top Right double V'
  5,0x14,0x14,0x14,0x14,0xfc, // 183 - 'Top Right double H'
  5,0x14,0x14,0xf7,0x00,0xff, // 184 - 'Right T double all'
  5,0x00,0x00,0xff,0x00,0xff, // 185 - 'Right side double'
  5,0x14,0x14,0xf4,0x04,0xfc, // 186 - 'Top Right double'
  5,0x14,0x14,0x17,0x10,0x1f, // 187 - 'Bot Right double'
  5,0x10,0x10,0x1f,0x10,0x1f, // 188 - 'Bot Right double V'
  5,0x14,0x14,0x14,0x14,0x1f, // 189 - 'Bot Right double H'
  5,0x10,0x10,0x10,0x10,0xf0, // 190 - 'Top Right'
  5,0x00,0x00,0x00,0x1f,0x10, // 191 - 'Bot Left'
  5,0x10,0x10,0x10,0x1f,0x10, // 192 - 'Bot T'
  5,0x10,0x10,0x10,0xf0,0x10, // 193 - 'Top T'
  5,0x00,0x00,0x00,0xff,0x10, // 194 - 'Left T'
  5,0x10,0x10,0x10,0x10,0x10, // 195 - 'Top side'
  5,0x10,0x10,0x10,0xff,0x10, // 196 - 'Center +'
  5,0x00,0x00,0x00,0xff,0x14, // 197 - 'Left side double H'
  5,0x00,0x00,0xff,0x00,0xff, // 198 - 'Left side double'
  5,0x00,0x00,0x1f,0x10,0x17, // 199 - 'Bot Left double V'
  5,0x00,0x00,0xfc,0x04,0xf4, // 200 - 'Top Left double V'
  5,0x14,0x14,0x17,0x10,0x17, // 201 - 'Bot T double'
  5,0x14,0x14,0xf4,0x04,0xf4, // 202 - 'Top T double'
  5,0x00,0x00,0xff,0x00,0xf7, // 203 - 'Left Side double spl'
  5,0x14,0x14,0x14,0x14,0x14, // 204 - 'Center double'
  5,0x14,0x14,0xf7,0x00,0xf7, // 205 - 'Center + double'
  5,0x14,0x14,0x14,0x17,0x14, // 206 - 'Bot T double H'
  5,0x10,0x10,0x1f,0x10,0x1f, // 207 - 'Bot Right double V'
  5,0x14,0x14,0x14,0xf4,0x14, // 208 - 'Top T double H'
  5,0x10,0x10,0xf0,0x10,0xf0, // 209 - 'Top Right double V'
  5,0x00,0x00,0x1f,0x10,0x1f, // 210 - 'Bot Left double V'
  5,0x00,0x00,0x00,0x1f,0x14, // 211 - 'Bot Right double H'
  5,0x00,0x00,0x00,0xfc,0x14, // 212 - 'Top Right double H'
  5,0x00,0x00,0xf0,0x10,0xf0, // 213 - 'Top Right double V'
  5,0x10,0x10,0xff,0x10,0xff, // 214 - 'Center + double V'
  5,0x14,0x14,0x14,0xff,0x14, // 215 - 'Center + double H'
  5,0x10,0x10,0x10,0x10,0x1f, // 216 - 'Bot Right'
  5,0x00,0x00,0x00,0xf0,0x10, // 217 - 'Top Left'
  5,0xff,0xff,0xff,0xff,0xff, // 218 - 'Full Block'
  5,0xf0,0xf0,0xf0,0xf0,0xf0, // 219 - 'Half Block Bottom'
  3,0xff,0xff,0xff, // 220 - 'Half Block LHS'
  5,0x00,0x00,0x00,0xff,0xff, // 221 - 'Half Block RHS'
  5,0x0f,0x0f,0x0f,0x0f,0x0f, // 222 - 'Half Block Top'
  5,0x38,0x44,0x44,0x38,0x44, // 223 - 'Alpha'
  5,0x7c,0x2a,0x2a,0x3e,0x14, // 224 - 'Beta'
  5,0x7e,0x02,0x02,0x06,0x06, // 225 - 'Gamma'
  5,0x02,0x7e,0x02,0x7e,0x02, // 226 - 'Pi'
  5,0x63,0x55,0x49,0x41,0x63, // 227 - 'Sigma'
  5,0x38,0x44,0x44,0x3c,0x04, // 228 - 'Theta'
  5,0x40,0x7e,0x20,0x1e,0x20, // 229 - 'mu'
  5,0x06,0x02,0x7e,0x02,0x02, // 230 - 'Tau'
  5,0x99,0xa5,0xe7,0xa5,0x99, // 231
  5,0x1c,0x2a,0x49,0x2a,0x1c, // 232
  5,0x4c,0x72,0x01,0x72,0x4c, // 233
  5,0x30,0x4a,0x4d,0x4d,0x30, // 234
  5,0x30,0x48,0x78,0x48,0x30, // 235
  5,0xbc,0x62,0x5a,0x46,0x3d, // 236 - 'Zero Slashed'
  4,0x3e,0x49,0x49,0x49,  // 237
  5,0x7e,0x01,0x01,0x01,0x7e, // 238
  5,0x2a,0x2a,0x2a,0x2a,0x2a, // 239 - '3 Bar Equals'
  5,0x44,0x44,0x5f,0x44,0x44, // 240 - '+/-'
  5,0x40,0x51,0x4a,0x44,0x40, // 241 - '>='
  5,0x40,0x44,0x4a,0x51,0x40, // 242 - '<='
  5,0x00,0x00,0xff,0x01,0x03, // 243 - 'Top of Integral'
  3,0xe0,0x80,0xff, // 244 - 'Bot of Integral'
  5,0x08,0x08,0x6b,0x6b,0x08, // 245 - 'Divide'
  5,0x36,0x12,0x36,0x24,0x36, // 246 - 'Wavy ='
  5,0x06,0x0f,0x09,0x0f,0x06, // 247 - 'Degree'
  4,0x00,0x00,0x18,0x18,  // 248 - 'Math Product'
  4,0x00,0x00,0x10,0x10,  // 249 - 'Short Dash'
  5,0x30,0x40,0xff,0x01,0x01, // 250 - 'Square Root'
  5,0x00,0x1f,0x01,0x01,0x1e, // 251 - 'Superscript n'
  5,0x00,0x19,0x1d,0x17,0x12, // 252 - 'Superscript 2'
  5,0x00,0x3c,0x3c,0x3c,0x3c, // 253 - 'Centered Square'
  5,0xff,0x81,0x81,0x81,0xff, // 254 - 'Full Frame'
  5,0xff,0xff,0xff,0xff,0xff, // 255 - 'Full Block'
};


void initMDLib() 
{
  mx.begin();
  // module initialize
  mx.control(MD_MAX72XX::INTENSITY, 0); // dot matix intensity 0-15
  mx.setFont( _sys_var_single );
}


void printStringWithShift(char *p, int shift_speed)
{
  uint8_t charWidth;
  uint8_t cBuf[8];  // this should be ok for all built-in fonts

  PRINTS("\nScrolling text");
  mx.clear();

  while (*p != '\0')
  {
    charWidth = mx.getChar(*p++, sizeof(cBuf) / sizeof(cBuf[0]), cBuf);

    for (uint8_t i=0; i<=charWidth; i++)  // allow space between characters
    {
      mx.transform(MD_MAX72XX::TSL);
      if (i < charWidth)
        mx.setColumn(0, cBuf[i]);
      delay(shift_speed);
    }
  }
}

void printString(char* s) 
{
  printText( 0, MAX_DEVICES-1, s );
}


void printText(uint8_t modStart, uint8_t modEnd, char *pMsg)
// Print the text string to the LED matrix modules specified.
// Message area is padded with blank columns after printing.
{
  uint8_t   state = 0;
  uint8_t   curLen;
  uint16_t  showLen;
  uint8_t   cBuf[8];
  int16_t   col = ((modEnd + 1) * COL_SIZE) - 1;

  mx.control(modStart, modEnd, MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);

  do     // finite state machine to print the characters in the space available
  {
    switch(state)
    {
      case 0: // Load the next character from the font table
        // if we reached end of message, reset the message pointer
        if (*pMsg == '\0')
        {
          showLen = col - (modEnd * COL_SIZE);  // padding characters
          state = 2;
          break;
        }

        // retrieve the next character form the font file
        showLen = mx.getChar(*pMsg++, sizeof(cBuf)/sizeof(cBuf[0]), cBuf);
        curLen = 0;
        state++;
        // !! deliberately fall through to next state to start displaying

      case 1: // display the next part of the character
        mx.setColumn(col--, cBuf[curLen++]);

        // done with font character, now display the space between chars
        if (curLen == showLen)
        {
          showLen = 1;
          state = 2;
        }
        break;

      case 2: // initialize state for displaying empty columns
        curLen = 0;
        state++;
        // fall through

      case 3:  // display inter-character spacing or end of message padding (blank columns)
        mx.setColumn(col--, 0);
        curLen++;
        if (curLen == showLen)
          state = 0;
        break;

      default:
        col = -1;   // this definitely ends the do loop
    }
  } while (col >= (modStart * COL_SIZE));

  mx.control(modStart, modEnd, MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
}


////////////////////////////////////////////////////////
//
// WS2812 Control
//
void setLedColor( char* color, int brightness) {
  uint32_t ledColor;
  
  if ( strcmp( color, "Red" ) == 0 ) 
    ledColor = CRGB::Red;
  if ( strcmp( color, "Green" ) == 0 ) 
    ledColor = CRGB::Green;
  if ( strcmp( color, "Blue" ) == 0 ) 
    ledColor = CRGB::Blue;
  if ( strcmp( color, "Purple" ) == 0 ) 
    ledColor = CRGB::Purple;
  if ( strcmp( color, "White" ) == 0 ) 
    ledColor = CRGB::White;
  if ( strcmp(color, "Yellow" ) == 0 ) 
    ledColor = CRGB::Yellow;
    
  for (int i=0; i<NUM_LEDS; i++) {leds[i] = ledColor;}  
  FastLED.setBrightness( brightness );
  FastLED.show();
  Serial.print("Color set: "); Serial.println(color);
}


//
//   Convert floating to character string, without using String
//
char *ftoa(char *a, double f, int precision)
{
 long p[] = {0,10,100,1000,10000,100000,1000000,10000000,100000000};
 
 char *ret = a;
 long heiltal = (long)f;
 itoa(heiltal, a, 10);
 while (*a != '\0') a++;
 if ( precision ) {
    *a++ = '.';
    long desimal = abs((long)((f - heiltal) * p[precision]));
    itoa(desimal, a, 10);
 }
 return ret;
}


// Web Server Routines
//
void handle_msg()  { 

  String msg = wServer.arg("msg");
  Serial.println(msg);

  String decodedMsg = msg;
  // Restore special characters that are misformed to %char by the client browser
  decodedMsg.replace("+", " ");
  decodedMsg.replace("%21", "!");
  decodedMsg.replace("%22", "");
  decodedMsg.replace("%23", "#");
  decodedMsg.replace("%24", "$");
  decodedMsg.replace("%25", "%");
  decodedMsg.replace("%26", "&");
  decodedMsg.replace("%27", "'");
  decodedMsg.replace("%28", "(");
  decodedMsg.replace("%29", ")");
  decodedMsg.replace("%2A", "*");
  decodedMsg.replace("%2B", "+");
  decodedMsg.replace("%2C", ",");
  decodedMsg.replace("%2F", "/");
  decodedMsg.replace("%3A", ":");
  decodedMsg.replace("%3B", ";");
  decodedMsg.replace("%3C", "<");
  decodedMsg.replace("%3D", "=");
  decodedMsg.replace("%3E", ">");
  decodedMsg.replace("%3F", "?");
  decodedMsg.replace("%40", "@");
  displayText = decodedMsg.c_str();
  displayText.remove(20);                               // Truncate to 20 characters 
  Serial.println(displayText);                           // print original string to monitor
  
  textOn = wServer.arg("textOn");
  if ( textOn.equals("text") && displayText.length() != 0) {   // display time if not set
    textOn = "text";
    displayTextTimes = 3;
  }
  else
    textOn = "time";
  Serial.print("textOn: "); Serial.println(textOn);

  intensity = wServer.arg("intensity");
  if ( intensity.length() == 0 ) {
    intensity = "0";
  }
  byte bIntensity = (byte)intensity.toInt();
  mx.control(MD_MAX72XX::INTENSITY, bIntensity);
  Serial.print("intensity: "); Serial.println(intensity);

  strcpy( auto_brightness, wServer.arg("auto_brightness").c_str() );
  Serial.print("auto_brightness: "); Serial.println(auto_brightness);
  
  ledStrip_intensity = wServer.arg("ledStrip_intensity");
  if ( ledStrip_intensity.length() == 0 ) {
    ledStrip_intensity = "10";
  } 
  FastLED.setBrightness( ledStrip_intensity.toInt() );    
  Serial.print("ledStrip_intensity: "); Serial.println(ledStrip_intensity);

  sColor = wServer.arg("sColor");
  if ( sColor.length() != 0 ) {
    Serial.print("sColor: "); Serial.println(sColor);
    setLedColor( (char*)sColor.c_str(), ledStrip_intensity.toInt() );
  }
  wServer.send(200, "text/html", setForm() ); // Same page with selections now set
}


String setForm( ) { 
  String form =                                             // String form to sent to the client-browser
    "<!DOCTYPE html>"
    "<html>"
    "<body>"
    "<title>iClock v2</title>"
    "<head> <meta name='viewport' content='width=device-width'></head>"
    "<center>"
      "<img src='http://www.olalde.org/Comp_TOL_Fam.png'><br>"
      "<b>Current text: " + displayText + "</b><br>"
      "<b>Current displaying: " + textOn + "</b><br>"
      "<b>Current intensity: " + intensity + "</b><br>"
      "<b>Current color: " + sColor + "</b><br>"
      "<b>Current led intensity: " + ledStrip_intensity + "</b>"
      "<form action='msg'><p>Set text and display option<br>"
        "<input type='text' name='msg' size=20 value='" + displayText + "'><br>"
        "<input type='radio' name='textOn' value='text' " +
          (textOn.equals("text") ? "checked='checked'" : "")+ ">Display Text<br>"
        "<input type='radio' name='textOn' value='time'" +
          (textOn.equals("time") ? "checked='checked'" : "")+ ">Display Time<br>"
        "<br>"
        "Set display intensity (0-15)"
        "<input type='text' name='intensity' size=3 value=" + intensity + "><br>"
        "Enable auto_brightness control"
        "<input type='checkbox' name='auto_brightness' size=3 value='On'" + 
          (strcmp(auto_brightness, "On")==0 ? "checked" : "")  + "><br>"
        "Set LED RGB color"
        "<select name=sColor>"
          "<option selected='selected'>" + sColor + "</option>"
          "<option value='Purple'>Purple</option>"
          "<option value='Red'>Red</option>"
          "<option value='Blue'>Blue</option>"
          "<option value='Green'>Green</option>"
          "<option value='White'>White</option>"
          "<option value='Yellow'>Yellow</option>"
        "</select><br>"
        "Set LED intensity (0-255)"
        "<input type='text' name='ledStrip_intensity' size=4 value=" + ledStrip_intensity + "><br>"
        "<br><input type='submit' name='Submit' value='Submit'>"
    "</center>"
    "</body>"
    "</html>"   ;
    return form;
}  


void handle_msgAdmin()  { 

  String msg = wServer.arg("msgAdmin");
  Serial.println(msg);

  if ( wServer.arg("Update").equals("Update") ) {
    Serial.println("Clicked Update");
    wServer.send(200, "text/html", 
      "<!DOCTYPE html><html><body><title>iClock v2</title><head><meta name='viewport' content='width=device-width'>\
      <meta http-equiv='refresh' content='60; url=http://iclock2.local/'></head><center><h1>UPDATING!!</h1>\
      <script>\
        var timeleft = 60;\
        var downloadTimer = setInterval(function(){\
          document.getElementById('progressBar').value = 60 - timeleft;\
          timeleft -= 1;\
          if(timeleft <= 0)\
            clearInterval(downloadTimer);\
        }, 1000);\
      </script>\
      <progress value='0' max='60' id='progressBar'></progress>\
      " );

    t_httpUpdate_return ret = ESPhttpUpdate.update(OTAclient, "http://192.168.1.225/file.bin");
    // Or:
    //t_httpUpdate_return ret = ESPhttpUpdate.update(OTAclient, "server", 80, "file.bin");

    switch (ret) {
      case HTTP_UPDATE_FAILED:
        Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
        break;
      case HTTP_UPDATE_NO_UPDATES:
        Serial.println("HTTP_UPDATE_NO_UPDATES");
        break;
      case HTTP_UPDATE_OK:
        Serial.println("HTTP_UPDATE_OK");
        break;
    }
    
    return;
  }
}


String setAdmin( ) { 
  String form =                                             // String form to sent to the client-browser
    "<!DOCTYPE html>"
    "<html>"
    "<body>"
    "<title>iClock v2 Admin</title>"
    "<head> <meta name='viewport' content='width=device-width'></head>"
    
      "<form action='msgAdmin'><p>Select administrative options<br>"
      "<br><br><br>"
      "Last boot: " + String(bootTime) +
      "<br><input type='submit' name='Update' value='Update'></form>"

    "</body>"
    "</html>";
    return form;
}


String result;
char current_temp[10];
char HiLoConditions[100];

String dateTime(String timestamp) {
  time_t ts = timestamp.toInt();
  char buff[30];
  sprintf(buff, "%2d:%02d %02d-%02d-%4d", hour(ts), minute(ts), day(ts), month(ts), year(ts));
  return String(buff);
}

char* displayOutsideWeather( char* timeBuf) {

  strcpy( timeBuf, "      Now: " );
  strcat( timeBuf, current_temp );
  return timeBuf;
}

void currentConditions(void) {
  OWM_conditions *ow_cond = new OWM_conditions;
  owCC.updateConditions(ow_cond, ow_key, "us", "McMurray", "imperial");
  Serial.print("Latitude & Longtitude: ");
  Serial.print("<" + ow_cond->longtitude + " " + ow_cond->latitude + "> @" + dateTime(ow_cond->dt) + ": ");
  Serial.println("icon: " + ow_cond->icon + ", " + " temp.: " + ow_cond->temp + ", press.: " + ow_cond->pressure + 
                ", desc.: " + ow_cond->description);
  ow_cond->temp.remove(ow_cond->temp.indexOf('.'));
  strcpy( current_temp, ow_cond->temp.c_str() );

  delete ow_cond;
  Serial.print( "current_temp:" ); Serial.print( current_temp ); Serial.println( "*" );
}

char* displayOutsideForecast( char* timeBuf) {

//  strcpy( timeBuf, "Now: " );
//  strcat( timeBuf, current_temp );
  strcpy( timeBuf, HiLoConditions );
  return timeBuf;
}

#define OWMDays 7
void oneDayFcast(void) {
  OWM_oneLocation *location   = new OWM_oneLocation;
  OWM_oneForecast *ow_fcast1 = new OWM_oneForecast[OWMDays];
  byte entries = owF1.updateForecast(location, ow_fcast1, OWMDays, ow_key, "us", "McMurray", "imperial");
  Serial.print("Entries: "); Serial.println(entries+1);
  for (byte i = 0; i < 2; i++) {    // Only copy two rows, today and tomorrow
    Serial.print(dateTime(ow_fcast1[i].dt) + ": icon: ");
    Serial.print(ow_fcast1[i].icon + ", temp.: [" + ow_fcast1[i].t_min + ", " + ow_fcast1[i].t_max + "], press.: " + ow_fcast1[i].pressure);
    Serial.println(", descr.: " + ow_fcast1[i].description + ":: " + ow_fcast1[i].main);
    if ( i==0 ) {    // Today's forecast
      strcpy( HiLoConditions, "   Today: "); 
    } else {
      strcat( HiLoConditions, "   Tomorrow: "); 
    }
    ow_fcast1[i].t_max.remove(ow_fcast1[i].t_max.indexOf('.')); ow_fcast1[i].t_min.remove(ow_fcast1[i].t_min.indexOf('.'));
    strcat( HiLoConditions, ow_fcast1[i].t_max.c_str() ); strcat( HiLoConditions, "/" ); strcat( HiLoConditions, ow_fcast1[i].t_min.c_str() );
    strcat( HiLoConditions, " "); strcat( HiLoConditions, ow_fcast1[i].main.c_str() );
  }
  delete[] ow_fcast1;
  delete location;
}
void fiveDayFcast(void) {
  OWM_fiveForecast *ow_fcast5 = new OWM_fiveForecast[40];
  byte entries = owF5.updateForecast(ow_fcast5, 40, ow_key, "us", "McMurray", "imperial");
  Serial.print("Entries: "); Serial.println(entries+1);
  for (byte i = 0; i <= entries; ++i) { 
    Serial.print(dateTime(ow_fcast5[i].dt) + ": icon: ");
    Serial.print(ow_fcast5[i].icon + ", temp.: [" + ow_fcast5[i].t_min + ", " + ow_fcast5[i].t_max + "], press.: " + ow_fcast5[i].pressure);
    Serial.println(", descr.: " + ow_fcast5[i].description + ":: " + ow_fcast5[i].cond + " " + ow_fcast5[i].cond_value);
  }
  delete[] ow_fcast5;
}
