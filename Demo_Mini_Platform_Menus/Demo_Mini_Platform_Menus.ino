/*
  Mini Platform for Teensy 4.1 Menu Example Software

  This program demonstrates some of the features
  of the Mini Platform board. 
  
  It makes use of the TeensyUserInterface.h library. Use this 
  fork for the library which includes ST7796 LCD support:
  https://github.com/mjs513/TeensyUserInterface/tree/my_edits

  This software is in the public domain
*/

#include "ST7796_t3.h"
#include <font_Arial.h>
#include <font_ArialBold.h>
#include <Adafruit_FT6206.h>
#include <TeensyUserInterface.h>
#include <SPI.h>
#include <Audio.h>
#include <Wire.h>
#include <SD.h>
#include <Bounce2.h>
#include <SerialFlash.h>
#include <LittleFS.h>
#include <USBHost_t36.h>
#include <EEPROM.h>
#include <QNEthernet.h>
using namespace qindesign::network;
extern "C" uint8_t external_psram_size;

// Setup USBHost_t36 and as many HUB ports as needed.
USBHost myusb;
USBHub hub1(myusb);
// Instances for one drive
USBDrive myDrive(myusb);
// Instances for accessing the files on the drive
USBFilesystem myFiles(myusb);

// Setup audio subsystem
// Audio components
AudioSynthWaveformSine  sine1;         // Used with Tetris
AudioPlaySdWav          playSdWav1;    // Plays WAV from SD card
AudioInputI2S           audioInput1;   // Gets audio from Mic or Line-In
AudioOutputI2S          audioOutput1;  // Sends audio to Headphone/Line-Out
AudioMixer4             mixerLeft;     // Routes selected input on left channel
AudioMixer4             mixerRight;    // Routes selected input on right channel

// Patch cables to connect everything
// 1. Route SD Card (Stereo)
AudioConnection          patchCord1(playSdWav1, 0, mixerLeft, 0);  // SD Left -> Left Mixer ch 0
AudioConnection          patchCord2(playSdWav1, 1, mixerRight, 0); // SD Right -> Right Mixer ch 0

// 2. Route I2S Hardware Input (Mic / Line-In Stereo)
AudioConnection          patchCord3(audioInput1, 0, mixerLeft, 1);  // Input Left -> Left Mixer ch 1
AudioConnection          patchCord4(audioInput1, 1, mixerRight, 1); // Input Right -> Right Mixer ch 1

// 3. Route Sine Wave (Mono source split into both Left and Right channels)
AudioConnection          patchCord5(sine1, 0, mixerLeft, 2);       // Sine -> Left Mixer ch 2
AudioConnection          patchCord6(sine1, 0, mixerRight, 2);      // Sine -> Right Mixer ch 2

// 4. Route Mixers to the physical Stereo Output Hardware
AudioConnection          patchCord7(mixerLeft, 0, audioOutput1, 0);  // Left Mixer -> Headphone Left
AudioConnection          patchCord8(mixerRight, 0, audioOutput1, 1); // Right Mixer -> Headphone Right

AudioControlSGTL5000 sgtl5000_1;

// Audio Constants
#define INPUT_MIC     0
#define INPUT_LINEIN  1
#define INPUT_SD      2
#define INPUT_SINE    3

// Pins used with the Teensy 4.1 built-in SD card
#define SDCARD_CS_PIN BUILTIN_SDCARD
#define SDCARD_MOSI_PIN 11
#define SDCARD_SCK_PIN 13

// Baseboard 16MB/128Mb NOR flash chip on SPI1 CS pin
#define FLASH_CS 37

// ESP32 is attached to Serial7 port on Teensy 4.1
#define ESP32SERIAL Serial7
// Create buffer for incoming chars from ESP32-C3
#define ESP32SERIAL_BUFFER_SIZE 1024
unsigned char esp32SerialBuffer[ESP32SERIAL_BUFFER_SIZE];

#define TXT_FONT Arial_12
#define TXT_FONTBOLD Arial_12_Bold
#define TXT_CALCBOLD Arial_14_Bold

// Pins used for LCD in addition to the default SPI pins
const int LCD_CS_PIN = 10;
const int LCD_DC_PIN = 9;
const int TOUCH_CS_PIN = 41;  //Not used.  For ILI9341 compatibility

// Tetris pushbuttons
#define BTN_L 22
#define BTN_R 32
#define BTN_ROT 40

boolean sDCardInstalled = false;  // Flag if SD card installed
boolean esp32SAttached = false;   // Flag if ESP32S is attached

// Create low level display and touch objects
ST7796_t3 tft = ST7796_t3(LCD_CS_PIN, LCD_DC_PIN);
Adafruit_FT6206 ctp = Adafruit_FT6206();

// Create the user interface object
TeensyUserInterface ui;

//===============================================================================
//  Initialization
//===============================================================================
void setup() {

  tft.init(320, 480);         // Create tft object for use with Tetris
  tft.setRotation(3);         // Landscape
  tft.invertDisplay(true);    // Invert colors so that they are correct

  ctp.begin(40);              // Touch used with Tetris
  // setup the LCD orientation, the default font and initialize the user interface
  ui.begin(LCD_CS_PIN, LCD_DC_PIN, TOUCH_CS_PIN, LCD_ORIENTATION_LANDSCAPE_4PIN_RIGHT, Arial_9_Bold);

  // use a grayscale color palette for main screen
  ui.setColorPaletteGray();
  // Use larger font to make back button easier to hit
  ui.setTitleBarFont(Arial_16_Bold);
  ui.setMenuFont(Arial_14_Bold);

  Serial.begin(115200);       //Initialize USB serial port to computer
  ESP32SERIAL.begin(115200);  //Initialize Seria1 7 connected to ESP32-C3
  // Setup extra memory for the ESP32 serial buffer to avoid possible overruns
  ESP32SERIAL.addMemoryForRead(esp32SerialBuffer, ESP32SERIAL_BUFFER_SIZE);

  // Start the Ethernet port
  Ethernet.begin();

  // Start the USB Host port
  myusb.begin();

  // Define pins to get SPI1 to work with NOR Flash chip
  SPI1.setMOSI(26);
  SPI1.setSCK(27);
  SPI1.setMISO(39);
  SPI1.setCS(37);
  SPI1.begin();

  pinMode(BTN_L, INPUT_PULLUP); pinMode(BTN_R, INPUT_PULLUP); pinMode(BTN_ROT, INPUT_PULLUP);

  // Setup initial audio settings
  AudioMemory(8);
  sgtl5000_1.enable();
  sgtl5000_1.volume(0.5);
  sgtl5000_1.unmuteLineout();
  sgtl5000_1.inputSelect(AUDIO_INPUT_LINEIN);
  sgtl5000_1.lineInLevel(5);
  sgtl5000_1.micGain(20);

  // Check for SD card installed in Teensy 4.1 card slot
  if (!(SD.begin(SDCARD_CS_PIN))) {
    Serial.println("SD card not found");
    sDCardInstalled = false;
  } else {
    Serial.println("SD card is Inserted");
    sDCardInstalled = true;
  }
}

// ---------------------------------------------------------------------------------
//           Define the menus and top level loop, place menus after setup()
// ---------------------------------------------------------------------------------

// for each menu, create a forward declaration with "extern"
extern MENU_ITEM mainMenu[];

// the main menu
MENU_ITEM mainMenu[] = {
  { MENU_ITEM_TYPE_MAIN_MENU_HEADER, "Mini Platform for Teensy 4.1 Demo", MENU_COLUMNS_3, mainMenu },
  { MENU_ITEM_TYPE_COMMAND, "Audio", cmdAudioPage, NULL },
  { MENU_ITEM_TYPE_COMMAND, "Scan WiFi", cmdScanWifiPage, NULL },
  { MENU_ITEM_TYPE_COMMAND, "System Info", cmdSystemInfoPage, NULL },
  { MENU_ITEM_TYPE_COMMAND, "USB Host", cmdUsbHostPage, NULL },
  { MENU_ITEM_TYPE_COMMAND, "Ethernet", cmdEthernetPage, NULL },
  { MENU_ITEM_TYPE_COMMAND, "Mandelbrot", cmdMandelbrotPage, NULL },
  { MENU_ITEM_TYPE_COMMAND, "Tetris", cmdTetrisPage, NULL },
  { MENU_ITEM_TYPE_COMMAND, "RGB LED", cmdRgbLedPage, NULL },
  { MENU_ITEM_TYPE_COMMAND, "Calculator", cmdCalculatorPage, NULL },
  { MENU_ITEM_TYPE_END_OF_MENU, "", NULL, NULL }
};

//===============================================================================
//  Main
//===============================================================================
void loop() {
  // display the menu, then execute commands selected by the user
  ui.displayAndExecuteMenu(mainMenu);
}

// ---------------------------------------------------------------------------------
//                            Commands executed from the main menu
// ---------------------------------------------------------------------------------

//===============================================================================
//  Audio Page
//===============================================================================

void cmdAudioPage(void) {
  BUTTON playSDAudioButton;
  BUTTON audioInputButton;

  static bool audioPlaying = false;

  // Setup audio screen graphical elements
  ui.setColorPaletteBlue();
  ui.drawTitleBarWithMenuButton("AUDIO");
  ui.clearDisplaySpace();

  // Headset volume control
  NUMBER_BOX_FLOAT headsetVolBox;
  headsetVolBox.labelText = "Headset";
  headsetVolBox.value = 0.5;
  headsetVolBox.minimumValue = 0.0;
  headsetVolBox.maximumValue = 1.0;
  headsetVolBox.stepAmount = 0.1;
  headsetVolBox.digitsRightOfDecimal = 1;
  headsetVolBox.centerX = ui.displaySpaceCenterX - 180;
  headsetVolBox.centerY = ui.displaySpaceCenterY - 75;
  headsetVolBox.width = 100;
  headsetVolBox.height = 40;
  ui.drawNumberBox(headsetVolBox);

  // Line Out level control
  NUMBER_BOX lineOutLevelBox;
  lineOutLevelBox.labelText = "Line Out";
  lineOutLevelBox.value = 20;
  lineOutLevelBox.minimumValue = 13;  // 13 is minimum value
  lineOutLevelBox.maximumValue = 31;
  lineOutLevelBox.stepAmount = 1;
  lineOutLevelBox.centerX = ui.displaySpaceCenterX - 60;
  lineOutLevelBox.centerY = ui.displaySpaceCenterY - 75;
  lineOutLevelBox.width = 100;
  lineOutLevelBox.height = 40;
  ui.drawNumberBox(lineOutLevelBox);

  // Line In Level Control
  NUMBER_BOX lineInLevelBox;
  lineInLevelBox.labelText = "Line In";
  lineInLevelBox.value = 5;
  lineInLevelBox.minimumValue = 0;
  lineInLevelBox.maximumValue = 15;
  lineInLevelBox.stepAmount = 1;
  lineInLevelBox.centerX = ui.displaySpaceCenterX + 60;
  lineInLevelBox.centerY = ui.displaySpaceCenterY - 75;
  lineInLevelBox.width = 100;
  lineInLevelBox.height = 40;
  ui.drawNumberBox(lineInLevelBox);

  // Mic In Level Control
  NUMBER_BOX micInLevelBox;
  micInLevelBox.labelText = "Mic In";
  micInLevelBox.value = 20;
  micInLevelBox.minimumValue = 0;
  micInLevelBox.maximumValue = 63;
  micInLevelBox.stepAmount = 1;
  micInLevelBox.centerX = ui.displaySpaceCenterX + 180;
  micInLevelBox.centerY = ui.displaySpaceCenterY - 75;
  micInLevelBox.width = 100;
  micInLevelBox.height = 40;
  ui.drawNumberBox(micInLevelBox);

  // Define and draw WAV file selections
  SELECTION_BOX wavFileSelectionBox;
  wavFileSelectionBox.labelText = "WAV File Select";
  wavFileSelectionBox.value = 1;
  wavFileSelectionBox.choice0Text = "SD 1";
  wavFileSelectionBox.choice1Text = "SD 2";
  wavFileSelectionBox.choice2Text = "SD 3";
  wavFileSelectionBox.choice3Text = "SD 4";
  wavFileSelectionBox.centerX = ui.displaySpaceCenterX+ 60;
  wavFileSelectionBox.centerY = 195;
  wavFileSelectionBox.width = 360;
  wavFileSelectionBox.height = 50;
  ui.drawSelectionBox(wavFileSelectionBox);

  // Define and draw Audio Play button
  playSDAudioButton = { "Play WAV", ui.displaySpaceCenterX - 180, ui.displaySpaceBottomY - 120, 100, 50 };
  ui.drawButton(playSDAudioButton);

  // Define and draw audio input selections
  SELECTION_BOX audioSelectionBox;
  audioSelectionBox.labelText = "Audio Input Select";
  audioSelectionBox.value = INPUT_SD;
  audioSelectionBox.choice0Text = "Microphone";
  audioSelectionBox.choice1Text = "Line In";
  audioSelectionBox.choice2Text = "SD Card";
  audioSelectionBox.choice3Text = "";
  audioSelectionBox.centerX = ui.displaySpaceCenterX;
  audioSelectionBox.centerY = 290;
  audioSelectionBox.width = 400;
  audioSelectionBox.height = 50;
  ui.drawSelectionBox(audioSelectionBox);

 setAudioInput(INPUT_SD);
 
  // Execute Audio Page loop until "Back" button pressed, then return to the main menu
  while (true) {

    // Keep an eye on any audio that may be playing and reset button when it ends
    if (playSdWav1.isStopped() && audioPlaying) {  // Audio finished playing
      playSDAudioButton.labelText = "Play WAV";
      ui.drawButton(playSDAudioButton);
      audioPlaying = false;
    }

    // Update touch events for all objects
    ui.getTouchEvents();

    // Adjust Headset Volume
    if (ui.checkForNumberBoxTouched(headsetVolBox)) {
      sgtl5000_1.volume(headsetVolBox.value);
      Serial.print("Headset volume = ");
      Serial.println(headsetVolBox.value);
    }
    // Adjust Line Output Level, lower number is higher gain
    if (ui.checkForNumberBoxTouched(lineOutLevelBox)) {
      sgtl5000_1.lineOutLevel(lineOutLevelBox.value);
      Serial.print("Line Out level = ");
      Serial.println(lineOutLevelBox.value);
    }
    // Adjust Line Input Level
    if (ui.checkForNumberBoxTouched(lineInLevelBox)) {
      sgtl5000_1.lineInLevel(lineInLevelBox.value);
      Serial.print("Line In level = ");
      Serial.println(lineInLevelBox.value);
    }
    // Adjust Mic Input Level
    if (ui.checkForNumberBoxTouched(micInLevelBox)) {
      sgtl5000_1.micGain(micInLevelBox.value);
      Serial.print("Mic In level = ");
      Serial.println(micInLevelBox.value);
    }
    // Update WAV file selection
    if (ui.checkForSelectionBoxTouched(wavFileSelectionBox)) {
      if (audioPlaying) {  // Already playing a file, so stop to play new selection
        playSdWav1.stop();
        switch (wavFileSelectionBox.value) {
          case 0:
            playSdWav1.play("SDTEST1.WAV");
            break;
          case 1:
            playSdWav1.play("SDTEST2.WAV");
            break;
          case 2:
            playSdWav1.play("SDTEST3.WAV");
            break;
          case 3:
            playSdWav1.play("SDTEST4.WAV");
            break;
          default:
            break;
        }
        delay(10);
      }
    }

    // Check for play audio button touch
    // If currently playing audio, stop playing audio
    if (ui.checkForButtonClicked(playSDAudioButton) && audioPlaying) {
      playSdWav1.stop();
      playSDAudioButton.labelText = "Play WAV";
      ui.drawButton(playSDAudioButton);
      audioPlaying = false;
    }
    // If no audio playing, play current selection
    else if (ui.checkForButtonClicked(playSDAudioButton) && !audioPlaying) {
      setAudioInput(INPUT_SD);
      audioSelectionBox.value = INPUT_SD;
      ui.drawSelectionBox(audioSelectionBox);
      switch (wavFileSelectionBox.value) {
        case 0:
          playSdWav1.play("SDTEST1.WAV");
          break;
        case 1:
          playSdWav1.play("SDTEST2.WAV");
          break;
        case 2:
          playSdWav1.play("SDTEST3.WAV");
          break;
        case 3:
          playSdWav1.play("SDTEST4.WAV");
          break;
        default:
          break;
      }
      delay(10);  // wait for library to parse WAV info
      // Update text on Play WAV file button
      playSDAudioButton.labelText = "Playing";
      ui.drawButton(playSDAudioButton);
      audioPlaying = true;
    }

    // Update audio input selection
    if (ui.checkForSelectionBoxTouched(audioSelectionBox)) {
        switch (audioSelectionBox.value) {
          case INPUT_MIC:
            setAudioInput(INPUT_MIC);
            break;
          case INPUT_LINEIN:
            setAudioInput(INPUT_LINEIN);
            break;
          case INPUT_SD:
            setAudioInput(INPUT_SD);
            break;
          default:
            break;
        }
      }

    // Do any cleanup required before return to main menu
    if (ui.checkForMenuButtonClicked()) {
      if (playSdWav1.isPlaying())
        playSdWav1.stop();
      ui.setColorPaletteGray();
      return;
    }
  }
}

void setAudioInput(int source) {
  // Turn off all channels on both mixers initially
  mixerLeft.gain(0, 0.0);  mixerRight.gain(0, 0.0); // Channel 0 (SD Card)
  mixerLeft.gain(1, 0.0);  mixerRight.gain(1, 0.0); // Channel 1 (Mic / Line In)
  mixerLeft.gain(2, 0.0);  mixerRight.gain(2, 0.0); // Channel 2 (Sine Wave)
  mixerLeft.gain(3, 0.0);  mixerRight.gain(3, 0.0); // Channel 3 (Unused)
  
  playSdWav1.stop();

  if (source == 0) {
    // Microphone
    Serial.println("Input: Microphone");
    sgtl5000_1.inputSelect(AUDIO_INPUT_MIC); 
    
    mixerLeft.gain(1, 1.0);  // Unmute Left Mic
    mixerRight.gain(1, 1.0); // Unmute Right Mic
  } 
  else if (source == 1) {
    // Line In
    Serial.println("Input: Line In");
    sgtl5000_1.inputSelect(AUDIO_INPUT_LINEIN);
    
    mixerLeft.gain(1, 1.0);  // Unmute Left Line-In
    mixerRight.gain(1, 1.0); // Unmute Right Line-In
  } 
  else if (source == 2) {
    // SD Card (Full Stereo)
    Serial.println("Input: SD Card");
    
    mixerLeft.gain(0, 1.0);  // Unmute Left SD Channel
    mixerRight.gain(0, 1.0); // Unmute Right SD Channel
  }
  else if (source == 3) {
    // Sine Wave (Center panned mono)
    Serial.println("Input: Sine Wave Generator");
    
    mixerLeft.gain(2, 1.0);  // Route Sine to Left Output
    mixerRight.gain(2, 1.0); // Route Sine to Right Output
  }
}
//===============================================================================
//  Scan WiFi Page
//===============================================================================

void cmdScanWifiPage(void) {
  BUTTON scanButton;
  int x = 10;
  int y = 75;
  // y_Inc provides the line spacing based upon font size.
  int y_Inc = ui.lcdGetFontHeightWithDecentersAndLineSpacing() + 5;
  // clear the screen and draw title bar showing the "Menu" button
  ui.setColorPaletteBlue();
  ui.drawTitleBarWithMenuButton("Scan WiFi Using ESP32-C3");
  ui.clearDisplaySpace();

  scanButton = { "Scan", ui.displaySpaceCenterX + 175, ui.displaySpaceTopY + 40, 100, 50 };
  ui.drawButton(scanButton);

  ui.lcdSetCursorXY(x, y);
  ui.lcdSetFont(TXT_FONT);
  ui.lcdSetFontColor(LCD_WHITE);

  // wait for the user to press the Scan button and do a WiFi scan
  // or press the "Menu" button, then return to the main menu
  //
  while (true) {

    // get touch events on the LCD
    ui.getTouchEvents();

    // Do any cleanup required before return to main menu
    if (ui.checkForMenuButtonClicked()) {
      ui.setColorPaletteGray();
      return;
    }
    if (ui.checkForButtonClicked(scanButton)) {
      y = 75;                    // Reset cursor to top of screen
      ESP32SERIAL.println("S");  // Send command to ESP32 to start scan
      Serial.println("Scan being requested");
      ui.clearDisplaySpace();  // Clear in case of previous scan
      scanButton.labelText = "Scanning";
      ui.drawButton(scanButton);  // Redraw the button
      ui.lcdSetCursorXY(x, y);    // Set initial cursor position
      delay(3000);                // Pause to allow ESP32 to start scanning
      ui.lcdSetFont(TXT_FONT);
      // Look for serial data coming back from the ESP32-C3
      if (ESP32SERIAL.available()) {
        Serial.print("Read incoming data");
        while (ESP32SERIAL.available()) {  // Print the scan data to the LCD & USB
          String returnData = ESP32SERIAL.readString();
          const char* cString = returnData.c_str();  // Convert to C style string
          ui.lcdPrint(cString);
          ui.lcdSetCursorXY(x, y += y_Inc);
          Serial.println(returnData);
        }
      }
      scanButton.labelText = "Scan";
      ui.drawButton(scanButton);  // Redraw the button
    }
  }
}

//===============================================================================
//  System Information Page
//===============================================================================
void cmdSystemInfoPage(void) {
  int x = 10;
  int y = 50;
  int y_Inc = ui.lcdGetFontHeightWithDecentersAndLineSpacing() + 5;
  // clear the screen and draw title bar showing the "MEnu" button
  ui.setColorPaletteBlue();
  ui.drawTitleBarWithMenuButton("System Information");
  ui.clearDisplaySpace();

  ui.lcdSetCursorXY(x, y);  // Set initial cursor position
  ui.lcdSetFont(TXT_FONT);  // Set initial font style and size
  ui.lcdSetFontColor(LCD_WHITE);

  // Check for PSRAM chip(s) installed on Teensy 4.1
  uint8_t size = external_psram_size;
  if (size == 0) {
    Serial.println("No PSRAM Installed");
    ui.lcdPrint("No PSRAM Installed");
  } else {
    Serial.printf("Teensy 4.1 PSRAM Memory Size = %d MB\n", size);
    ui.lcdPrint("PSRAM Memory = ");
    ui.lcdPrint(size);
    ui.lcdPrint(" MB");
  }
  ui.lcdSetCursorXY(x, y += y_Inc);

  // Check for Flash chip installed on Teensy 4.1
  LittleFS_QSPIFlash myfs_NOR;  // NOR FLASH on Teensy
  LittleFS_QPINAND myfs_NAND;   // NAND FLASH 2Gb on Teensy

  // Check for any NOR Flash chip installed.  Normally only NAND Flash is installed
  if (myfs_NOR.begin()) {
    Serial.printf("NOR Flash Memory Size = %d Mbyte / ", myfs_NOR.totalSize() / 1048576);
    Serial.printf("%d Mbit\n", myfs_NOR.totalSize() / 131072);
    uint32_t flashSize = myfs_NOR.totalSize();
    ui.lcdPrint("NOR Flash Memory Size = ");
    ui.lcdPrint(int(flashSize / 1048576));
    ui.lcdPrint(" MB");
  }
  // Check for NAND Flash chip installed
  else if (myfs_NAND.begin()) {
    uint32_t flashSize = myfs_NAND.totalSize();
    Serial.printf("NAND Flash Memory Size =  %d bytes / ", myfs_NAND.totalSize());
    Serial.printf("%d Mbyte / ", myfs_NAND.totalSize() / 1048576);
    Serial.printf("%d Gbit\n", myfs_NAND.totalSize() * 8 / 1000000000);
    ui.lcdPrint("NAND Flash Memory Size = ");
    ui.lcdPrint(int(flashSize / 1048576));
    ui.lcdPrint(" MB / ");
    ui.lcdPrint(int(flashSize * 8 / 1000000000));
    ui.lcdPrint(" Gbit");
  } else {
    Serial.printf("No Flash Installed\n");
    ui.lcdPrint("No Flash Installed\n");
  }
  ui.lcdSetCursorXY(x, y += y_Inc);
  // tft.println();

  // Check for SD card installed in Teensy 4.1 card slot
  if (!(SD.begin(SDCARD_CS_PIN))) {
    Serial.println("SD card not found");
    ui.lcdPrint("SD card not found");
    sDCardInstalled = false;
  } else {
    Serial.println("SD card is Inserted");
    ui.lcdPrint("SD card is Inserted");
    sDCardInstalled = true;
  }
  ui.lcdSetCursorXY(x, y += y_Inc);

  // Check for ESP32-C3 installed
  while (ESP32SERIAL.available()) {  // Clear ESP32 input buffer before CMD
    ESP32SERIAL.read();
  }
  // bool esp32SAttachedLast = esp32SAttached;

  ESP32SERIAL.print("?");                          // Ask ESP32-C3 if it is there
  delay(100);                                      // Wait a bit for ESP32 to respond
  if (ESP32SERIAL.available()) {                   // If there is a response
    String returnData = ESP32SERIAL.readString();  // Read response
    if (returnData == 'Y') {                       // ESP32-C3 responded with 'Y' for Yes, I'm here
      esp32SAttached = true;
      Serial.println("ESP32-C3 was found");
      ui.lcdPrint("ESP32-C3 was found");
    } else {                                      // No response or invalid response
      Serial.println("ESP32-C3 not found");
      ui.lcdPrint("ESP32-C3 not found");
      esp32SAttached = false;
    }
  } else {
    esp32SAttached = false;
  }
  ui.lcdSetCursorXY(x, y += y_Inc);
  
  // Check for NOR Flash on baseboard
  if (!SerialFlash.begin(SPI1, FLASH_CS)) {
    Serial.println(F("Unable to access SPI Flash chip"));
    ui.lcdPrint("Unable to access SPI Flash Chip");
  } else {
    unsigned char id[5];
    SerialFlash.readID(id);
    Serial.print("ID=");

    Serial.printf("ID: %02X %02X %02X\n", id[0], id[1], id[2]);
    unsigned long sizeFlash = SerialFlash.capacity(id);

    if (sizeFlash > 0) {
      Serial.print("SPI1 NOR Flash Memory has ");
      Serial.print(sizeFlash / 1000000);
      Serial.println(" MB");
      ui.lcdPrint("SPI NOR Flash Memory Size = ");
      ui.lcdPrint(int(sizeFlash / 1000000));
      ui.lcdPrint(" MB");
    }
  }
  ui.lcdSetCursorXY(x, y += y_Inc);

  // Check for Ethernet cable connected
  auto link = Ethernet.linkStatus();
  Serial.print("Ethernet link status: ");
  ui.lcdPrint("Ethernet link Status: ");
  switch (link) {
    case Unknown:
      Serial.println("Unknown");
      ui.lcdPrint("Unknown");
      break;
    case LinkON:
      Serial.println("ON");
      ui.lcdPrint("ON");
      break;
    case LinkOFF:
      Serial.println("OFF");
      ui.lcdPrint("OFF");
      break;
  }

  ui.lcdSetCursorXY(x, y += y_Inc);

  // Check for USB Flash Drive attached
  myusb.Task();
  if (!myFiles) {
    Serial.println("USB Flash Drive not connected");
    ui.lcdPrint("USB Flash Drive is not connected");
  } else {
    Serial.println("USB Flash Drive is connected");
    ui.lcdPrint("USB Flash Drive is connected");
  }
  ui.lcdSetCursorXY(x, y += y_Inc);

  // Check CPU internal temperature
  Serial.print(tempmonGetTemp());
  Serial.println("°C");
  ui.lcdPrint("CPU Temp: ");
  ui.lcdPrint(tempmonGetTemp(), 1);
  ui.lcdPrint("°C");

  // wait for the user to press the "OK" button, then return to the main menu
  //
  while (true) {

    // get touch events on the LCD
    ui.getTouchEvents();

    // Do any cleanup required before return to main menu
    if (ui.checkForMenuButtonClicked()) {
      ui.setColorPaletteGray();
      return;
    }
  }
}

//===============================================================================
//  USB Host Page
//      Check Flash Drive button
//      Read Mouse button??
//===============================================================================

void cmdUsbHostPage(void) {
  BUTTON usbHostButton;
  
  // Cursor coordinates
  int x = 20;
  int y = 75;
  
  // y_Inc sets the line spacing for printing text based on font size
  int y_Inc = ui.lcdGetFontHeightWithDecentersAndLineSpacing() + 5;
  
  // clear the screen and draw title bar showing with the "Menu" button
  ui.setColorPaletteBlue();
  ui.drawTitleBarWithMenuButton("USB Host - Insert Flash Drive");
  ui.clearDisplaySpace();

  usbHostButton = { "Check Drive", ui.displaySpaceCenterX + 150, ui.displaySpaceTopY + 40, 150, 50 };
  ui.drawButton(usbHostButton);

  ui.lcdSetCursorXY(x, y);  // Set initial cursor position
  ui.lcdSetFont(TXT_FONT);  // Set initial font style and size
  ui.lcdSetFontColor(LCD_WHITE);

  // Check for presence of drive
  if (!myFiles) {
    ui.lcdPrint("Insert USB Drive");
    ui.lcdSetCursorXY(x, y += y_Inc);
  }
  while (!myFiles) {
    myusb.Task();         // Keep checking for drive
    ui.getTouchEvents();  // Also watch for menu button
    // Do any cleanup required before return to main menu
    if (ui.checkForMenuButtonClicked()) {
      ui.setColorPaletteGray();
      return;
    }
  }

  char buffer[300];  // Buffer to hold formatted string
  ui.lcdPrint("Drive Initialized");
  ui.lcdSetCursorXY(x, y += y_Inc);  // Used as linefeed
  ui.lcdPrint("Device Info:");
  ui.lcdSetCursorXY(x, y += y_Inc);

  sprintf(buffer, ("Sector Count: %ld\n"), myDrive.msDriveInfo.capacity.Blocks);
  ui.lcdPrint(buffer);
  ui.lcdSetCursorXY(x, y += y_Inc);
  sprintf(buffer, ("Sector size: %ld\n"), myDrive.msDriveInfo.capacity.BlockSize);
  ui.lcdPrint(buffer);
  ui.lcdSetCursorXY(x, y += y_Inc);

  uint64_t drivesize = myDrive.msDriveInfo.capacity.Blocks;
  drivesize *= myDrive.msDriveInfo.capacity.BlockSize;
  ui.lcdPrint("Disk Capacity: ");
  ui.lcdPrint(drivesize, 1);
  ui.lcdPrint(" Bytes");
  ui.lcdSetCursorXY(x, y += y_Inc);

  sprintf(buffer, ("VendorID: %8.8s\n"), myDrive.msDriveInfo.inquiry.VendorID);
  ui.lcdPrint(buffer);
  ui.lcdSetCursorXY(x, y += y_Inc);
  sprintf(buffer, ("ProductID: %16.16s"), myDrive.msDriveInfo.inquiry.ProductID);
  ui.lcdPrint(buffer);
  ui.lcdSetCursorXY(x, y += y_Inc);

  // wait for the user to press the "Menu" button, then return to the main menu
  // or recursively call the routine if user presses the "Check Drive" button
  while (true) {
    // get touch events on the LCD
    ui.getTouchEvents();
    if (ui.checkForButtonClicked(usbHostButton)) {
      cmdUsbHostPage();
    }
    // Do any cleanup required before return to main menu
    if (ui.checkForMenuButtonClicked()) {
      ui.setColorPaletteGray();
      return;
    }
  }
}

//===============================================================================
//  Wired Ethernet Page
//      Check for connection status and type
//===============================================================================
void cmdEthernetPage(void) {
  BUTTON ethernetButton;
  // Cursor coordinates
  int x = 20;
  int y = 75;
  // y_Inc sets the line spacing for printing text based on font size
  int y_Inc = ui.lcdGetFontHeightWithDecentersAndLineSpacing() + 5;
  char buffer[30];  // Buffer to hold formatted string

  // clear the screen and draw title bar with the "Menu" button
  ui.setColorPaletteBlue();
  ui.drawTitleBarWithMenuButton("Wired Ethernet");
  ui.clearDisplaySpace();

  ethernetButton = { "Check Ethernet Connection", ui.displaySpaceCenterX + 125, ui.displaySpaceTopY + 40, 175, 60 };
  ui.drawButton(ethernetButton);

  // wait for the user to press the "OK" or "Check Ethernet Connection" button, then execute command or return to the main menu
  //
  while (true) {

    // get touch events on the LCD
    ui.getTouchEvents();

    // Check for Ethernet button push
    if (ui.checkForButtonClicked(ethernetButton)) {
      // Reset display space in case of multiple button pushes
      x = 20;
      y = 75;
      ui.clearDisplaySpace();
      ui.drawButton(ethernetButton);
      ui.lcdSetFont(TXT_FONT);  // Set initial font style and size
      ui.lcdSetFontColor(LCD_WHITE);
      ui.lcdSetCursorXY(x, y);  // Set initial cursor position
      auto link = Ethernet.linkStatus();
      Serial.print("Ethernet link status: ");
      ui.lcdPrint("Ethernet link Status: ");
      switch (link) {
        case Unknown:
          Serial.print("Unknown");
          ui.lcdPrint("Unknown");
          break;
        case LinkON: {
            Serial.print("ON");
            ui.lcdPrint("ON");

            // We can use driver capabilities to determine what to print
            const DriverCapabilities& dc = Ethernet.driverCapabilities();
            LinkInfo li = Ethernet.linkInfo();
            if (dc.isLinkSpeedDetectable) {
              Serial.printf(", %d Mbps", li.speed);
              ui.lcdSetCursorXY(x, y += y_Inc);
              sprintf(buffer, ("Link Speed:  %d Mbps"), li.speed);
              ui.lcdPrint(buffer);
            }
            if (dc.isLinkFullDuplexDetectable) {
              Serial.printf(", %s duplex", li.fullNotHalfDuplex ? "full" : "half");
              ui.lcdSetCursorXY(x, y += y_Inc);
              sprintf(buffer, ("Link Type:  %s duplex"), li.fullNotHalfDuplex ? "full" : "half");
              ui.lcdPrint(buffer);
            }
            if (dc.isLinkCrossoverDetectable) {
              Serial.printf(", %s crossover", li.isCrossover ? "is" : "not");
              ui.lcdSetCursorXY(x, y += y_Inc);
              sprintf(buffer, ("Cable Type:  %s crossover"), li.isCrossover ? "is" : "not");
              ui.lcdPrint(buffer);
            }
            break;
          }
        case LinkOFF:
          Serial.print("OFF");
          ui.lcdPrint("OFF");
          break;
      }
      Serial.println();
    }

    // Do any cleanup required before return to main menu
    if (ui.checkForMenuButtonClicked()) {
      ui.setColorPaletteGray();
      return;
    }
  }
}
//===============================================================================
//  Mandelbrot Page
//  Draws the Mandelbrot set.  Touch screen to zoom in
//  Info button shows and clears an informational page
//===============================================================================
// Globals used with Mandelbrot
float
centerReal = -0.6,  // Image center point in complex plane
centerImag = 0.0,
rangeReal = 3.0,  // Image coverage in complex plane
rangeImag = 3.0;
const int16_t
bits = 12,         // Fractional resolution
pixelWidth = 360,  // Display dimensions
pixelHeight = 270,
iterations = 30;  //  Fractal iteration limit or 'dwell'

int64_t n, a, b, a2, b2, posReal, posImag;

void cmdMandelbrotPage(void) {
  bool infoFlag = false;
  // clear the screen and draw title bar showing with the "Menu" button
  ui.setColorPaletteBlue();
  ui.drawTitleBarWithMenuButton("Mandelbrot  (touch to zoom)");
  ui.clearDisplaySpace();

  // Draw Info button on the menu bar using extended button
  // (Center X, Center Y, Width, Height, button color, button active color, button frame color, text color, font)
  BUTTON_EXTENDED infoButton = { "Info", ui.displaySpaceRightX - 35, ui.displaySpaceTopY - 17, 60, 30, LCD_DARKBLUE, LCD_LIGHTBLUE, LCD_LIGHTBLUE, LCD_ORANGE, Arial_14 };
  ui.drawButton(infoButton);

  drawMandelbrot();

  // wait for the user to press the image to zoom or the "menu" button
  // to return to the main menu
  while (true) {
    // get touch events on the LCD
    ui.getTouchEvents();

    // If image touched, zoom in
    if (ui.checkForTouchEventInRect(TOUCH_PUSHED_EVENT, 60, 40, 420, 300) && !infoFlag) {
      // Update formula parameters and redraw the image
      rangeReal *= 0.90;  // Zoom in each iteration
      rangeImag *= 0.90;
      if (rangeReal < 1.3) {  // Reset the zoom
        rangeReal = 3.0;
        rangeImag = 3.0;
      }
      drawMandelbrot();
    }

    if (ui.checkForButtonClicked(infoButton)) {
      // Alternately draw and clear the informational screen
      if (!infoFlag) {
        ui.clearDisplaySpace();
        ui.lcdSetFont(TXT_FONT);  // Set initial font style and size
        ui.lcdSetFontColor(LCD_WHITE);
        ui.lcdSetCursorXY(10, 60);
        ui.lcdPrint("The Mandelbrot Set is a fractal, a visual representation of a");
        ui.lcdSetCursorXY(10, 80);
        ui.lcdPrint("mathematical forumula plotted on the complex plane.");
        ui.lcdSetCursorXY(10, 120);
        ui.lcdPrint("It is generated by iterating the simple quadratic equation");
        ui.lcdSetCursorXY(10, 140);
        ui.lcdPrint("[z(squared) + c] and plotting the points for which the sequence");
        ui.lcdSetCursorXY(10, 160);
        ui.lcdPrint("does not diverge to infinity.");
        ui.lcdSetCursorXY(10, 200);
        ui.lcdPrint("If the sequence remains bounded, the starting value of c is part");
        ui.lcdSetCursorXY(10, 220);
        ui.lcdPrint("of the set and colored black,while points that diverge are");
        ui.lcdSetCursorXY(10, 240);
        ui.lcdPrint("colored based on how quickly they escape to infinity");
        ui.lcdSetCursorXY(10, 280);
        ui.lcdPrint("Press 'Info' to close");
        infoFlag = true;
      } else {
        ui.clearDisplaySpace();
        drawMandelbrot();
        infoFlag = false;
      }
    }
    // Do any cleanup required before return to main menu
    if (ui.checkForMenuButtonClicked()) {
      ui.setColorPaletteGray();
      return;
    }
  }
}
//
// Draw Mandelbrot image
//
void drawMandelbrot(void) {
  int32_t
  startReal = (int64_t)((centerReal - rangeReal * 0.5) * (float)(1 << bits)),
  startImag = (int64_t)((centerImag + rangeImag * 0.5) * (float)(1 << bits)),
  incReal = (int64_t)((rangeReal / (float)pixelWidth) * (float)(1 << bits)),
  incImag = (int64_t)((rangeImag / (float)pixelHeight) * (float)(1 << bits));

  posImag = startImag;
  for (int y = 0; y < pixelHeight; y++) {
    posReal = startReal;
    for (int x = 0; x < pixelWidth; x++) {
      a = posReal;
      b = posImag;
      for (n = iterations; n > 0; n--) {
        a2 = (a * a) >> bits;
        b2 = (b * b) >> bits;
        if ((a2 + b2) >= (4 << bits))
          break;
        b = posImag + ((a * b) >> (bits - 1));
        a = posReal + a2 - b2;
      }
      ui.lcdDrawPixel(x + 60, y + 40, (n * 29) << 8 | (n * 67));  // Change 29/67 to affect colors
      posReal += incReal;
    }
    posImag -= incImag;
  }
}

//===============================================================================
//  RGB LED Page
//  Adjust RGB LED and color panel using RGB sliders
//===============================================================================
sliderM redLevel("redLevel", sliderM::SLIDER_MODE::SLIDER_MODE_HORIZONTAL);
sliderM greenLevel("greenLevel", sliderM::SLIDER_MODE::SLIDER_MODE_HORIZONTAL);
sliderM blueLevel("blueLevel", sliderM::SLIDER_MODE::SLIDER_MODE_HORIZONTAL);

// Pins used for RGB LED
#define RGB_R_PIN 4
#define RGB_G_PIN 5
#define RGB_B_PIN 6

int rgbTextx = 20;
int rgbTexty = 65;

void cmdRgbLedPage(void) {
  BUTTON ledButton;
  bool ledState = true;

  // y_Inc sets the line spacing for printing text
  int y_Inc = ui.lcdGetFontHeightWithDecentersAndLineSpacing() + 5;
  // clear the screen and draw title bar showing Menu button
  ui.setColorPaletteBlue();
  ui.drawTitleBarWithMenuButton("RGB LED");
  ui.clearDisplaySpace();

  // Draw sliders
  redLevel.init(330, 140, 190, 25, true, true);
  redLevel.setAxis(0.0, 255.0, 16, 2);
  redLevel.setBump(1.0, true, true, true);
  redLevel.setHandleColors(LCD_RED, LCD_RED);
  redLevel.setSliderColors(LCD_WHITE, LCD_BLACK, LCD_MDGREY, LCD_RED);
  redLevel.setSliderPrint(3, 0, false);
  redLevel.setValue(0.0);
  redLevel.drawSlider();

  greenLevel.init(330, 210, 190, 25, true, true);
  greenLevel.setAxis(0.0, 255.0, 16, 2);
  greenLevel.setSliderPrint(3, 0, false);
  greenLevel.setValue(0.0);
  greenLevel.drawSlider();

  blueLevel.init(330, 280, 190, 25, true, true);
  blueLevel.setAxis(0.0, 255.0, 16, 2);
  blueLevel.setBump(1.0, true, true, true);
  blueLevel.setHandleColors(LCD_BLUE, LCD_BLUE);
  blueLevel.setSliderColors(LCD_WHITE, LCD_BLACK, LCD_MDGREY, LCD_BLUE);
  blueLevel.setSliderPrint(3, 0, false);
  blueLevel.setValue(0.0);
  blueLevel.drawSlider();

  // Draw color box outline
  ui.lcdDrawRectangle(18, 130, 153, 163, LCD_BLUE);
  // Draw LED on/off button
  ledButton = { "LED On", ui.displaySpaceCenterX + 175, ui.displaySpaceTopY + 40, 100, 50 };
  ui.drawButton(ledButton);

  // Draw initial color value text
  ui.lcdSetCursorXY(rgbTextx, rgbTexty);  // Set initial cursor position
  ui.lcdSetFont(TXT_FONT);                // Set initial font style and size
  ui.lcdSetFontColor(LCD_WHITE);
  ui.lcdPrint("24-bit RGB888: 0x000000");
  ui.lcdSetCursorXY(rgbTextx, rgbTexty + y_Inc);
  ui.lcdPrint("16-bit RGB565: 0x0000");

  //Setup RGB pins as outputs and drive LOW to keep them off
  pinMode(RGB_R_PIN, OUTPUT);
  pinMode(RGB_G_PIN, OUTPUT);
  pinMode(RGB_B_PIN, OUTPUT);
  digitalWrite(RGB_R_PIN, LOW);
  digitalWrite(RGB_G_PIN, LOW);
  digitalWrite(RGB_B_PIN, LOW);

  // Wait for user to provide input or press the Menu button to return to the main menu
  while (true) {

    // get touch events on the LCD
    ui.getTouchEvents();

    // Update sliders
    if (redLevel.checkSliderT()) {
      redLevel.drawSlider();
      updateColorPanel(ledState);
    }
    if (greenLevel.checkSliderT()) {
      greenLevel.drawSlider();
      updateColorPanel(ledState);
    }
    if (blueLevel.checkSliderT()) {
      blueLevel.drawSlider();
      updateColorPanel(ledState);
    }
    // Check LED on/off button
    if (ui.checkForButtonClicked(ledButton)) {
      ledState = !ledState;
      if (ledState == true) {
        ledButton.labelText = "LED On";
        // For reasons unknow, need to redraw the button on first button press
        // or text is not updated
        ui.drawButton(ledButton);
        updateColorPanel(ledState);
      } else {
        ledButton.labelText = "LED Off";
        ui.drawButton(ledButton);
        updateColorPanel(ledState);
      }
    }
    // Do any cleanup required before return to main menu
    if (ui.checkForMenuButtonClicked()) {
      // Turn off RGB LED
      analogWrite(RGB_R_PIN, 0);
      analogWrite(RGB_G_PIN, 0);
      analogWrite(RGB_B_PIN, 0);
      ui.lcdSetFont(TXT_FONT);
      ui.setColorPaletteGray();
      return;
    }
  }
}
// Subroutine to handle updating the color panel, RGB LED and the text fields
void updateColorPanel(bool ledOn) {
  char buffer[8];
  ui.lcdSetFont(TXT_FONTBOLD);
  int y_Inc = ui.lcdGetFontHeightWithDecentersAndLineSpacing() + 5;

  // Get current slider values and update color panel
  byte r = redLevel.getValue();
  byte g = greenLevel.getValue();
  byte b = blueLevel.getValue();

  // Convert to 565 16-bit color format
  uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  ui.lcdDrawFilledRectangle(20, 132, 150, 160, color);

  // Update the color values for 24-bit RGB888 and 16-bit RGB565
  ui.lcdDrawFilledRectangle(130, 65, 100, 60, LCD_BLACK);
  ui.lcdSetCursorXY(rgbTextx + 115, 65);
  // Format text and put into temporary 'buffer' for printing to the LCD
  sprintf(buffer, "%02X%02X%02X", r, g, b);
  ui.lcdPrint(buffer);
  ui.lcdSetCursorXY(rgbTextx + 115, 65 + y_Inc);
  sprintf(buffer, "%X", color);
  ui.lcdPrint(buffer);

  // Update LED if it is enabled or turn it off
  if (ledOn) {
    analogWrite(RGB_R_PIN, r);
    analogWrite(RGB_G_PIN, g);
    analogWrite(RGB_B_PIN, b);
  } else {
    analogWrite(RGB_R_PIN, 0);
    analogWrite(RGB_G_PIN, 0);
    analogWrite(RGB_B_PIN, 0);
  }
}

//===============================================================================
//  Calculator Page
//===============================================================================
// Globals used with Calculator
const int MAX_CHARACTERS = 20;
char calcValueStr[MAX_CHARACTERS + 15];
boolean opFlag = false;      // Flags if there is an operator in the string
boolean equalsFlag = false;  // Flags if result is displayed

void cmdCalculatorPage(void) {

  const int BUTTON_W = 80;
  const int BUTTON_H = 43;
  boolean firstFlg = true;
  float num1;
  float num2;

  // clear the screen and draw title bar with the "Menu" button
  ui.drawTitleBarWithMenuButton("");
  ui.clearDisplaySpace(0x0861);  // Dark grey
  ui.setMenuFont(Arial_18_Bold);

  // Draw data display box
  ui.lcdDrawFilledRoundedRectangle(ui.displaySpaceCenterX - 164, ui.displaySpaceTopY + 5, 328, 50, 10, LCD_BLUE);

  // Define Calculator buttons
  BUTTON buttonClear = { "C", ui.displaySpaceCenterX - 123, ui.displaySpaceTopY + 81, BUTTON_W, BUTTON_H };
  BUTTON buttonDivide = { "/", ui.displaySpaceCenterX + 123, ui.displaySpaceTopY + 81, BUTTON_W, BUTTON_H };
  BUTTON button7 = { "7", ui.displaySpaceCenterX - 123, ui.displaySpaceTopY + 126, BUTTON_W, BUTTON_H };
  BUTTON button8 = { "8", ui.displaySpaceCenterX - 41, ui.displaySpaceTopY + 126, BUTTON_W, BUTTON_H };
  BUTTON button9 = { "9", ui.displaySpaceCenterX + 41, ui.displaySpaceTopY + 126, BUTTON_W, BUTTON_H };
  BUTTON buttonMultiply = { "x", ui.displaySpaceCenterX + 123, ui.displaySpaceTopY + 126, BUTTON_W, BUTTON_H };
  BUTTON button4 = { "4", ui.displaySpaceCenterX - 123, ui.displaySpaceTopY + 171, BUTTON_W, BUTTON_H };
  BUTTON button5 = { "5", ui.displaySpaceCenterX - 41, ui.displaySpaceTopY + 171, BUTTON_W, BUTTON_H };
  BUTTON button6 = { "6", ui.displaySpaceCenterX + 41, ui.displaySpaceTopY + 171, BUTTON_W, BUTTON_H };
  BUTTON buttonMinus = { "-", ui.displaySpaceCenterX + 123, ui.displaySpaceTopY + 171, BUTTON_W, BUTTON_H };
  BUTTON button1 = { "1", ui.displaySpaceCenterX - 123, ui.displaySpaceTopY + 216, BUTTON_W, BUTTON_H };
  BUTTON button2 = { "2", ui.displaySpaceCenterX - 41, ui.displaySpaceTopY + 216, BUTTON_W, BUTTON_H };
  BUTTON button3 = { "3", ui.displaySpaceCenterX + 41, ui.displaySpaceTopY + 216, BUTTON_W, BUTTON_H };
  BUTTON buttonPlus = { "+", ui.displaySpaceCenterX + 123, ui.displaySpaceTopY + 216, BUTTON_W, BUTTON_H };
  BUTTON buttonDelete = { "<<", ui.displaySpaceCenterX - 123, ui.displaySpaceTopY + 261, BUTTON_W, BUTTON_H };
  BUTTON button0 = { "0", ui.displaySpaceCenterX - 41, ui.displaySpaceTopY + 261, BUTTON_W, BUTTON_H };
  BUTTON buttonDot = { ".", ui.displaySpaceCenterX + 41, ui.displaySpaceTopY + 261, BUTTON_W, BUTTON_H };
  BUTTON buttonEquals = { "=", ui.displaySpaceCenterX + 123, ui.displaySpaceTopY + 261, BUTTON_W, BUTTON_H };

  // Draw Calculator buttons
  ui.drawButton(buttonClear);
  ui.drawButton(buttonDivide);
  ui.drawButton(button7);
  ui.drawButton(button8);
  ui.drawButton(button9);
  ui.drawButton(buttonMultiply);
  ui.drawButton(button4);
  ui.drawButton(button5);
  ui.drawButton(button6);
  ui.drawButton(buttonMinus);
  ui.drawButton(button1);
  ui.drawButton(button2);
  ui.drawButton(button3);
  ui.drawButton(buttonPlus);
  ui.drawButton(buttonDelete);
  ui.drawButton(button0);
  ui.drawButton(buttonDot);
  ui.drawButton(buttonEquals);

  ui.lcdSetCursorXY(ui.displaySpaceCenterX - 62, ui.displaySpaceTopY + 70);
  ui.lcdPrint("Calculator");
  ui.lcdSetFont(TXT_FONT);

  // Wait for user to provide input or press the Menu button to return to the main menu
  while (true) {
    // get touch events on the LCD
    ui.getTouchEvents();

    // Check for numbers being entered
    if (ui.checkForButtonClicked(button0)) calc_AddCharToStringBuf('0', firstFlg);
    if (ui.checkForButtonClicked(button1)) calc_AddCharToStringBuf('1', firstFlg);
    if (ui.checkForButtonClicked(button2)) calc_AddCharToStringBuf('2', firstFlg);
    if (ui.checkForButtonClicked(button3)) calc_AddCharToStringBuf('3', firstFlg);
    if (ui.checkForButtonClicked(button4)) calc_AddCharToStringBuf('4', firstFlg);
    if (ui.checkForButtonClicked(button5)) calc_AddCharToStringBuf('5', firstFlg);
    if (ui.checkForButtonClicked(button6)) calc_AddCharToStringBuf('6', firstFlg);
    if (ui.checkForButtonClicked(button7)) calc_AddCharToStringBuf('7', firstFlg);
    if (ui.checkForButtonClicked(button8)) calc_AddCharToStringBuf('8', firstFlg);
    if (ui.checkForButtonClicked(button9)) calc_AddCharToStringBuf('9', firstFlg);

    // check if the "dot" button pressed
    // only allow one dot/decimal point per number
    if (ui.checkForButtonClicked(buttonDot)) {
      int len = strlen(calcValueStr);
      int dotCount = 0;
      for (int i = 0; i <= len; i++) {
        if (calcValueStr[i] == '.')
          dotCount++;
      }
      if (dotCount == 0 || (dotCount == 1 && opFlag))
        calc_AddCharToStringBuf('.', firstFlg);
    }

    // Check for Operator buttons, allow only one to be entered.
    if (ui.checkForButtonClicked(buttonDivide)) {
      handleOperatorButton('/', firstFlg);
    }
    if (ui.checkForButtonClicked(buttonMultiply)) {
      handleOperatorButton('x', firstFlg);
    }
    if (ui.checkForButtonClicked(buttonMinus)) {
      handleOperatorButton('-', firstFlg);
    }
    if (ui.checkForButtonClicked(buttonPlus)) {
      handleOperatorButton('+', firstFlg);
    }

    // Check for equal sign and show result on new line
    if (ui.checkForButtonClicked(buttonEquals)) {
      calc_AddCharToStringBuf('=', firstFlg);
      // Find the operator used
      char operatorFound = ' ';
      int operatorIndex = -1;
      int len = strlen(calcValueStr);
      equalsFlag = true;

      for (int i = 0; i < len; i++) {
        // Check for common operators
        if (calcValueStr[i] == '/' || calcValueStr[i] == 'x' || calcValueStr[i] == '-' || calcValueStr[i] == '+') {
          operatorFound = calcValueStr[i];
          operatorIndex = i;
          break;  // Found the operator, exit the loop
        }
      }

      if (operatorIndex != -1) {
        // Temporarily null-terminate the first number string
        char temp = calcValueStr[operatorIndex];
        calcValueStr[operatorIndex] = '\0';
        // Convert string to number
        num1 = atof(calcValueStr);
        // The second number starts right after the operator
        num2 = atof(&calcValueStr[operatorIndex + 1]);

        // Restore the original operator character for potential future use
        calcValueStr[operatorIndex] = temp;

        Serial.println(num1);
        Serial.println(num2);
        Serial.print("Operator: ");
        Serial.println(operatorFound);

        float finalResult = 0.0f;
        bool error = false;

        switch (operatorFound) {
          case '/':
            if (num2 != 0.0f) {
              finalResult = num1 / num2;
            } else {
              Serial.println("Error: Division by zero");
              error = true;
            }
            break;
          case 'x':
            finalResult = num1 * num2;
            break;
          case '-':
            finalResult = num1 - num2;
            break;
          case '+':
            finalResult = num1 + num2;
            break;
        }

        if (!error) {
          Serial.print("Result: ");
          Serial.println(finalResult);
          calc_DisplayResult(finalResult);
          equalsFlag = true;
        }
      }
    }

    // if delete button pressed, delete the last character
    if (ui.checkForButtonClicked(buttonDelete)) {
      int i = strlen(calcValueStr);
      if (i > 0 && calcValueStr[i] != '=') calcValueStr[i - 1] = '\0';
      calc_DisplayValueInStringBuf();
      firstFlg = false;
      equalsFlag = false;
    }

    // if Clear button pressed, clear the screen and values
    if (ui.checkForButtonClicked(buttonClear)) {
      calcValueStr[0] = '\0';
      ui.lcdDrawFilledRoundedRectangle(ui.displaySpaceCenterX - 164, ui.displaySpaceTopY + 5, 328, 50, 10, LCD_BLUE);
      firstFlg = true;
      opFlag = false;
      equalsFlag = false;
    }

    // Do any cleanup required before return to main menu
    if (ui.checkForMenuButtonClicked()) {
      ui.setColorPaletteGray();
      ui.setMenuFont(Arial_14_Bold);
      return;
    }
  }
}

//
// Function to handle operator button presses
//
void handleOperatorButton(char operatorChar, bool& firstFlg) {
  int len = strlen(calcValueStr);

  // Recheck string for previous operator in case we backspaced over it
  bool operatorPresent = false;
  for (int i = 0; i < len; i++) {
    if (calcValueStr[i] == '/' || calcValueStr[i] == 'x' || calcValueStr[i] == '-' || calcValueStr[i] == '+') {
      operatorPresent = true;
      break;  // Exit the loop as soon as an operator is found
    }
  }

  // Check conditions before adding the character
  if (!operatorPresent && len != 0) {
    opFlag = true;
    calc_AddCharToStringBuf(operatorChar, firstFlg);
    Serial.print("Added operator: ");
    Serial.println(operatorChar);
  }
}

//
// Function to print the "value" as the user is entering it
// calcValueStr contains c string representation of the value
//
void calc_DisplayValueInStringBuf(void) {
  ui.lcdDrawFilledRoundedRectangle(ui.displaySpaceCenterX - 164, ui.displaySpaceTopY + 5, 328, 50, 10, LCD_BLUE);
  ui.lcdSetCursorXY(ui.displaySpaceCenterX, ui.displaySpaceTopY + 10);
  ui.lcdPrintCentered(calcValueStr);
}

//
// Function to display result after '=' entered
//
void calc_DisplayResult(float result) {
  char buffer[16];
  ui.lcdSetCursorXY(ui.displaySpaceCenterX, ui.displaySpaceTopY + 35);
  sprintf(buffer, "%g\n", result);  // Remove any trailing zeros
  ui.lcdPrintCentered(buffer);
}

//
// Function to add a character to the string buffer
//
void calc_AddCharToStringBuf(char c, boolean& firstCharEntered) {
  int len = strlen(calcValueStr);
  if (len >= MAX_CHARACTERS || equalsFlag)
    return;

  if (firstCharEntered == true)
    len = 0;
  firstCharEntered = false;

  calcValueStr[len] = c;
  calcValueStr[len + 1] = 0;

  calc_DisplayValueInStringBuf();
}
//===============================================================================
//  Tetris Page
//  Does not use the TeensyUserInterface display wrapper for best performance
//  Uses direct tft drawing commands in order to use display frame buffer.
//  Also uses direct control of touch.
//  Exit returns to main menu
//===============================================================================
//  Game Constants & Globals ---
const uint16_t colors[] = {0x0000, 0x07FF, 0xFFE0, 0xF81F, 0xFD20, 0x001F, 0x07E0, 0xF800};
const int OFF_X = 10, OFF_Y = 10, B_SIZE = 15; // Grid offset and block size
int board[10][20] = {0};                        // 10x20 Game board array
long score = 0, highScore = 0;
int curPiece, curX, curY, curRot;               // Atributes of current falling piece
unsigned long lastDrop = 0;                     // Timer for automatic falling

// Tetrominoes defined as 4x4 bitmasks (Hex) for 4 rotations each
// For example 0x0F00 represents a horizontal line, 0x4444 a vertical line in a 4x4 grid.
// 0x0F00   0x4444
// ------   ------
// 0000     0100
// 1111     0100
// 0000     0100
// 0000     0100
const uint16_t shapes[7][4] = {
  {0x0F00, 0x4444, 0x0F00, 0x4444}, // I
  {0x0660, 0x0660, 0x0660, 0x0660}, // O
  {0x0E40, 0x4C40, 0x4E00, 0x4640}, // T
  {0x0E80, 0xC440, 0x2E00, 0x88C0}, // L
  {0x0E20, 0x44C0, 0x8E00, 0xC880}, // J
  {0x06C0, 0x8C40, 0x6C00, 0x8C40}, // S
  {0x0C60, 0x4C80, 0x0C60, 0x4C80}  // Z
};

// ----------------------------------------------------------------------------
// Main Tetris entry point
// ----------------------------------------------------------------------------
bool updateTFT;
void cmdTetrisPage(void) {
  ui.lcdClearScreen(0x0000);

  EEPROM.get(0, highScore);
  if (highScore < 0) highScore = 0;
  tft.useFrameBuffer(true);  // Enables flicker-free rendering
  setAudioInput(INPUT_SINE); 
  
  randomSeed(analogRead(0)); // Seed RNG using floating analog noise
  newPiece();

  while (true) {
    updateTFT = false;

    // --- Input Handling: Physical Buttons ---
    if (digitalRead(BTN_L) == LOW && !checkCollision(curX - 1, curY, curRot)) {
      curX--; playSound(440, 10); delay(100);
    }
    if (digitalRead(BTN_R) == LOW && !checkCollision(curX + 1, curY, curRot)) {
      curX++; playSound(440, 10); delay(100);
    }
    if (digitalRead(BTN_ROT) == LOW) {
      int nr = (curRot + 1) % 4;
      if (!checkCollision(curX, curY, nr)) {
        curRot = nr;
        playSound(880, 15);
      }
      delay(150);
    }

    // --- Input Handling: Touch Zones ---
    if (ctp.touched()) {
      TS_Point p = ctp.getPoint();
      int tx = 480 - p.y, ty = p.x; // Map touch coordinates to screen rotation
      if (tx > 160 && tx < 320) {               // Left side of screen
        if (ty > 100 && ty < 200 && !checkCollision(curX - 1, curY, curRot)) {
          curX--;
          playSound(440, 10);
        }
        else if (ty > 210) {        // Bottom Left = Hard Drop
          while (!checkCollision(curX, curY + 1, curRot)) curY++;
          playSound(200, 40); lockPiece();
        }
      } else if (tx > 320) {        // Right side of screen
        if (ty < 80) {              // Exit from Tetris routine
          // Do any cleanup before returning to main menu
          tft.useFrameBuffer(false);
          patchCord1.disconnect();
          patchCord2.disconnect();
          patchCord1.connect(playSdWav1, 0, audioOutput1, 0);
          patchCord2.connect(playSdWav1, 0, audioOutput1, 1);
          memset(board, 0, sizeof(board)); // Game Over: Clear board
          score = 0;
          ui.setColorPaletteGray();
          delay(50);
          return;
        }               // Add exit from routine
        else if (ty > 100 && ty < 200 && !checkCollision(curX + 1, curY, curRot)) {
          curX++;
          playSound(440, 10);
        }
        else if (ty > 210) {        // Bottom Right = Rotate
          int nr = (curRot + 1) % 4;
          if (!checkCollision(curX, curY, nr)) {
            curRot = nr;
            playSound(880, 15);
          }
        }
      }

      delay(130);
    }

    // --- Gravity: Automatic Fall ---
    if (millis() - lastDrop > 600) {
      updateTFT = true;
      if (!checkCollision(curX, curY + 1, curRot)) curY++;
      else {
        playSound(300, 20);
        lockPiece();
      }
      lastDrop = millis();
    }

    // --- Rendering ---
    if ( updateTFT ) {
      tft.fillScreen(0x0000);
      drawHUD(); // Draw scores and UI buttons

      tft.drawRect(OFF_X - 1, OFF_Y - 1, 152, 302, 0xFFFF); // Draw board border

      // Draw static blocks on the board
      for (int x = 0; x < 10; x++)
        for (int y = 0; y < 20; y++)
          if (board[x][y]) tft.fillRect(OFF_X + x * 15, OFF_Y + y * 15, 14, 14, colors[board[x][y]]);

      // Draw the currently falling piece
      for (int i = 0; i < 16; i++)
        // Check each 'pixel' of the 4x4 tetromino mask for collision
        if (shapes[curPiece][curRot] & (0x8000 >> i))
          tft.fillRect(OFF_X + (curX + i % 4) * 15, OFF_Y + (curY + i / 4) * 15, 14, 14, colors[curPiece + 1]);

      tft.updateScreen(); // Push frame buffer to display all at once to avoid screen flicker
    }
  }
}
// Helper to play short "beeps" without blocking game flow
void playSound(int freq, int dur) {
  bool updateTFT;
  updateTFT = true;
  sine1.frequency(freq);
  sine1.amplitude(0.3);
  delay(dur);
  sine1.amplitude(0);
}

// Check if a piece at (nx, ny) with rotation (nr) hits walls or other blocks
bool checkCollision(int nx, int ny, int nr) {
  for (int i = 0; i < 16; i++) {
    if (shapes[curPiece][nr] & (0x8000 >> i)) { // Extract bits from the 4x4 mask
      int px = nx + (i % 4), py = ny + (i / 4);
      if (px < 0 || px >= 10 || py >= 20 || (py >= 0 && board[px][py])) return true;
    }
  }
  return false;
}

// Spawns a random piece at the top; resets game if it collides immediately
void newPiece() {
  curPiece = random(7); curX = 3; curY = 0; curRot = 0;
  if (checkCollision(curX, curY, curRot)) {
    memset(board, 0, sizeof(board)); // Game Over: Clear board
    score = 0;
    playSound(150, 300);
    delay(1000);
  }
}

// Transfers the falling piece to the 'board' array and checks for full lines
void lockPiece() {
  for (int i = 0; i < 16; i++)
    if (shapes[curPiece][curRot] & (0x8000 >> i))
      board[curX + (i % 4)][curY + (i / 4)] = curPiece + 1;

  // Line clearing logic
  for (int y = 19; y >= 0; y--) {
    bool full = true;
    for (int x = 0; x < 10; x++) if (!board[x][y]) full = false;
    if (full) {
      score += 100; playSound(600, 40);
      // Shift everything above this line down
      for (int ty = y; ty > 0; ty--) for (int tx = 0; tx < 10; tx++) board[tx][ty] = board[tx][ty - 1];
      y++; // Re-check the same row index
    }
  }

  if (score > highScore) {
    highScore = score;
    EEPROM.put(0, highScore); // Save new high score to permanent memory
  }
  newPiece();
}

void drawHUD() {
  tft.setTextSize(2);
  // Draws the visual indicators for the touch buttons and displays the scores

  // LEFT COLUMN
  // Top Left: SCORE
  tft.setCursor(175, 20); tft.setTextColor(0x07FF); // Cyan
  tft.print("SCORE:");
  tft.setCursor(175, 45); tft.print(score);

  // Center Left: MOVE LEFT X, Y, Width, Height, Radius, Color
  tft.drawRoundRect(175, 100, 140, 100, 8, 0x52AA);
  tft.setCursor(215, 152); tft.setTextColor(0xFFFF); tft.print("LEFT");

  // Bottom Left: HARD DROP
  tft.drawRoundRect(175, 210, 140, 100, 8, 0xF800);
  tft.setCursor(215, 252); tft.setTextColor(0xF800); tft.print("DROP");

  // RIGHT COLUMN
  // Top Right: HIGH SCORE
  tft.setCursor(280, 20); tft.setTextColor(0xFFE0); // Yellow
  tft.print("HIGH:");
  tft.setCursor(280, 45); tft.print(highScore);

  // Top Right Right: EXIT
  tft.drawRoundRect(390, 10, 80, 60, 8, 0x07FF);
  tft.setCursor(410, 35); tft.setTextColor(0x07FF); tft.print("EXIT");

  // Center Right: MOVE RIGHT
  tft.drawRoundRect(330, 100, 140, 100, 8, 0x52AA);
  tft.setCursor(370, 152); tft.setTextColor(0xFFFF); tft.print("RIGHT");

  // Bottom Right: ROTATE
  tft.drawRoundRect(330, 210, 140, 100, 8, 0x07E0);
  tft.setCursor(360, 252); tft.setTextColor(0x07E0); tft.print("ROTATE");

}
