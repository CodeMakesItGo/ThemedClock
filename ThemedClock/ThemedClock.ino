
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <YoutubeApi.h> // Brian Lough
#include <ArduinoJson.h>
#include <FastLED.h>
#include <NTPClient.h> // Frabrice Weinberg
#include <LedControl.h> // Eberhard Fahle
#include <DFPlayerMini_Fast.h> // PowerBroker2

#define VERSION "YouTube SubDisplay 1.0\n"

const char* ssid     = "SSID";    // SSID of local network
const char* password = "PASSWORD";    // Password on network

#define API_KEY     "API_KEY"        // https://console.developers.google.com
#define CHANNEL_ID  "CHANNEL_ID"        // https://www.youtube.com/account_advanced


#define SUB_UPDATE 60000       // Subscriber count checked every minute
#define DISPLAY_UPDATE 500     // Segment display is updated 2 times a second
#define WELD_UPDATE 50         // Weld simulation LEDs are updated 20 times a second
#define WELD_FLASH_TIME 31000  // Weld simulation plays for 31 seconds to match sound file

//8 digit 7-segment display
#define DIN 5
#define CS 4
#define CLK 14
#define SEG_BRIGHTNESS 15 //0-15

//Jewel LED
#define NUM_LEDS 7
#define LED_PIN 3
#define LED_BRIGHTNESS 255 //0-255

//MP3 Player
#define VOLUME 30 //0-30

//timers
static unsigned long last_SubUpdate = 0;
static unsigned long last_DisplayUpdate = 0;
static unsigned long last_WeldUpdate = 0;
static unsigned long last_WeldFlashStart = 0;
static unsigned int last_hour = 0;

//Global variables (file scope)
static bool displayWeldFlash = false;
const long utcOffsetInSeconds = 3600; //Set for time zone, doesn't matter for this app
static long subscribers = 0;
static long subscribersBefore = 0;

//Wifi clients
WiFiClientSecure client;
WiFiUDP ntpUDP;

//Time of day
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

//YouTube Stats
YoutubeApi api(API_KEY, client);

//8 digit 7-segment display
LedControl lc=LedControl(DIN,CLK,CS,1);

// Define the array of leds
CRGB leds[NUM_LEDS];

//Sound Player
DFPlayerMini_Fast myMP3;

// Output to the 8 7-segment display
void printNumber(String subs, bool noDecimals = false) 
{  
  lc.clearDisplay(0);
  
  for(int index = 0; index < 8; ++index)
  {
    int digits = subs.length();
    if(index >= digits)
    {
      lc.setChar(0,index,' ',false); 
    }
    else
    {
      int digit = digits - index - 1;
      lc.setChar(0, index, subs[digit], noDecimals == false && (index == 3 || index == 6)  ? true : false); 
    }
  }
} 

void WeldFlash()
{
  FastLED.setBrightness(  LED_BRIGHTNESS );

  //Center is always white
  leds[0] = CRGB::White;
  
  //All other LEDs are a blue color
  leds[1].setRGB(0x0,0x6d,0xab);
  leds[2] = leds[1];
  leds[3] = leds[1];
  leds[4] = leds[1];
  leds[5] = leds[1];
  leds[6] = leds[1];

  //Get a random brightness to create a flicker effect
  int randNumber = random(LED_BRIGHTNESS);
  
  //Only fade leds 1-6, LED 0 stays bright white
  fadeToBlackBy(&(leds[1]), 6, randNumber);
  
  //Update the Jewel
  FastLED.show();
}

void updateSubscribers()
{
  int attempts = 10;

  // Try 10 times to get a valid response
  while(api.getChannelStatistics(CHANNEL_ID) == false && attempts > 0)
  {
    delay(1000);
    attempts--;
  }

  if(attempts > 0)
  {
    subscribers = api.channelStats.subscriberCount;
  }
  else
  {
    printNumber("--------", true);
  }
}

void setup()  
{
  //Serial must be 9600 for sound player
  Serial.begin(9600);
  Serial.println(VERSION);
  
  // Startup the Display
  lc.shutdown(0,false);
  // Set the brightness to a high values
  lc.setIntensity(0,SEG_BRIGHTNESS);
  // clear the display
  lc.clearDisplay(0);

  // Display test number while connecting
  printNumber("12345678");

  //Start FastLED
  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, NUM_LEDS); 
  //Turn off the Jewel
  FastLED.setBrightness( 0 );

  //Needed for YouTube API
  client.setInsecure();
  //Start Wifi connection
  WiFi.begin(ssid, password);
  Serial.println("Connecting to Wifi");
  
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
  }

  //Start NTP
  timeClient.begin();

  Serial.println("Connected to Wifi");

    // Display blank subscriber count
  printNumber("--------", true);

  //Let things sync up
  delay(5000);

  //Remap output to GPIO2 so we can still program
  Serial.set_tx(2);

  //Assign our serial port to be used for the sound player
  myMP3.begin(Serial);
  myMP3.volume(VOLUME);  
}

void loop() 
{
  long time = millis();
  
  // Update sub count and check hour every minute
  if(time - last_SubUpdate > SUB_UPDATE || last_SubUpdate == 0)
  {
    timeClient.update();
    last_SubUpdate = time;
    unsigned int this_hour = timeClient.getHours();
    
    updateSubscribers();

    if(subscribers != subscribersBefore)
    {
      printNumber(String(subscribers));
      Serial.println(subscribers);
      subscribersBefore = subscribers;
      displayWeldFlash = true;
      myMP3.play(2); //Play the haslip sound
    }  
    else if(last_hour != this_hour)
    {
      Serial.println(this_hour);
      displayWeldFlash = true;
      last_hour = this_hour;
      myMP3.play(1);  //Play the weld sound
    }
    else
    {
      //Do Nothing
    }
  }

  //Update segment display
  if(time - last_DisplayUpdate > DISPLAY_UPDATE)
  {
    last_DisplayUpdate = time;
    
    if(subscribers > 0)
      printNumber(String(subscribers));
  }

  //Update Jewel Flasher
  if(displayWeldFlash && last_WeldFlashStart > 0)
  {
    // timeout for weld flash
    if(time - last_WeldFlashStart > WELD_FLASH_TIME)
    {
      displayWeldFlash = false;
    }

    //Update weld flash
    if(time - last_WeldUpdate > WELD_UPDATE || last_WeldUpdate == 0)
    {
      last_WeldUpdate = time;
      WeldFlash();
    }
  }
  else
  {
    last_WeldFlashStart = time;
    FastLED.setBrightness( 0 );
    FastLED.show();
  }
  
  delay(1);
}
