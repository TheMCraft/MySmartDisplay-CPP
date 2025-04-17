#include <Arduino.h>
#include <NeoPixelBus.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include "BluetoothSerial.h"
#include "EEPROM.h"

BluetoothSerial SerialBT;

// PIN for LED Strip (33 MySmartDisplay, 13 MeetingLight )
#define DATA_PIN 33

#define colorMax 70

#define BUILTIN_LED 2
#define EXTERNAL_LED 32

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

int numOfLeds = 33;  // change here and in setup()

NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(numOfLeds, DATA_PIN);

int p = 0;

// running light 0 .. off, 1 ... right, 2 ... left, 3 ... manually
int run = 0;
int rp = 0;       // first pixel
int rc = 3;       // count of running pixels
int rdelay = 250;  // delays im ms

// blinking  0... off, 1 .. color 1, 2 ... color 1&2 alternate, 3 ... blink jump
// color 1&2
int blink = 0;
int bp = 1;  // first color

// collector string for serial commands and flags
String cmd = "";
boolean cmdreceived = false;
boolean debug_bluetooth = false;
boolean debug_serial = true;

// Colors
RgbColor white(colorMax);
RgbColor black(0);
RgbColor currentcolor(173, 0 , 72);
RgbColor secondcolor(105, 121, 131);

// meeting stuff
int meeting = 1;  // meeting mode
long meetingtime = 0;
long lasttime = 0;
long start = 0;
int addr = 0;
int meetingminutes = 2;
int warningminutes = 1;
int flashingseconds = 10;
int meetingmode = 0;  //set to 1 for multicolor gradient mode 
int oldratio = -1;

static BLECharacteristic* pCharacteristic;

#define EEPROM_SIZE 10

// {dataflag, r1, g1, b1, r2, g2, b2, meetingminues, warningminutes, flash seconds}
byte data[] = {1, 0, 50, 0, 50, 0, 0, 2, 1, 10};

// Prototypes
void serialEvent();
void serialBTEvent();
void clear();
void handleCommand(String com, String param);
void writeBT(String s);
void writeEEPROM();
void readEEPROM();
void bleSetup();
void writeToBLEDevice(String message);

void setup() {
  Serial.begin(115200);

  cmd.reserve(100);

  pinMode(BUILTIN_LED, OUTPUT);
  pinMode(EXTERNAL_LED, OUTPUT);

  if (!EEPROM.begin(EEPROM_SIZE)) {
    Serial.println("failed to init EEPROM");
    delay(5000);
  }

  readEEPROM();

  strip.Begin();
  clear();
  // strip.Show();
  // Serial.println(" ");
  // Serial.println("","Meeting LED Strip 1.0");
  
  bleSetup();
  numOfLeds = 13;  // change above too
}

class BLEServerCB : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) override {
    BLEDevice::startAdvertising();
  }
} bleServerCB;

class CharacteristicCallBack : public BLECharacteristicCallbacks
{
  public:
    void onWrite(BLECharacteristic *characteristic_) override
    {
      int index = 0;
      String cmdString = String(characteristic_->getValue().c_str());
      
      Serial.println(cmdString);
      if (cmdString.length() > 0) {
        index = cmdString.indexOf(":");
        String com = cmdString.substring(0, index);
        String param = cmdString.substring(index + 1);
        handleCommand(com, param);
    }
  }
};


void bleSetup() {
  uint64_t chipId = ESP.getEfuseMac();
  String chipIdString = String(chipId, HEX);
  chipIdString.toUpperCase();
  String bleName = chipIdString;
  BLEDevice::init(bleName.c_str());
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(&bleServerCB);

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_READ |
                                         BLECharacteristic::PROPERTY_WRITE |
                                         BLECharacteristic::PROPERTY_NOTIFY |
                                         BLECharacteristic::PROPERTY_INDICATE
                                       );
  pCharacteristic->setCallbacks(new CharacteristicCallBack());

  pCharacteristic->setValue("Hello from MeetingLight");
  pService->start();
  
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06); 
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  

}

void writeToBLEDevice(String message)
{
  std::string messageStdString(message.c_str(), message.length());
  pCharacteristic->setValue(messageStdString);
}


void debugPrint(String tag, String s) {
  if (debug_bluetooth) {
    writeBT(tag);
    writeBT(":");
    writeBT(s);
    writeBT("\n");
  }
  if (debug_serial) {
    Serial.print(tag);
    Serial.print(" ");
    Serial.println(s);
  }
}

void writeBT(String s) {
  for (int i = 0; i < s.length(); i++) {
    SerialBT.write(s.charAt(i));
  }
}

void clear() {
  strip.ClearTo(black);
  strip.Show();
  // for (int x = 0; x < numOfLeds; x++) {
  //   strip.setPixelColor(x, 0, 0, 0);
  //}
}

void colorrun() {
  for (int x = 0; x < numOfLeds; x++) {
    int g = random(0, colorMax);
    int r = random(0, colorMax);
    int b = random(0, colorMax);
    RgbColor color(r, g, b);
    strip.SetPixelColor(x, color);
    strip.Show();
    delay(rdelay);
    strip.SetPixelColor(x, black);
  }

  for (int x = numOfLeds - 1; x >= 0; x--) {
    int g = random(0, colorMax);
    int r = random(0, colorMax);
    int b = random(0, colorMax);
    RgbColor color(r, g, b);
    strip.SetPixelColor(x, color);
    strip.Show();
    delay(rdelay);
    strip.SetPixelColor(x, black);
  }
}

void lightup(int color) {
  for (int x = 0; x < numOfLeds; x++) {
    if (color != 2) {
      strip.SetPixelColor(x, currentcolor);
    } else {
      strip.SetPixelColor(x, secondcolor);
    }
  }
  strip.Show();
}

void handleCommand(String com, String param) {
  debugPrint("cmd", com);
  debugPrint("param", param);

  if (com == "led") {
    if (param.startsWith("on")) {
      digitalWrite(EXTERNAL_LED, HIGH);
      digitalWrite(BUILTIN_LED, HIGH);
    }
    if (param == "off") {
      digitalWrite(EXTERNAL_LED, LOW);
      digitalWrite(BUILTIN_LED, LOW);
    }
  } else if (com == "num") {
    int n = param.toInt();
    if (n > 0) {
      numOfLeds = n;
    }
  } else if (com == "r") {
    currentcolor.R = param.toInt();
  } else if (com == "g") {
    currentcolor.G = param.toInt();
  } else if (com == "b") {
    currentcolor.B = param.toInt();
  } else if (com == "r2") {
    secondcolor.R = param.toInt();
  } else if (com == "g2") {
    secondcolor.G = param.toInt();
  } else if (com == "b2") {
    secondcolor.B = param.toInt();
  }  else if (com == "mmode") {
    meetingmode = param.toInt();
  } else if (com == "debug") {
    debug_bluetooth = !debug_bluetooth;
  } else if (com == "debugserial") {
    debug_serial = !debug_serial;
  } else if (com == "p") {
    p = param.toInt();
    RgbColor pcol = strip.GetPixelColor(p);
    if (pcol.R == 0 && pcol.G == 0 && pcol.B == 0) {
      strip.SetPixelColor(p, currentcolor);
    } else {
      strip.SetPixelColor(p, black);
    }
    run = 0;
    blink = 0;
    strip.Show();
  } else if (com == "light") {
    if (param == "on") {
      clear();
      lightup(0);
      run = 0;
      blink = 0;
    }
    if (param == "on2") {
      clear();
      lightup(2);
      run = 0;
      blink = 0;
    }
    if (param == "off") {
      clear();
      run = 0;
      blink = 0;
    }
  } else if (com == "colorrun") {
    colorrun();
  } else if (com == "blink") {
    if (param == "on") {
      clear();
      run = 0;
      blink = 1;
    } else if (param == "off") {
      clear();
      run = 0;
      blink = 0;
    } else {
      clear();
      run = 0;
      blink = param.toInt();
    }

  } else if (com == "readconfig") {
    readEEPROM();
  } else if (com == "writeconfig") {
    writeEEPROM();
  } else if (com == "run") {
    if (param == "on") {
      clear();
      run = 1;
      blink = 0;
    }
    if (param == "off") {
      clear();
      run = 0;
    }
    if (param == "man") {
      clear();
      run = 3;  // manually by setting rp
      blink = 0;
    }

  } else if (com == "rp") {
    rp = param.toInt();
  } else if (com == "rc") {
    rc = param.toInt();
  } else if (com == "delay") {
    rdelay = param.toInt();
  } else if (com == "clear") {
    clear();
    run = 0;
    blink = 0;
  } else if (com == "meeting") {
    if (param == "start") {
      debugPrint("Meeting", "start");
      writeEEPROM();
      start = millis();
      meetingtime = 0;
      lasttime = 0;
      clear();
      meeting = 1;
      run = 0;
      blink = 0;
    }
    if (param == "stop") {
      debugPrint("Meeting", "stop");
      start = millis();
      clear();
      meeting = 0;
      run = 0;
      blink = 0;
    }
  } else if (com == "meetingminutes") {
    meetingminutes = param.toInt();
    //writeEEPROM();
  } else if (com == "warningminutes") {
    warningminutes = param.toInt();
    //writeEEPROM();
  } else if (com == "flashingseconds") {
    flashingseconds = param.toInt();
    //writeEEPROM();
  } else if (com == "get") {
    if (param == "config") {
      // writeBT(String(meetingminutes));
      // writeBT(" ");
      // writeBT(String(warningminutes));
      // writeBT(" ");
      // writeBT(String(flashingseconds));
      // writeBT(" ");
      // writeBT(String(meeting));
      String configString = "config;" + 
                            String(meetingminutes) + ";" + 
                            String(warningminutes) + ";" + 
                            String(flashingseconds) + ";" +
                            String(meeting) + ";" +
                            String(currentcolor.R) + ";" +
                            String(currentcolor.G) + ";" +
                            String(currentcolor.B) + ";" +
                            String(secondcolor.R) + ";" +
                            String(secondcolor.G) + ";" +
                            String(secondcolor.B);
      writeToBLEDevice(configString);
      Serial.println(configString);
    }
    
  } else if (com == "info") {
    writeBT("Meeting Leds 1.0\n");
    writeBT("Meeting Time: ");
    writeBT(String(meetingminutes));
    writeBT(" Min\n");
    writeBT("Warning Time: ");
    writeBT(String(warningminutes));
    writeBT(" Min\n");
    writeBT("Flashing Time: ");
    writeBT(String(flashingseconds));
    writeBT(" Sec\n");
    writeBT("Current Meeting Time: ");
    writeBT(String(meetingtime / 60000));
    writeBT(" Min\n");

  } else {
    debugPrint("Error", "unknown command");
  }
}

void runStrip() {
  if (run > 0) {
    for (int x = 0; x < numOfLeds; x++) {
      if (x >= rp && x < (rp + rc)) {
        strip.SetPixelColor(x, currentcolor);
      } else {
        strip.SetPixelColor(x, black);
      }
    }
    strip.Show();
    delay(rdelay);
    if (run == 1) {
      rp++;
      if (rp > numOfLeds - rc) {
        rp--;
        run = 2;
      }
    } else if (run == 2) {
      rp--;
      if (rp < 0) {
        rp = 0;
        run = 1;
      }
    }
  }
}
void blinkStrip() {
  if (blink > 0) {
    if (bp == 0) {
      for (int x = 0; x < numOfLeds; x++) {
        strip.SetPixelColor(x, black);
      }
      if (blink == 3) {
        bp = 2;
      } else {
        bp = 1;
      }

    } else if (bp == 1) {
      for (int x = 0; x < numOfLeds; x++) {
        strip.SetPixelColor(x, currentcolor);
      }
      if (blink == 1) {
        bp = 0;
      } else {
        bp = 2;
      }
    } else if (bp == 2) {
      for (int x = 0; x < numOfLeds; x++) {
        strip.SetPixelColor(x, secondcolor);
      }
      if (blink == 3) {
        bp = 0;
      } else {
        bp = 1;
      }
    }
    strip.Show();
    delay(rdelay);
  }
}

void blinkCount(int color, int count) {
  debugPrint("BlinkStrip", String(count));
  clear();
  for (int i = 0; i < count; i++) {
    for (int x = 0; x < numOfLeds; x++) {
      if (color != 2) {
        strip.SetPixelColor(x, currentcolor);
      } else {
        strip.SetPixelColor(x, secondcolor);
      }
    }
    strip.Show();
    delay(250);
    for (int x = 0; x < numOfLeds; x++) {
      strip.SetPixelColor(x, black);
    }
    strip.Show();
    delay(250);
  }
}



void showRatio(double ratio){

   int ri = (int) round(numOfLeds*ratio);
   if (ri != oldratio){
   clear();
    for (int x = 0; x < numOfLeds; x++) {
      if (x >= ri) {
        strip.SetPixelColor(x, currentcolor);
      } else {
        strip.SetPixelColor(x, secondcolor);
      }
    }
    strip.Show();
   }
   oldratio = ri;
}

void loop() {
  int index = 0;
  // serialEvent();
  //serialBTEvent();
  /*
  if (cmdreceived) {
    if (cmd.length() > 0) {
      index = cmd.indexOf(":");
      String com = cmd.substring(0, index);
      String param = cmd.substring(index + 1);

      handleCommand(com, param);
    }

    cmdreceived = false;
    cmd = "";
  }
  */
  if (run > 0) {
    runStrip();
  } else if (blink > 0) {
    blinkStrip();
  }
  // do meeting stuff

  if (meeting > 0) {
    meetingtime = millis() - start;
    if (meetingtime > lasttime + 1000) {
      lasttime = meetingtime;
    }
    if (meeting == 1) {
      blinkCount(1, 3);
      meeting++;
    } else if (meeting == 2) {
      handleCommand("light", "on");
      meeting++;
    } else if (meeting == 3) {
      if (meetingmode == 0){
        if (meetingtime > (meetingminutes - warningminutes) * 60000) {
          handleCommand("light", "on2");
          meeting++;
      }
      }else{
        double ratio = meetingtime / (meetingminutes * 60000.0- flashingseconds*1000);
        showRatio(ratio);
        if (meetingtime > (meetingminutes * 60000 - flashingseconds*1000)) {
          meeting++;
        }
      }
      
    } else if (meeting == 4) {
      if (meetingtime > (meetingminutes * 60000 - flashingseconds * 1000)) {
        clear();
        run = 0;
        blink = 3;
        meeting++;
      }
    } else if (meeting == 5) {
      if (meetingtime > (meetingminutes * 60000)) {
        clear();
        run = 0;
        blink = 0;
        meeting = 0;
        blinkCount(1, 3);
        writeToBLEDevice("meeting;stop");
      }
    }
  }
}

void serialEvent() {
  if (Serial.available()) {
    while (Serial.available()) {
      char inChar = (char)Serial.read();
      if (inChar == '\n') {
        cmdreceived = true;
      } else {
        if (inChar != '\r') {
          cmd += inChar;
        }
      }
    }
  }
}

void serialBTEvent() {
  while (SerialBT.available() > 0) {
    char inChar = ((char)SerialBT.read());

    if (inChar == ';') {  // Delimiter = ';'
      debugPrint("BTreceived", cmd);
      cmdreceived = true;
      if (cmd.length() > 0) {
        int index = cmd.indexOf(":");
        String com = cmd.substring(0, index);
        String param = cmd.substring(index + 1);

        handleCommand(com, param);
      }

      cmdreceived = false;
      cmd = "";
    } else {
      if (inChar != '\r' && inChar != '\n') {
        cmd += inChar;
      }
    }
  }
}

void setDataFromConfig() {
  data[0] = 1;
  data[1] = currentcolor.R;
  data[2] = currentcolor.G;
  data[3] = currentcolor.B;
  data[4] = secondcolor.R;
  data[5] = secondcolor.G;
  data[6] = secondcolor.B;
  data[7] = meetingminutes;
  data[8] = warningminutes;
  data[9] = flashingseconds;
}

void writeEEPROM() {
  debugPrint("EEPROM", "write");
  // writing byte-by-byte to EEPROM
  setDataFromConfig();
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(addr, data[i]);
    // EEPROM.write(addr, 0);  //clear EEPROM;

    addr += 1;
  }
  EEPROM.commit();
  addr = 0;
}

void setConfigFromData() {
  currentcolor.R = data[1];
  currentcolor.G = data[2];
  currentcolor.B = data[3];
  secondcolor.R = data[4];
  secondcolor.G = data[5];
  secondcolor.B = data[6];
  meetingminutes = data[7];
  warningminutes = data[8];
  flashingseconds = data[9];
}

void readEEPROM() {
  debugPrint("EEPROM", "read");
  // reading byte-by-byte from EEPROM
  for (int i = 0; i < EEPROM_SIZE; i++) {
    byte readValue = EEPROM.read(i);
    if (i == 0 && readValue == 0) {
      debugPrint("EEPROM", "no saved data");
      break;
    } else {
      data[i] = readValue;
      debugPrint("EEPROM read ", String(readValue));
    }
  }
  setConfigFromData();
}