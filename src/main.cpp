/*
MIT License

Copyright (c) 2018 esp-rfid Community
Copyright (c) 2017 Ömer Şiar Baysal

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
 */

#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <SPI.h>
#include <ESP8266mDNS.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <TimeLib.h>
#include "Ntp.h"
#include <Servo.h>

#define DEBUG

#include <MFRC522.h>
#include "PN532.h"

MFRC522 mfrc522 = MFRC522();
PN532 pn532;
Servo myservo;  // create servo object to control a servo

int rfidss;
int readerType;
int relayPin;
int button=0;

//#endif

// these are from vendors
#include "webh/glyphicons-halflings-regular.woff.gz.h"
#include "webh/required.css.gz.h"
#include "webh/required.js.gz.h"

// these are from us which can be updated and changed
#include "webh/esprfid.js.gz.h"
#include "webh/esprfid.htm.gz.h"
#include "webh/index.html.gz.h"

#ifdef ESP8266
extern "C" {
	#include "user_interface.h"
}
#endif

NtpClient NTP;
WiFiEventHandler wifiDisconnectHandler, wifiConnectHandler;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

unsigned long blink_ = millis();
bool wifiFlag = false;
bool configMode = false;
int wmode;
uint8_t wifipin = 255;
#define LEDoff HIGH
#define LEDon LOW

// Variables for whole scope
const char *http_username = "admin";
char *http_pass = NULL;
unsigned long previousMillis = 0;
unsigned long previousLoopMillis = 0;
unsigned long currentMillis = 0;
unsigned long cooldown = 0;
unsigned long deltaTime = 0;
unsigned long uptime = 0;
bool shouldReboot = false;
bool activateRelay = false;
bool deactivateRelay = false;
bool inAPMode = false;
bool isWifiConnected = false;
unsigned long autoRestartIntervalSeconds = 0;

bool wifiDisabled = true;
bool doDisableWifi = false;
bool doEnableWifi = false;
bool timerequest = false;
bool formatreq = false;
unsigned long wifiTimeout = 0;
unsigned long wiFiUptimeMillis = 0;
char *deviceHostname = NULL;

char *mhs = NULL;
char *muser = NULL;
char *mpas = NULL;
int mport;

int lockType;
int relayType;
unsigned long activateTime;
int timeZone;
int servopos = 0;    // variable to store the servo position

unsigned long nextbeat = 0;
unsigned long interval = 1800;

#include "log.esp"
#include "helpers.esp"
#include "wsResponses.esp"
#include "rfid.esp"
#include "wifi.esp"
#include "config.esp"
#include "websocket.esp"
#include "webserver.esp"

void ICACHE_FLASH_ATTR setup()


{
myservo.attach(4);  // attaches the servo on pin 9 to the servo object

myservo.write(95);
delay(500);         // tell servo to go to position in variable 'pos'

myservo.detach();
pinMode(button,INPUT_PULLUP);

#ifdef DEBUG
	Serial.begin(9600);
	Serial.println();

	Serial.println(F("[ INFO ] ESP RFID v0.9"));

	uint32_t realSize = ESP.getFlashChipRealSize();
	uint32_t ideSize = ESP.getFlashChipSize();
	FlashMode_t ideMode = ESP.getFlashChipMode();
	Serial.printf("Flash real id:   %08X\n", ESP.getFlashChipId());
	Serial.printf("Flash real size: %u\n\n", realSize);
	Serial.printf("Flash ide  size: %u\n", ideSize);
	Serial.printf("Flash ide speed: %u\n", ESP.getFlashChipSpeed());
	Serial.printf("Flash ide mode:  %s\n", (ideMode == FM_QIO ? "QIO" : ideMode == FM_QOUT ? "QOUT" : ideMode == FM_DIO ? "DIO" : ideMode == FM_DOUT ? "DOUT" : "UNKNOWN"));
	if (ideSize != realSize)
	{
		Serial.println("Flash Chip configuration wrong!\n");
	}
	else
	{
		Serial.println("Flash Chip configuration ok.\n");
	}
#endif

	if (!SPIFFS.begin())
	{
#ifdef DEBUG
		Serial.print(F("[ WARN ] Formatting filesystem..."));
#endif
		if (SPIFFS.format())
		{
			writeEvent("WARN", "sys", "Filesystem formatted", "");

#ifdef DEBUG
			Serial.println(F(" completed!"));
#endif
		}
		else
		{
#ifdef DEBUG
			Serial.println(F(" failed!"));
			Serial.println(F("[ WARN ] Could not format filesystem!"));
#endif
		}
	}
	wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);
	wifiConnectHandler = WiFi.onStationModeConnected(onWifiConnect);
	configMode = loadConfiguration();
	if (!configMode)
	{
		fallbacktoAPMode();
		configMode = false;
	}
	else {
		configMode = true;
	}
	setupWebServer();
	writeEvent("INFO", "sys", "System setup completed, running", "");
}

void ICACHE_RAM_ATTR loop()
{


	if(digitalRead(button)==LOW)
	 {
		 delay(4000);
		myservo.attach(4);  // attaches the servo on pin 9 to the servo object
		myservo.write(0);
	 	delay(500);
	 	myservo.write(95);
	 	delay(500);             // tell servo to go to position in variable 'pos'
	 	myservo.detach();
	}
	currentMillis = millis();
	deltaTime = currentMillis - previousLoopMillis;
	uptime = NTP.getUptimeSec();
	previousLoopMillis = currentMillis;

	if (wifipin != 255 && configMode && !wmode)
	{
		if (!wifiFlag)
		{
			if ((currentMillis - blink_) > 500)
			{
				blink_ = currentMillis;
				digitalWrite(wifipin, !digitalRead(wifipin));
			}
		}
		else
		{
			if (!(digitalRead(wifipin)==LEDon)) digitalWrite(wifipin, LEDon);
		}
	}

	if (currentMillis >= cooldown)
	{
		rfidloop();
	}

	// Continuous relay mode
	if (lockType == 1)
	{
		if (activateRelay)
		{
			// currently OFF, need to switch ON
			//unlock------------------------------

			if (digitalRead(relayPin) == !relayType)
			{
#ifdef DEBUG
				Serial.print("mili : ");
				Serial.println(millis());
				Serial.println("activating relay now");
				Serial.println("servo unlocks Continuous");

#endif

				//digitalWrite(relayPin, relayType);
				myservo.attach(4);  // attaches the servo on pin 9 to the servo object
				myservo.write(180);        // tell servo to go to position in variable 'pos'
				delay(500);
				myservo.detach();
			}
			//lock--------------------------------------------------
			else	// currently ON, need to switch OFF
			{
#ifdef DEBUG
				Serial.print("mili : ");
				Serial.println(millis());
				Serial.println("deactivating relay now");
				Serial.println("servo locks");

#endif
				//digitalWrite(relayPin, !relayType);
				myservo.attach(4);  // attaches the servo on pin 9 to the servo object
				myservo.write(79);
				delay(500);             // tell servo to go to position in variable 'pos'
				myservo.detach();
			}
			activateRelay = false;
		}
	}
	else if (lockType == 0)	// momentary relay mode
	{
		if (activateRelay)
		{
#ifdef DEBUG
			Serial.print("mili : ");
			Serial.println(millis());
			Serial.println("activating relay now");
			Serial.println("servo unlocks moment");

#endif
			//unlock-----------------------------------------
			//digitalWrite(relayPin, relayType);
			myservo.attach(4);  // attaches the servo on pin 9 to the servo object
			myservo.write(180);              // tell servo to go to position in variable 'pos'
			delay(500);
			myservo.write(95);
			delay(200);
			myservo.detach();
			previousMillis = millis();
			activateRelay = false;
			deactivateRelay = true;
		}
		else if ((currentMillis - previousMillis >= activateTime) && (deactivateRelay))
		{
#ifdef DEBUG
			Serial.println(currentMillis);
			Serial.println(previousMillis);
			Serial.println(activateTime);
			Serial.println(activateRelay);
			Serial.println("deactivate relay after this");
			Serial.print("mili : ");
			Serial.println(millis());
#endif
			//lock--------------------------------------------------
			//digitalWrite(relayPin, !relayType);

			//
			// myservo.attach(4);  // attaches the servo on pin 9 to the servo object
			// myservo.write(0);
			// delay(1000);
			// myservo.write(80);
			// delay(500);             // tell servo to go to position in variable 'pos'
			// myservo.detach();
			// deactivateRelay = false;
		}
	}

	if (formatreq)
	{
#ifdef DEBUG
		Serial.println(F("[ WARN ] Factory reset initiated..."));
#endif
		SPIFFS.end();
		ws.enable(false);
		SPIFFS.format();
		ESP.restart();
	}

	if (timerequest)
	{
		timerequest = false;
		sendTime();
	}

	if (autoRestartIntervalSeconds > 0 && uptime > autoRestartIntervalSeconds * 1000)
	{
		writeEvent("INFO", "sys", "System is going to reboot", "");
#ifdef DEBUG
		Serial.println(F("[ WARN ] Auto restarting..."));
#endif
		shouldReboot = true;
	}

	if (shouldReboot)
	{
		writeEvent("INFO", "sys", "System is going to reboot", "");
#ifdef DEBUG
		Serial.println(F("[ INFO ] Rebooting..."));
#endif
		ESP.restart();
	}

	if (isWifiConnected)
	{
		wiFiUptimeMillis += deltaTime;
	}

	if (wifiTimeout > 0 && wiFiUptimeMillis > (wifiTimeout * 1000) && isWifiConnected == true)
	{
		writeEvent("INFO", "wifi", "WiFi is going to be disabled", "");
		doDisableWifi = true;
	}

	if (doDisableWifi == true)
	{
		doDisableWifi = false;
		wiFiUptimeMillis = 0;
		disableWifi();
	}
	else if (doEnableWifi == true)
	{
		writeEvent("INFO", "wifi", "Enabling WiFi", "");
		doEnableWifi = false;
		if (!isWifiConnected)
		{
			wiFiUptimeMillis = 0;
			enableWifi();
		}
	}
}
