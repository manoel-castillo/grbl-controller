/***************************************************************************************
    Name    : GRBL Controller
    Author  : Manoel Mello Castillo
    Created : June 23, 2018
    Version : 0.1
    License : This program is free software. You can redistribute it and/or modify
              it under the terms of the GNU General Public License as published by
              the Free Software Foundation, either version 3 of the License, or
              (at your option) any later version.
              This program is distributed in the hope that it will be useful,
              but WITHOUT ANY WARRANTY; without even the implied warranty of
              MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
              GNU General Public License for more details.
 ***************************************************************************************/
#include <util/atomic.h>
#include <LiquidCrystal.h>
#include <SPI.h>
#include <SD.h>

#define MENU_SIZE         0x07
#define SD_CS_PIN         0x03
#define BTN_NONE          0x00
#define BTN_RIGHT         0x01
#define BTN_LEFT          0x02
#define BTN_UP            0x03
#define BTN_DOWN          0x04
#define BTN_SELECT        0x05
#define MENU_MAIN         0x01
#define MENU_HOMMING      0x02
#define MENU_FILES        0x03
#define MENU_MOVE         0x04
#define MENU_STATUS       0x05
#define MENU_RESET        0x06
#define MENU_UNLOCK       0x07
#define MENU_CONFIG       0x08
#define MENU_MILL         0x09
#define MENU_MILL_OPT     0x10
#define RESET_GRBL        0x18
#define OPERATION_MILLING 0x00
#define OPERATION_MENU    0x01
#define NULL_MESSAGE      ""
#define OK_MESSAGE        "ok"
#define ALARM_MESSAGE     "ALARM"
#define ERROR_MESSAGE     "error"

byte currentOperation = OPERATION_MENU;

// SD variables
File root;
File currentFile;
bool sdInitialized = false;
unsigned long fileSize = 0;

// Serial communication
String msgToSend;
unsigned long lastRxCheck = 0;
unsigned long lastTxCheck = 0;
unsigned long printMsgCheck = 0;
byte cursorPos = 0;
bool lastOrFirstScroll = true;
bool lockMessages = false;
String rxBuffer[2]; //0: [Alarm], 1: [Msg:], <...>
String currentMessage;
bool okInstruction = true;

// Menu control variables
byte currentMenu = MENU_MAIN;
byte currentButton = BTN_NONE;
byte menuPage = 0;

//sub menus variables
byte subMenuPage = 0;
byte currentConfigLine = 0;
byte statusCommand = 0;

//machine variables and MoveMenu
float x = 0.0;
float y = 0.0;
float z = 0.0;
float stepSizeOpt[] = {0.1, 1.0, 10.0};
byte stepSizeIdx = 0;
byte resetAxisIdx = 0; //0:XY, 1:Z
bool locked = true; //grbl starts locked unitl homming or unlock
bool paused = false;
byte millOption = 0;

// Setting the LCD shields pins
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);

void setup() {

  rxBuffer[0] = NULL_MESSAGE;
  rxBuffer[1] = NULL_MESSAGE;

  // Initializes serial communication
  //GRBL baud rate
  Serial.begin(115200);
  while (!Serial) {
    ;
  }

  delay(500);
  Serial.print(F("\n"));

  SD.begin(SD_CS_PIN);
  root = SD.open("/");

  // Initializes and clears the LCD screen
  lcd.begin(16, 2);
  lcd.clear();

  menuMain();

}

void loop() {
  if (currentOperation == OPERATION_MENU) {
    sendMessage();
  } else if (currentOperation == OPERATION_MILLING) {
    mill();
  }
  readStatus();
  operate();
}

int lastButtonRead;
unsigned long buttonReadTime = 0;
byte evaluateButton() {
  unsigned long currentTime = millis();
  if (currentTime - buttonReadTime < 200) return BTN_NONE;
  buttonReadTime = currentTime;

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    lastButtonRead = analogRead(0);
  }

  if (lastButtonRead < 50) return BTN_RIGHT; // right
  if (lastButtonRead < 250) return BTN_UP; // up
  if (lastButtonRead < 450) return BTN_DOWN; // down
  if (lastButtonRead < 650) return BTN_LEFT; // left
  if (lastButtonRead < 850) return BTN_SELECT; // select
  return BTN_NONE; //none :p

}

void operate() {
  byte button = evaluateButton();

  printCurrentMessage();

  if (currentOperation == OPERATION_MENU) {
    if (button == currentButton) return;
    currentButton = button;
    switch (currentMenu) {
      case MENU_MAIN:
        menuMain();
        break;
      case MENU_HOMMING:
        menuHomming();
        break;
      case MENU_FILES:
        menuFile();
        break;
      case MENU_MILL:
        promptMill();
        break;
      case MENU_MOVE:
        menuMove();
        break;
      case MENU_STATUS:
        showStatus();
        currentMenu = MENU_MAIN;
        break;
      case MENU_RESET:
        menuReset();
        break;
      case MENU_UNLOCK:
        unlock();
        currentMenu = MENU_MAIN;
        break;
      case MENU_CONFIG:
        menuConfig();
        break;
    }

  } else if (currentOperation == OPERATION_MILLING) {
    currentButton = button;

    if (currentButton != BTN_NONE) {
      currentMenu = MENU_MILL_OPT;
    }

    switch (currentMenu) {
      case MENU_MILL_OPT:
        showMillOptions();
        break;
    }

  }

}

byte getLength(const __FlashStringHelper *str) {
  PGM_P p = reinterpret_cast<PGM_P>(str);
  byte n = 0;
  while (1) {
    unsigned char c = pgm_read_byte(p++);
    if (c == 0) break;
    n++;
  }
  return n;
}

void printCenterMessagePSTR(const __FlashStringHelper *msg, int line) {
  byte l = getLength(msg);
  byte cursorPos = 8 - l / 2;
  if (l % 2 != 0) cursorPos -= 1;
  lcd.setCursor(cursorPos, line);
  lcd.print(msg);
}

void printCenterMessage(String msg, int line) {
  int l = msg.length();
  byte cursorPos = 8 - l / 2;
  if (l % 2 != 0) cursorPos -= 1;
  lcd.setCursor(cursorPos, line);
  lcd.print(msg);
}

void menuMain() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("<"));
  switch (menuPage) {
    case 0:
      printCenterMessagePSTR(F("HOMMING"), 0);
      break;
    case 1:
      printCenterMessagePSTR(F("FILES"), 0);
      break;
    case 2:
      printCenterMessagePSTR(F("MOVE"), 0);
      break;
    case 3:
      printCenterMessagePSTR(F("STATUS"), 0);
      break;
    case 4:
      printCenterMessagePSTR(F("UNLOCK"), 0);
      break;
    case 5:
      printCenterMessagePSTR(F("RESET"), 0);
      break;
    case 6:
      printCenterMessagePSTR(F("CONFIG"), 0);
      break;
  }

  lcd.setCursor(15, 0);
  lcd.print(F(">"));

  switch (currentButton) {
    case BTN_NONE: // When button returns as 0 there is no action taken
      break;
    case BTN_SELECT:  // This case will execute if the "forward" button is pressed
      switch (menuPage) { // The case that is selected here is dependent on which menu page you are on and where the cursor is.
        case 0:
          currentMenu = MENU_HOMMING;
          break;
        case 1:
          currentMenu = MENU_FILES;
          break;
        case 2:
          currentMenu = MENU_MOVE;
          break;
        case 3:
          currentMenu = MENU_STATUS;
          break;
        case 4:
          currentMenu = MENU_UNLOCK;
          break;
        case 5:
          currentMenu = MENU_RESET;
          break;
        case 6:
          currentMenu = MENU_CONFIG;
          break;
      }

      break;

    case BTN_LEFT:
      menuPage--;
      if (menuPage == 0xFF) menuPage = MENU_SIZE - 1;
      break;
    case BTN_RIGHT:
      menuPage++;
      if (menuPage >= MENU_SIZE) menuPage = 0;
      break;
  }
}

void menuMove() {
  currentMessage = "";
  lcd.clear();
  lcd.setCursor(0, currentConfigLine);
  lcd.print(F("*"));
  lcd.setCursor(1, 0);
  lcd.print(F("<"));
  switch (subMenuPage) {
    case 0:
      printCenterMessagePSTR(F("Move XY"), 0);
      break;
    case 1:
      printCenterMessagePSTR(F("Move Z"), 0);
      break;
    case 2:
      printCenterMessagePSTR(F("Step Size"), 0);
      break;
    case 3:
      printCenterMessagePSTR(F("Reset Axis"), 0);
      break;
    case 4:
      printCenterMessagePSTR(F("Exit"), 0);
      break;
  }
  lcd.setCursor(15, 0);
  lcd.print(F(">"));

  printMoveInfo();

  if (currentConfigLine == 0) { //selectiong options
    switch (currentButton) {
      case BTN_SELECT:
        if (subMenuPage == 4) {
          subMenuPage = 0;
          currentConfigLine = 0;
          currentMenu = MENU_MAIN;
        } else if (!locked) {
          currentConfigLine = 1;
        }
        break;

      case BTN_LEFT:
        subMenuPage--;
        if (subMenuPage == 0xFF) subMenuPage = 4;
        break;
      case BTN_RIGHT:
        subMenuPage++;
        if (subMenuPage > 0x04) subMenuPage = 0;
        break;
    }
  } else { //operating options
    if (currentButton == BTN_SELECT) currentConfigLine = 0;
    switch (subMenuPage) {
      case 0: //move XY
        moveXY();
        break;
      case 1: //move Z
        moveZ();
        break;
      case 2: // Step Size
        selectStepSize();
        break;
      case 3: // Reset Axis
        selectResetAxis();
        break;
    }
  }
}

void printMoveInfo() {
  String coords = "";

  if (locked) {
    printCenterMessagePSTR(F("Machine Locked"), 1);
    return;
  }

  switch (subMenuPage) {
    case 0: //Show XY coord
      coords += x;
      coords += ",";
      coords += y;
      printCenterMessage(coords, 1);
      break;
    case 1: //Show Z coord
      coords += z;
      printCenterMessage(coords, 1);
      break;
    case 2: //Show Step Size
      coords += stepSizeOpt[stepSizeIdx];
      coords += "mm";
      printCenterMessage(coords, 1);
      break;
    case 3: // Show axis to reset
      switch (resetAxisIdx) {
        case 0:
          printCenterMessagePSTR(F("XY"), 1);
          break;
        case 1:
          printCenterMessagePSTR(F("Z"), 1);
          break;
        case 2:
          printCenterMessagePSTR(F("Cancel"), 1);
          break;
      }
      break;
  }
}

void moveXY() {
  bool exec = true;
  switch (currentButton) {
    case BTN_LEFT:
      x -= stepSizeOpt[stepSizeIdx];
      break;
    case BTN_RIGHT:
      x += stepSizeOpt[stepSizeIdx];
      break;
    case BTN_DOWN:
      y -= stepSizeOpt[stepSizeIdx];
      break;
    case BTN_UP:
      y += stepSizeOpt[stepSizeIdx];
      break;
    case BTN_NONE:
      exec = false;
      break;
  }
  if (exec) {
    jog();
    printMoveInfo();
  }
}

void moveZ() {
  bool exec = true;
  switch (currentButton) {
    case BTN_UP:
    case BTN_RIGHT:
      z += stepSizeOpt[stepSizeIdx];
      break;
    case BTN_LEFT:
    case BTN_DOWN:
      z -= stepSizeOpt[stepSizeIdx];
      break;
    case BTN_NONE:
      exec = false;
      break;
  }
  if (exec) {
    jog();
    printMoveInfo();
  }
}

void jog() {
  Serial.print(F("G0 X"));
  Serial.print(x);
  Serial.print(F(" Y"));
  Serial.print(y);
  Serial.print(F(" Z"));
  Serial.print(z);
  Serial.print(F("\n"));
}

void selectResetAxis() {
  switch (currentButton) {
    case BTN_LEFT:
      resetAxisIdx--;
      if (resetAxisIdx == 0xFF) resetAxisIdx = 2;
      break;
    case BTN_RIGHT:
      resetAxisIdx++;
      if (resetAxisIdx > 0x02) resetAxisIdx = 0;
      break;
    case BTN_SELECT:
      switch (resetAxisIdx) {
        case 0: //reset XY
          //msgToSend = "G92 X0 Y0\n";
          Serial.print(F("G92 X0 Y0\n"));
          break;
        case 1: //reset Z
          Serial.print(F("G92 Z0\n"));
          //msgToSend = "G92 Z0\n";
          break;
        case 2: //cancel
          resetAxisIdx = 0;
          break;
      }

      break;
  }
}

void selectStepSize() {
  switch (currentButton) {
    case BTN_LEFT:
      stepSizeIdx--;
      if (stepSizeIdx == 0xFF) stepSizeIdx = 2;
      printMoveInfo();
      break;
    case BTN_RIGHT:
      stepSizeIdx++;
      if (stepSizeIdx > 0x02) stepSizeIdx = 0;
      printMoveInfo();
      break;
  }
}

//TODO
void menuConfig() {
  String options[] = {};
  lcd.clear();
  lcd.setCursor(0, currentConfigLine);
  lcd.print(F("*"));
  printCenterMessagePSTR(F("grbl config"), 0);
  //lockMessages = true;
  //msgToSend = "$$\n"; //TODO

  switch (currentButton) {
    case BTN_LEFT:
      if (currentConfigLine == 1) {
        //TODO select the $x config
        subMenuPage--; // must verify the options length
        handleConfigOptions();
      } else {
        currentMenu = MENU_MAIN;
        subMenuPage = 0;
      }
      break;
    case BTN_RIGHT:
      if (currentConfigLine == 1) {
        subMenuPage++; // must verify the options length
        handleConfigOptions();
      }
      break;
    case BTN_SELECT:
      break;
    case BTN_UP:
    case BTN_DOWN:
      if (currentConfigLine == 0) currentConfigLine = 1;
      else currentConfigLine = 0;
      break;

  }
}

void handleConfigOptions() {
  //TODO shows grbl configs $x and alter values
}

void unlock() {
  //msgToSend = "$X\n";
  Serial.print(F("$X\n"));
  locked = false;
}

void showStatus() {
  switch (statusCommand) {
    case 0:
      //msgToSend = "?\n";
      Serial.print(F("?\n"));
      break;
    case 1:
      //msgToSend = "$G\n";
      Serial.print(F("$G\n"));
      break;
    case 2:
      //msgToSend = "$I\n";
      Serial.print(F("$I\n"));
      break;
  }
  statusCommand += 1;
  if (statusCommand > 2) statusCommand = 0;
}

void menuReset() {
  currentMessage = "";
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Reset?"));

  switch (currentButton) {
    case BTN_LEFT:
      currentMenu = MENU_MAIN;
      break;
    case BTN_SELECT:
      msgToSend += char(RESET_GRBL);
      lcd.setCursor(0, 1);
      lcd.print(F("Waiting..."));
      delay(2000);
      currentMenu = MENU_MAIN;
      menuPage = 0;
      subMenuPage = 0;
      locked = true;
      x = 0.0;
      y = 0.0;
      z = 0.0;
      break;
  }
}

void menuHomming() {
  currentMessage = "";
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.write(F(">"));
  lcd.setCursor(2, 0);
  lcd.print(F("Start cycle?"));

  switch (currentButton) {
    case BTN_LEFT:
      currentMenu = MENU_MAIN;
      break;

    case BTN_SELECT:
      //msgToSend = "$H\n";
      Serial.print(F("$H\n"));
      locked = false;
      currentMenu = MENU_MAIN;
      break;
  }
}

void menuFile() {
  currentMessage = "";
  lcd.clear();
  lcd.setCursor(0, currentConfigLine);
  lcd.print(F("*"));
  lcd.setCursor(1, 0);
  lcd.print(F("<"));

  switch (subMenuPage) {
    case 0:
      printCenterMessagePSTR(F("Files"), 0);
      break;
    case 1:
      printCenterMessagePSTR(F("Exit"), 0);
      break;
  }
  lcd.setCursor(15, 0);
  lcd.print(F(">"));

  if (!currentFile) {
    root.rewindDirectory();
    currentFile = root.openNextFile();
  }

  printCenterMessage(currentFile.name(), 1);

  if (currentConfigLine == 0) {
    switch (currentButton) {
      case BTN_DOWN:
      case BTN_SELECT:
        if (subMenuPage == 1) { //exit
          currentMenu = MENU_MAIN;
          subMenuPage = 0;
        } else {
          currentConfigLine = 1;
        }
        break;
      case BTN_LEFT:
        subMenuPage--;
        if (subMenuPage == 0xFF) subMenuPage = 1;
        break;
      case BTN_RIGHT:
        subMenuPage++;
        if (subMenuPage > 0x01) subMenuPage = 0;
        break;
    }
  } else {
    switch (currentButton) {
      case BTN_SELECT:
        currentMenu = MENU_MILL;
        break;
      case BTN_UP:
        currentConfigLine = 0;
        break;
      case BTN_RIGHT:
      case BTN_LEFT:
      case BTN_DOWN:
        if (currentFile) currentFile.close();
        currentFile = root.openNextFile();
        break;
    }
  }
}

void promptMill() {
  lcd.clear();
  printCenterMessage(currentFile.name(), 0);
  lcd.setCursor(0, currentConfigLine);
  lcd.print(F("*"));
  switch (millOption) {
    case 0:
      printCenterMessagePSTR(F("Begin Mill?"), 1);
      break;
    case 1:
      printCenterMessagePSTR(F("Cancel"), 1);
      break;
  }

  switch (currentButton) {
    case BTN_LEFT:
      millOption--;
      if (millOption == 0xFF) millOption = 1;
      break;
    case BTN_RIGHT:
      millOption++;
      if (millOption > 0x01) millOption = 0;
      break;
    case BTN_SELECT:
      if (millOption == 0x01) { //cancel
        millOption = 0;
        currentMenu = MENU_FILES;
      } else { //mill
        if (!currentFile) {
          printCenterMessagePSTR(F("Error! No file"), 1);
        } else {
          subMenuPage = 0;
          currentOperation = OPERATION_MILLING;
        }
      }
      break;
  }
}

void mill() {
  if (fileSize == 0) {
    fileSize = currentFile.size();
    subMenuPage = 0; // to use in the milling options menu
  }

  if (currentFile.available() && !paused && okInstruction) {
    String str = currentFile.readStringUntil('\n');
    Serial.println(str);
    if (str.startsWith("M02")) { //last line
      endJob();
      return;
    }
  }

  okInstruction = false;

  /*
    while (!msgBuffer.startsWith("ok") && !paused) {
      while (Serial.available() > 0) {
        delay(1);
        char c = char(Serial.read());
        if (c <= 31) continue;
        msgBuffer += c;
      }
      delay(1);
    }
  */
}

void showMillOptions() {
  lcd.clear();
  lockMessages = true;
  switch (subMenuPage) {
    case 0:
      printCenterMessagePSTR(F(" Pause "), 1);
      break;
    case 1:
      printCenterMessagePSTR(F("Resume"), 1);
      break;
    case 2:
      printCenterMessagePSTR(F("Status"), 1);
      break;
    case 3:
      printCenterMessagePSTR(F(" Exit "), 1);
      break;
  }

  switch (currentButton) {
    case BTN_LEFT:
      subMenuPage--;
      if (subMenuPage == 0xFF) subMenuPage = 3;
      break;
    case BTN_RIGHT:
      subMenuPage++;
      if (subMenuPage > 0x03) subMenuPage = 0;
      break;
    case BTN_SELECT:
      switch (subMenuPage) {
        case 0: //pause
          Serial.print(F("!\n"));
          paused = true;
          break;
        case 1: //resume
          Serial.print(F("~\n"));
          currentMenu = MENU_MILL;
          lockMessages = false;
          paused = false;
          subMenuPage = 0;
          break;
        case 2: //status
          Serial.print(F("?\n"));
          break;
        case 3: //exit
          currentMenu = MENU_MILL;
          lockMessages = false;
          subMenuPage = 0;
          break;
      }

      break;
  }
}

void endJob() {
  //Job done!
  currentOperation = OPERATION_MENU;
  currentMenu = MENU_FILES;
  lockMessages = false;
  okInstruction = true;
  menuPage = 1;
  subMenuPage = 0;
  x = 0.0;
  y = 0.0;
  z = 0.0;
  currentFile.seek(0);
  printCenterMessagePSTR(F("Job Done!"), 1);
  delay(5000);
  menuFile();
}

void readStatus() {
  String msgReceived = "";
  while (Serial.available() > 0) {
    delay(1);
    char c = char(Serial.read());
    if (c <= 31 && c != 10) continue;
    else if (c == 10) { // carriage return (13) or new line (10) are part of the message
      if (msgReceived.startsWith(OK_MESSAGE)) { //ok message
        okInstruction = true;
      } else {
        cursorPos = 0;
        if (msgReceived.startsWith("<") || msgReceived.startsWith("[")) { //status message
          rxBuffer[1] = msgReceived;
        } else if (msgReceived.startsWith(ALARM_MESSAGE) || msgReceived.startsWith(ERROR_MESSAGE)) { // alarm
          rxBuffer[0] = msgReceived;
        }
      }
      msgReceived = "";
      continue;
    }
    msgReceived += c;
  }
}

/*

  void readStatus() {
  //long currentTime = millis();
  //if (currentTime - lastRxCheck < 200) return; //check every 200 millis
  //lastRxCheck = currentTime;
  String msgReceived;
  if (Serial.available() > 0) {
    delay(1);
    //char c = char(Serial.read());
    //if (c <= 31 && c != 13 && c != 10) break; // carriage return (13) or new line (10) are part of the message
    //msgReceived += c;
    msgReceived = Serial.readStringUntil('\n');
  }

  msgReceived.replace("\r", "");
  msgReceived.replace("\n", "");

  //0: [Alarm], 1: [Msg:], 2:<...>
  if (msgReceived && msgReceived != "") {
    if (msgReceived.startsWith(OK_MESSAGE)) { //ok message
      okInstruction = true;
    } else {
      cursorPos = 0;
      if (msgReceived.startsWith("<")|| msgReceived.startsWith("[")) { //status message
        rxBuffer[1] = msgReceived;
      } else if (msgReceived.startsWith(ALARM_MESSAGE) || msgReceived.startsWith(ERROR_MESSAGE)) { // alarm
        rxBuffer[0] = msgReceived;
      }
    }
  }
  } */

void printCurrentMessage() {
  long currentTime = millis();
  if (currentTime - printMsgCheck < (currentOperation == OPERATION_MENU ? 500 : 2000)) return;
  printMsgCheck = currentTime;

  bool alarm = false;
  //0: [Alarm], 1: [Msg:], 2:<...>
  if (rxBuffer[0] && rxBuffer[0] != NULL_MESSAGE) { //Alarm Messages
    alarm = true;
    currentMessage = rxBuffer[0];
    rxBuffer[0] = NULL_MESSAGE;
  } else if (rxBuffer[1] && rxBuffer[1] != NULL_MESSAGE) { //Messages and status
    currentMessage = rxBuffer[1];
    rxBuffer[1] = NULL_MESSAGE;
  }

  if (currentOperation == OPERATION_MENU || alarm) {
    if (lastOrFirstScroll) {
      lastOrFirstScroll = false;
      return;
    }

    if ((!lockMessages && currentMessage != "") || alarm) {
      if (alarm) lcd.clear();
      lcd.setCursor(0, 1);
      lcd.print(splitMessage(currentMessage));
    }

  } else if (currentOperation == OPERATION_MILLING) {
    if (!paused && !lockMessages) {
      lcd.clear();
      float total = float((currentFile.position() * 100.0) / fileSize * 1.0);
      String percent = "";
      percent += total;
      percent += "%";
      printCenterMessage(percent, 1);
      printCenterMessagePSTR(F("Working..."), 0);
    } else if (paused) {
      lcd.clear();
      printCenterMessagePSTR(F("Paused"), 0);
    }
  }
}

String splitMessage(String msg) {
  if (msg.length() <= 16) return msg;

  if (cursorPos == 0) {
    lastOrFirstScroll = true;
  }

  String ret = msg.substring(cursorPos, cursorPos + 16);
  cursorPos++;
  if (cursorPos >= msg.length() - 15) {
    lastOrFirstScroll = true;
    cursorPos = 0;
  }
  return ret;
}

void sendMessage() {
  Serial.print(msgToSend);
  msgToSend = "";
}


