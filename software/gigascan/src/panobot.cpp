/* Copyright (C) 2017 Robert Fach All rights reserved.

This software may be distributed and modified under the terms of the GNU
General Public License version 2 (GPL2) as published by the Free Software
Foundation and appearing in the file GPL2.TXT included in the packaging of
this file. Please note that GPL2 Section 2[b] requires that all works based
on this software must also be made publicly available under the terms of
the GPL2 ("Copyleft").

Contact information
-------------------

Robert Fach
e-mail   :  robert.fach@gmx.net
*/

#include <Arduino.h>
#include <Streaming.h>

#ifdef ESP8266
  #define typeof(x) __typeof__(x)
  #include <SPI.h>
  #include <Wire.h>
#endif

#include <config.h>

#include <AccelStepper.h>
#include <math.h>

#include <menu.h>
#include <menuIO/chainStream.h>
#include <menuIO/serialOut.h>
#include <menuIO/u8g2Out.h>
#include <menuIO/encoderIn.h>
#include <menuIO/keyIn.h>

using namespace Menu;

//panobot logic & motor control
AccelStepper panStepper(AccelStepper::DRIVER, PAN_STEP_PIN, PAN_DIR_PIN);
AccelStepper tiltStepper(AccelStepper::DRIVER, TILT_STEP_PIN, TILT_DIR_PIN);

boolean g_runScan = false;
boolean g_isMaxPanLeftSetup = false;
boolean g_isMaxPanRightSetup = false;
boolean g_isPanStepSetup = false;
boolean g_isMaxTiltUpSetup = false;
boolean g_isMaxTiltDownSetup = false;
boolean g_isTiltStepSetup = false;
boolean g_updateStatus = true;

double g_maxPanSpeed = 3000.0;
double g_maxPanAccel = 2000.0;
double g_maxTiltSpeed = 2000.0;
double g_maxTiltAccel = 2000.0;
int pan_step_per_deg = 7 * 8;
int g_maxPanLeftDeg = -10;
int g_maxPanRightDeg = +10;
double g_panStepDeg = 0;
int tilt_step_per_deg = 50; // small gear 35;
int g_maxTiltUpDeg = +10;
int g_maxTiltDownDeg = -10;
double g_tiltStepDeg = 0;
int g_scanPositionHorizontal = 0;
int g_scanPositionVertical = 0;
int g_picturesHorizontal = 0;
int g_picturesVertical = 0;
int g_picturesCount = 0;
int g_picturesTotal = 0;
int g_takePictureDelay = 250;
int g_takePicturePreDelay = 0;
int g_shutterDelay = 500;
double panPos = 0;
double tiltPos = 0;

double g_crop = 1.6;
double g_sensorFF_horizontal = 36.0;
double g_sensorFF_vertical = 24.0;
int g_focalLength = 300;
int g_hol = 30;
int g_vol = 30;
double g_hfov = 0;
double g_vfov = 0;


//callback function used by the menu to do the math when updating some values
void updateScanner() {
  g_hfov = degrees( 2 * atan2(g_sensorFF_horizontal, g_crop * 2 * g_focalLength));
  g_vfov = degrees( 2 * atan2(g_sensorFF_vertical, g_crop * 2 * g_focalLength));
  g_panStepDeg = g_hfov * (100 - g_hol) / 100;
  g_tiltStepDeg = g_vfov * (100 - g_vol) / 100;
  g_picturesHorizontal = ceil((abs(g_maxPanLeftDeg) + abs(g_maxPanRightDeg)) / g_panStepDeg);
  g_picturesVertical = ceil((abs(g_maxTiltUpDeg) + abs(g_maxTiltDownDeg)) / g_tiltStepDeg);
  g_picturesTotal = g_picturesHorizontal * g_picturesVertical;
  Serial.print("HSCAN: "); Serial.println(abs(g_maxPanLeftDeg) + abs(g_maxPanRightDeg), DEC);
  Serial.print("VSCAN: "); Serial.println(abs(g_maxTiltUpDeg) + abs(g_maxTiltDownDeg), DEC);
  Serial.print("g_hfov: "); Serial.println(g_hfov,DEC);
  Serial.print("g_vfov: "); Serial.println(g_vfov,DEC);
  Serial.print("g_panStepDeg: "); Serial.println(g_panStepDeg,DEC);
  Serial.print("g_tiltStepDeg: "); Serial.println(g_tiltStepDeg,DEC);
  Serial.print("g_picturesHorizontal: "); Serial.println(g_picturesHorizontal, DEC);
  Serial.print("g_picturesVertical: "); Serial.println(g_picturesVertical, DEC);
  Serial.print("g_picturesTotal: "); Serial.println(g_picturesTotal, DEC);
  //g_updateStatus = true;
}

void updateMaxPanLeftPosition(eventMask e) {
  if (e & exitEvent) {
    panStepper.moveTo(0);
  } else {
    panStepper.moveTo(g_maxPanLeftDeg * pan_step_per_deg);
  }
  updateScanner();
}
void updateMaxPanRightPosition(eventMask e) {
  if (e & exitEvent) {
    panStepper.moveTo(0);
  } else {
    panStepper.moveTo(g_maxPanRightDeg * pan_step_per_deg);
  }
  updateScanner();
}

void updateMaxTiltUpPosition(eventMask e) {
  if (e & exitEvent) {
    tiltStepper.moveTo(0);
  } else {
    tiltStepper.moveTo(g_maxTiltUpDeg * tilt_step_per_deg);
  }
  updateScanner();
}

void updateMaxTiltDownPosition(eventMask e) {
  if (e & exitEvent) {
    tiltStepper.moveTo(0);
  } else {
    tiltStepper.moveTo(g_maxTiltDownDeg * tilt_step_per_deg);
  }
  updateScanner();
}

void runScanCallback()
{
  g_runScan = true;
  g_scanPositionHorizontal = 0;
  g_scanPositionVertical = 0;
  g_updateStatus = true;
}

void printScannerStats() {
  Serial.print("H[");
  Serial.print(g_scanPositionHorizontal+1, DEC);
  Serial.print("/");
  Serial.print(g_picturesHorizontal, DEC);
  Serial.println("]");
  Serial.print("V[");
  Serial.print(g_scanPositionVertical+1, DEC);
  Serial.print("/");
  Serial.print(g_picturesVertical, DEC);
  Serial.println("]");
  Serial.print("T[");
  Serial.print(g_picturesCount, DEC);
  Serial.print("/");
  Serial.print(g_picturesTotal, DEC);
  Serial.println("]");
}

typedef enum {
  IDLE,
  POSITIONING,
  STABILIZE_WAIT,
  FOCUS_WAIT,
  TRIGGER_WAIT
} eSystemState;

typedef enum {
  ZICK_ZACK_LEFT_RIGHT_DOWN_UP
} eSystemModePattern;

eSystemState state = IDLE;
eSystemModePattern modePattern = ZICK_ZACK_LEFT_RIGHT_DOWN_UP;
long delayTime = 0;
long startTime = 0;

eSystemState updatePosition(eSystemState state, eSystemModePattern mode) {
   if (state == IDLE) {
      if (mode == ZICK_ZACK_LEFT_RIGHT_DOWN_UP) {
        g_updateStatus = true;
        panPos = g_maxPanLeftDeg;
        tiltPos = g_maxTiltDownDeg;
      }
   } else {
      if (mode == ZICK_ZACK_LEFT_RIGHT_DOWN_UP) {
        g_updateStatus = true;
        if (g_scanPositionHorizontal >= g_picturesHorizontal-1) {
          if (g_scanPositionVertical >= g_picturesVertical-1) {
            g_runScan = false;
            return IDLE;
          } else {
            tiltPos += g_tiltStepDeg;
          }
          panPos = g_maxPanLeftDeg;
          g_scanPositionHorizontal = 0;
          g_scanPositionVertical++;
        } else {
          panPos += g_panStepDeg;
          g_scanPositionHorizontal++;
        }
      }
   }
   tiltStepper.moveTo(tiltPos * tilt_step_per_deg);
   panStepper.moveTo(panPos * pan_step_per_deg);
   return POSITIONING;
}

void stateMachine() {
  if (state == IDLE && g_runScan) {
    state = updatePosition(state, modePattern);
  }
  if (state == POSITIONING) {
    if (tiltStepper.distanceToGo() == 0 && panStepper.distanceToGo() == 0) {
      delayTime = g_takePictureDelay;
      startTime = millis();
      state = STABILIZE_WAIT;
    }
  }
  if (state == STABILIZE_WAIT) {
    if (millis() - startTime > delayTime) {
      digitalWrite(FOCUS_PIN, HIGH);
      delayTime = g_takePicturePreDelay;
      startTime = millis();
      state = FOCUS_WAIT;
    }
  }
  if (state == FOCUS_WAIT) {
    if (millis() - startTime > delayTime) {
      digitalWrite(SHUTTER_PIN, HIGH);
      delayTime = g_shutterDelay;
      startTime = millis();
      state = TRIGGER_WAIT;
    }
  }
  if (state == TRIGGER_WAIT) {
    if (millis() - startTime > delayTime) {
      g_picturesCount++;
      g_updateStatus = true;
      printScannerStats();
      digitalWrite(FOCUS_PIN, LOW);
      digitalWrite(SHUTTER_PIN, LOW);
      state = updatePosition(state,modePattern);
    }
  }
}

void initializeSteppers() {
  panStepper.setMaxSpeed(g_maxPanSpeed);
  panStepper.setAcceleration(g_maxPanAccel);
  panStepper.setPinsInverted(true,false,false);
  tiltStepper.setMaxSpeed(g_maxTiltSpeed);
  tiltStepper.setAcceleration(g_maxTiltAccel);
  tiltStepper.setPinsInverted(false,false,false);
}

//initializes everything which is bot related
void setupPanoBot() {
  pinMode(FOCUS_PIN, OUTPUT);
  pinMode(SHUTTER_PIN, OUTPUT);
  digitalWrite(FOCUS_PIN, LOW);
  digitalWrite(SHUTTER_PIN, LOW);
  initializeSteppers();
  updateScanner();
  g_updateStatus = true;
}

//Panobot MENU system, it uses ArduinoMenu library in combination with U8g2

// define menu colors --------------------------------------------------------
//each color is in the format:
//  {{disabled normal,disabled selected},{enabled normal,enabled selected, enabled editing}}
// this is a monochromatic color table
const colorDef<uint8_t> colors[] MEMMODE={
  {{0,0},{0,1,1}},//bgColor
  {{1,1},{1,0,0}},//fgColor
  {{1,0},{1,0,0}},//valColor
  {{1,1},{1,0,0}},//unitColor
  {{0,1},{0,0,1}},//cursorColor
  {{0,0},{1,1,1}},//titleColor
};

#define fontName u8g2_font_5x7_tf
#define fontX 5
#define fontY 8
#define offsetX 5
#define offsetY 32
#define MAX_DEPTH 2

//initialize SmartLCD display, hardware SPI
U8G2_ST7920_128X64_F_HW_SPI u8g2(U8G2_R0, DISPLAY_CS_PIN, U8X8_PIN_NONE);

// Panobot Setup Menu
MENU(subMenuSetup,"Setup",doNothing,noEvent,noStyle
  ,FIELD(g_maxPanLeftDeg,      "Pan Left     ","DEG",-200,0,10,1,updateMaxPanLeftPosition,enterEvent | exitEvent | updateEvent,wrapStyle)
  ,FIELD(g_maxPanRightDeg,     "Pan Right    ","DEG",0,200,10,1,updateMaxPanRightPosition,enterEvent | exitEvent | updateEvent,wrapStyle)
  ,FIELD(g_maxTiltUpDeg,       "Tilt UP      ","DEG",0,100,10,1,updateMaxTiltUpPosition,enterEvent | exitEvent | updateEvent,wrapStyle)
  ,FIELD(g_maxTiltDownDeg,     "Tilt Down    ","DEG",-90,0,10,1,updateMaxTiltDownPosition,enterEvent | exitEvent | updateEvent,wrapStyle)
  ,FIELD(g_takePicturePreDelay,"Image P-Delay","ms",0,5000,1000,100,doNothing,noEvent,wrapStyle)
  ,FIELD(g_takePictureDelay,   "Image Delay  ","ms",0,15000,1000,100,doNothing,noEvent,wrapStyle)
  ,FIELD(g_shutterDelay,       "Shutter Delay","ms",0,15000,1000,100,doNothing,noEvent,wrapStyle)
  ,FIELD(g_focalLength,        "Focal Length ","mm",1,1000,10,1,updateScanner,enterEvent | exitEvent | updateEvent,wrapStyle)
  ,FIELD(g_hol,                "Hor. Overlap ","%",0,100,10,1,updateScanner,enterEvent | exitEvent | updateEvent,wrapStyle)
  ,FIELD(g_vol,                "Ver. Overlap ","%",0,100,10,1,updateScanner,enterEvent | exitEvent| updateEvent,wrapStyle)
  ,FIELD(g_crop,               "Crop Factor  ","",0,10,0.1,0.01,updateScanner,enterEvent | exitEvent| updateEvent,wrapStyle)
  ,EXIT("<Back")
);

MENU(subMenuHardware,"Hardware",doNothing,noEvent,noStyle
  ,FIELD(pan_step_per_deg,  "PAN  ", "#/deg",0,1000,10,1, doNothing, noEvent, wrapStyle)
  ,FIELD(tilt_step_per_deg, "TILT ", "#/deg",0,1000,10,1, doNothing, noEvent, wrapStyle)
  ,FIELD(g_maxPanSpeed,     "Pan  Speed", "", 0, 5000, 100, 10, initializeSteppers, enterEvent | exitEvent| updateEvent, wrapStyle)
  ,FIELD(g_maxPanAccel,     "Pan  Accel", "", 0, 5000, 100, 10, initializeSteppers, enterEvent | exitEvent| updateEvent, wrapStyle)
  ,FIELD(g_maxTiltSpeed,    "Tilt Speed", "", 0, 5000, 100, 10, initializeSteppers, enterEvent | exitEvent| updateEvent, wrapStyle)
  ,FIELD(g_maxTiltAccel,    "Tilt Accel", "", 0, 5000, 100, 10, initializeSteppers, enterEvent | exitEvent| updateEvent, wrapStyle)
  ,EXIT("<Back")
)

//Panobot Main Menu
MENU(mainMenu,"Main menu",doNothing,noEvent,noStyle
  ,OP("Scan",runScanCallback,enterEvent)
  ,OP("Take Picture",doNothing,enterEvent)
  ,SUBMENU(subMenuSetup)
  ,SUBMENU(subMenuHardware)
);

//SmartLCD encoder driver
encoderIn<ENCODER_A_PIN,ENCODER_B_PIN> encoder;
encoderInStream<ENCODER_A_PIN,ENCODER_B_PIN> encStream(encoder,4);
//SmartLCD encoder key and reset button routed to the same "enter" command
keyMap encBtn_map[]={{ENCODER_BTN_PIN,options->getCmdChar(enterCmd)},{DISPLAY_RESET_BTN_PIN,options->getCmdChar(enterCmd)}};//negative pin numbers use internal pull-up, this is on when low
keyIn<2> encButton(encBtn_map);
//combine all streams as input streams
Stream* in[]={&encStream,&encButton,&Serial};
chainStream<3> sencoder(in);

MENU_OUTPUTS(out,MAX_DEPTH
  ,U8G2_OUT(u8g2,colors,fontX,fontY,offsetX,offsetY,{0,0,(128-2*offsetX)/fontX,32/fontY})
  ,SERIAL_OUT(Serial)
);

NAVROOT(nav,mainMenu,MAX_DEPTH,sencoder,out);

config myOptions('*','-',false,false,defaultNavCodes);

//this function writes the Panobot status to the LCD
void drawStatus() {
  enum {BufSize=128/fontX};
  char buf1[BufSize];
  char buf2[BufSize];
  snprintf (buf1, BufSize, "H[%02d/%02d]V[%02d/%02d]",
  g_scanPositionHorizontal+1,
  g_picturesHorizontal,
  g_scanPositionVertical+1,
  g_picturesVertical
  );
  snprintf (buf2, BufSize, "T[%03d/%03d]",
  g_picturesCount,
  g_picturesTotal
  );
  u8g2.drawStr(128 - fontX * strlen(buf1), 8, buf1);
  u8g2.drawStr(128 - fontX * strlen(buf2), 16, buf2);
  g_updateStatus = false;
}

void setup() {
  options = &myOptions;
  //initialize Serial interface
  Serial.begin(115200);
  while(!Serial);
  Serial.println("Panobot");Serial.flush();
  //initialize menu
  u8g2.begin();
  u8g2.setFont(fontName);
  encButton.begin();
  encoder.begin();
  //initialize bot
  setupPanoBot();
}

void loop() {
  nav.doInput();
  if (nav.changed(0) || g_updateStatus) {
    u8g2.clearBuffer();
    nav.doOutput();
    //this fixes?? an issue with ArduinoMenu, when the Cursor is over the last entry in a Menu,
    //the color mode/draw is set to XOR?? -> issue bug/question report
    u8g2.setFontMode(1);
    u8g2.setDrawColor(1);
    u8g2.setFont(u8g2_font_ncenB12_tr);
    u8g2.drawStr(0, 32, "PANOBOT");
    u8g2.setFont(fontName);
    drawStatus();
    u8g2.sendBuffer();
  }
  stateMachine();
  panStepper.run();
  tiltStepper.run();
}
