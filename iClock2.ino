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
#include <MaxMatrix.h>                  // MAX7912
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
const int displayWeatherPin = 4;
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
    return true;
  } else {
    return false;
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
  ESPhttpUpdate.setLedPin(16, LOW);

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
const int my_data = 13;    // DIN pin of MAX7219 module
const int my_load = 12;    // CS pin of MAX7219 module
const int my_clock = 14;  // CLK pin of MAX7219 module

const int maxInUse = 4;    //change this variable to set how many MAX7219's you'll use
MaxMatrix m(my_data, my_load, my_clock, maxInUse); // define module


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
  m.init(); // module initialize
  m.setIntensity(0); // dot matix intensity 0-15
  
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
        strcpy( timeBuf, timeBuf+1);
      Serial.println(timeBuf);
      printString(timeBuf);
    }
    
    //
    // Adjust display for brightness
    //
    if ( strcmp(auto_brightness, "On") == 0 ) {
      if ( isLight() ) 
        m.setIntensity(15);
      else
        m.setIntensity(0);
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






/*
################################################################################
# File Name:             MAX7219_5.ino                                             
# Board:                 Arduino UNO         
# Programming Language:   Wiring / C /Processing /Fritzing / Arduino IDE          
#           
# Objective:             Scrolling LED dot Matrix
#                     
# Operation:           Scrolls a message over a 16x8 LED dot matrix
#     
# Author:                Marcelo Moraes 
# Date:                  July 9th, 2013 
# Place:                 Sorocaba - SP - Brazil 
#         
################################################################################
 
 This code is a public example.
 */

//******************************************************************************
// visit this web page for further information about MaxMatrix library
// https://code.google.com/p/arudino-maxmatrix-library/
//******************************************************************************


PROGMEM const char CH[] = {
3, 8, B00000000, B00000000, B00000000, B00000000, B00000000, // space
1, 8, B01011111, B00000000, B00000000, B00000000, B00000000, // !
3, 8, B00000011, B00000000, B00000011, B00000000, B00000000, // "
5, 8, B00010100, B00111110, B00010100, B00111110, B00010100, // #
4, 8, B00100100, B01101010, B00101011, B00010010, B00000000, // $
5, 8, B01100011, B00010011, B00001000, B01100100, B01100011, // %
5, 8, B00110110, B01001001, B01010110, B00100000, B01010000, // &
1, 8, B00000011, B00000000, B00000000, B00000000, B00000000, // '
3, 8, B00011100, B00100010, B01000001, B00000000, B00000000, // (
3, 8, B01000001, B00100010, B00011100, B00000000, B00000000, // )
5, 8, B00101000, B00011000, B00001110, B00011000, B00101000, // *
5, 8, B00001000, B00001000, B00111110, B00001000, B00001000, // +
2, 8, B10110000, B01110000, B00000000, B00000000, B00000000, // ,
4, 8, B00001000, B00001000, B00001000, B00001000, B00000000, // -
2, 8, B01100000, B01100000, B00000000, B00000000, B00000000, // .
4, 8, B01100000, B00011000, B00000110, B00000001, B00000000, // /
4, 8, B00111110, B01000001, B01000001, B00111110, B00000000, // 0
3, 8, B01000010, B01111111, B01000000, B00000000, B00000000, // 1
4, 8, B01100010, B01010001, B01001001, B01000110, B00000000, // 2
//3, 8, B01100010, B01010001, B01001110, B01000110, B00000000, // 2      sucky 3 bit version
4, 8, B00100010, B01000001, B01001001, B00110110, B00000000, // 3
4, 8, B00011000, B00010100, B00010010, B01111111, B00000000, // 4
4, 8, B00100111, B01000101, B01000101, B00111001, B00000000, // 5
4, 8, B00111110, B01001001, B01001001, B00110000, B00000000, // 6
4, 8, B01100001, B00010001, B00001001, B00000111, B00000000, // 7
4, 8, B00110110, B01001001, B01001001, B00110110, B00000000, // 8
4, 8, B00000110, B01001001, B01001001, B00111110, B00000000, // 9
1, 8, B00101000, B00000000, B00000000, B00000000, B00000000, // :
1, 8, B10000000, B01010000, B00000000, B00000000, B00000000, // ;
3, 8, B00010000, B00101000, B01000100, B00000000, B00000000, // <
3, 8, B00010100, B00010100, B00010100, B00000000, B00000000, // =
3, 8, B01000100, B00101000, B00010000, B00000000, B00000000, // >
4, 8, B00000010, B01011001, B00001001, B00000110, B00000000, // ?
5, 8, B00111110, B01001001, B01010101, B01011101, B00001110, // @
4, 8, B01111110, B00010001, B00010001, B01111110, B00000000, // A
4, 8, B01111111, B01001001, B01001001, B00110110, B00000000, // B
4, 8, B00111110, B01000001, B01000001, B00100010, B00000000, // C
4, 8, B01111111, B01000001, B01000001, B00111110, B00000000, // D
4, 8, B01111111, B01001001, B01001001, B01000001, B00000000, // E
4, 8, B01111111, B00001001, B00001001, B00000001, B00000000, // F
4, 8, B00111110, B01000001, B01001001, B01111010, B00000000, // G
4, 8, B01111111, B00001000, B00001000, B01111111, B00000000, // H
3, 8, B01000001, B01111111, B01000001, B00000000, B00000000, // I
4, 8, B00110000, B01000000, B01000001, B00111111, B00000000, // J
4, 8, B01111111, B00001000, B00010100, B01100011, B00000000, // K
4, 8, B01111111, B01000000, B01000000, B01000000, B00000000, // L
5, 8, B01111111, B00000010, B00001100, B00000010, B01111111, // M
5, 8, B01111111, B00000100, B00001000, B00010000, B01111111, // N
4, 8, B00111110, B01000001, B01000001, B00111110, B00000000, // O
4, 8, B01111111, B00001001, B00001001, B00000110, B00000000, // P
4, 8, B00111110, B01000001, B01000001, B10111110, B00000000, // Q
4, 8, B01111111, B00001001, B00001001, B01110110, B00000000, // R
4, 8, B01000110, B01001001, B01001001, B00110010, B00000000, // S
5, 8, B00000001, B00000001, B01111111, B00000001, B00000001, // T
4, 8, B00111111, B01000000, B01000000, B00111111, B00000000, // U
5, 8, B00001111, B00110000, B01000000, B00110000, B00001111, // V
5, 8, B00111111, B01000000, B00111000, B01000000, B00111111, // W
5, 8, B01100011, B00010100, B00001000, B00010100, B01100011, // X
5, 8, B00000111, B00001000, B01110000, B00001000, B00000111, // Y
4, 8, B01100001, B01010001, B01001001, B01000111, B00000000, // Z
2, 8, B01111111, B01000001, B00000000, B00000000, B00000000, // [
4, 8, B00000001, B00000110, B00011000, B01100000, B00000000, // \ backslash
2, 8, B01000001, B01111111, B00000000, B00000000, B00000000, // ]
3, 8, B00000010, B00000101, B00000010, B00000000, B00000000,// degree symbol
//3, 8, B00000010, B00000001, B00000010, B00000000, B00000000, // hat
4, 8, B01000000, B01000000, B01000000, B01000000, B00000000, // _
2, 8, B00000001, B00000010, B00000000, B00000000, B00000000, // `
4, 8, B00100000, B01010100, B01010100, B01111000, B00000000, // a
4, 8, B01111111, B01000100, B01000100, B00111000, B00000000, // b
4, 8, B00111000, B01000100, B01000100, B00101000, B00000000, // c
4, 8, B00111000, B01000100, B01000100, B01111111, B00000000, // d
4, 8, B00111000, B01010100, B01010100, B00011000, B00000000, // e
3, 8, B00000100, B01111110, B00000101, B00000000, B00000000, // f
4, 8, B10011000, B10100100, B10100100, B01111000, B00000000, // g
4, 8, B01111111, B00000100, B00000100, B01111000, B00000000, // h
3, 8, B01000100, B01111101, B01000000, B00000000, B00000000, // i
4, 8, B01000000, B10000000, B10000100, B01111101, B00000000, // j
4, 8, B01111111, B00010000, B00101000, B01000100, B00000000, // k
3, 8, B01000001, B01111111, B01000000, B00000000, B00000000, // l
5, 8, B01111100, B00000100, B01111100, B00000100, B01111000, // m
4, 8, B01111100, B00000100, B00000100, B01111000, B00000000, // n
4, 8, B00111000, B01000100, B01000100, B00111000, B00000000, // o
4, 8, B11111100, B00100100, B00100100, B00011000, B00000000, // p
4, 8, B00011000, B00100100, B00100100, B11111100, B00000000, // q
4, 8, B01111100, B00001000, B00000100, B00000100, B00000000, // r
4, 8, B01001000, B01010100, B01010100, B00100100, B00000000, // s
3, 8, B00000100, B00111111, B01000100, B00000000, B00000000, // t
4, 8, B00111100, B01000000, B01000000, B01111100, B00000000, // u
5, 8, B00011100, B00100000, B01000000, B00100000, B00011100, // v
5, 8, B00111100, B01000000, B00111100, B01000000, B00111100, // w
5, 8, B01000100, B00101000, B00010000, B00101000, B01000100, // x
4, 8, B10011100, B10100000, B10100000, B01111100, B00000000, // y
3, 8, B01100100, B01010100, B01001100, B00000000, B00000000, // z
3, 8, B00001000, B00110110, B01000001, B00000000, B00000000, // {
1, 8, B01111111, B00000000, B00000000, B00000000, B00000000, // |
3, 8, B01000001, B00110110, B00001000, B00000000, B00000000, // }
4, 8, B00001000, B00000100, B00001000, B00000100, B00000000, // ~
};

byte buffer[10];

void printCharWithShift(char c, int shift_speed){
  if (c < 32) return;
  c -= 32;
  memcpy_P(buffer, CH + 7*c, 7);
  m.writeSprite(32, 0, buffer);
  m.setColumn(32 + buffer[0], 0);
  
  for (int i=0; i<buffer[0]+1; i++) 
  {
    delay(shift_speed);
    m.shiftLeft(false, false);
  }
}

void printStringWithShift(char* s, int shift_speed){
  while (*s != 0){
    printCharWithShift(*s, shift_speed);
    s++;
  }
}

void printString(char* s)
{
  int col = 0;
  m.clear();
  while (*s != 0)
  {
    if (*s < 32) continue;
    char c = *s - 32;
    memcpy_P(buffer, CH + 7*c, 7);
    m.writeSprite(col, 0, buffer);
    m.setColumn(col + buffer[0], 0);
    col += buffer[0] + 1;
    s++;
  }
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
  m.setIntensity( bIntensity );    
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
