
/*
    This sketch interfaces the ESP32-C3 to the Teensy 4.1 via serial port 2.
    It demonstrates how to receive a request for a scan for WiFi networks and
    report the results back via serial port and the LCD.

    If a 1.3" SH1106 OLED 128x64 display is attached to the ESP32-C3 I2C headers
    it also writes some info out to that display.

    This display uses the "U8g2lib.h" library that can be downloaded from the
    Arduino IDE.

    Change flag useDisplay to true if a display is attached.

    This is a simple variation of the ESP32 WiFiScan example program
*/

#include <Wire.h>
#include "WiFi.h"
#include <U8g2lib.h>

#define RX2 20  // Teensy 4.1 is connected to serial port #2
#define TX2 21
#define BTN_PIN 10

const bool useDisplay = false;  // Change to true if display is attached

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R2, /* reset=*/ U8X8_PIN_NONE);
//===============================================================================
//  Initialization
//===============================================================================
void setup() {
  Serial.begin(115200);   // USB port
  Serial1.begin(115200, SERIAL_8N1, RX2, TX2);  //Port connected to Teensy 4.1

  pinMode(BTN_PIN, INPUT_PULLUP);

  if (useDisplay) u8g2.begin();  // Initialize the SH1106 display if one is attached

  // Set WiFi to station mode and disconnect from an AP if previously connected
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  Serial.println("Setup done");
  if (useDisplay) {
    // Print something to the SH1106 display if it is attached
    u8g2.firstPage();                    // Start the write to the display
    u8g2.setFont(u8g2_font_ncenB14_tr);  // Set the font
    u8g2.drawStr(3, 35, "Hello World!"); // Write 'Hello World!'
    u8g2.drawRFrame(0, 0, 127, 63, 7);   // Draw a rectangle around it
    u8g2.nextPage();                     // Completes the write to the display
  }
}
//===============================================================================
//  Main
//===============================================================================
void loop() {
  //static   int i = 0;
  if (Serial1.available()) {
    char command = Serial1.read();
    Serial.println(command);
    if (command == '?') { // Are you there?
      Serial.println("Y");
      Serial1.print("Y");  // Acknowledge I'm attached
      if (useDisplay) {
        u8g2.firstPage();    // if SH1106 attached, update with status
        u8g2.setFont(u8g2_font_ncenB12_tr);  // Set the font
        u8g2.drawStr(3, 35, "I'm Connected!"); // Respond to T4.1 query
        u8g2.nextPage();
      }
    }
    if (command == 'S') {
      Serial.println("scan start");
      if (useDisplay) {
        u8g2.firstPage();
        u8g2.setFont(u8g2_font_ncenB10_tr);  // Set the font
        u8g2.drawStr(30, 35, "Scanning"); // Update SH1106 display
        u8g2.nextPage();
      }
      // WiFi.scanNetworks will return the number of networks found
      int n = WiFi.scanNetworks();
      Serial.println("scan done");
      if (n == 0) {
        Serial.println("no networks found");
        Serial1.println("No networks found");
      } else {
        Serial.print(n);
        Serial1.print(n);
        Serial.println(" Networks Found");
        Serial1.println(" Networks Found");
        for (int i = 0; i < n; ++i) {
          // Print SSID and RSSI for each network found to both USB and
          // out Serial2 to attached Teensy 4.1
          Serial.print(i + 1);
          Serial1.print(i + 1);
          Serial.print(": ");
          Serial1.print(": ");
          Serial.print(WiFi.SSID(i));
          Serial1.print(WiFi.SSID(i));
          Serial.print(" (");
          Serial1.print(" (");
          Serial.print(WiFi.RSSI(i));
          Serial1.print(WiFi.RSSI(i));
          Serial.print(")");
          Serial1.print(")");
          Serial.println((WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? " " : "*");
          Serial1.println((WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? " " : "*");
        }
        delay(10);
      }
      if (useDisplay) {
        u8g2.setFont(u8g2_font_6x10_mf);  // Set the font
        int lineHt = u8g2.getMaxCharHeight();
        u8g2.clear();  // Clear the display
        u8g2.setCursor(0, lineHt);
        for (int i = 0; i < n; ++i) {
          u8g2.print(WiFi.SSID(i));
          u8g2.updateDisplay();
          u8g2.setCursor(0, lineHt * (i + 2));
          delay(10);
        }
      }
    }
  }
  if (digitalRead(BTN_PIN) == LOW) {
    // button on pin 10 has been pressed - do something
  }
  WiFi.scanDelete();
}
