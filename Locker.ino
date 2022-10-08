#include "Adafruit_Keypad.h"
#include "Adafruit_Fingerprint.h"
#include "ESP32_New_ISR_Servo.h"
#include "SSD1306Wire.h" 
#include "BLEDevice.h"
#include "BLEUtils.h"
#include "BLEServer.h"

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

#define TIMER_INTERRUPT_DEBUG 1
#define ISR_SERVO_DEBUG 1

// Select different ESP32 timer number (0-3) to avoid conflict
#define USE_ESP32_TIMER_NO 1

#define PIN_D5 5 // Pin D5 mapped to pin GPIO5/SPISS/VSPI_SS of ESP32

// Published values for SG90 servos; adjust if needed
#define MIN_MICROS 500 // 544
#define MAX_MICROS 2500

int servoIndex = -1;

#define C1 13
#define C2 12
#define C3 14
#define C4 27
#define R1 26
#define R2 25
#define R3 33
#define R4 32

#define FPM3C_PIN5_RX 16
#define FPM3C_PIN4_TX 17

#define mySerial Serial2

#define NORMAL_MODE 1
#define FINGER_MODE 2
#define ENROLL_MODE 3
#define ADMIN_MODE 4
#define PWD_CHANGE_MODE 5

#define SDA 22
#define SCL 23

// Initialize the OLED display
SSD1306Wire display(0x3c, SDA, SCL);

const byte ROWS = 4; 
const byte COLS = 4;
char keys[ROWS][COLS] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}};
byte rowPins[ROWS] = {R1, R2, R3, R4}; 
byte colPins[COLS] = {C1, C2, C3, C4}; 

Adafruit_Keypad customKeypad = Adafruit_Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

int MODE = NORMAL_MODE;
String stack = "";
String pwd = "000123";
String adminPwd = "263589";

bool str_contains(String stack, String sub);
int getFingerID();
void enroll_fingerprint();
bool get_fingerprint_id();
char pop_stack();
bool isInt(String s);
void open_lock();
String hidden_password(String password);
void process_stack();

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String value = pCharacteristic->getValue().c_str();

      if (value.length() > 0) {
        Serial.println("*********");
        Serial.print("MSG: ");
        for (int i = 0; i < value.length(); i++)
          Serial.print(value[i]);
        Serial.println();
        Serial.println("*********");
        if(str_contains(value, pwd)){
            open_lock();
        }
      }
    }
}; ~

void setup()
{
    Serial.begin(9600);
    while (!Serial);

    delay(200);

    Serial.print(F("\nStarting ISR_MultiServos on ")); Serial.println(ARDUINO_BOARD);
    ESP32_ISR_Servos.useTimer(USE_ESP32_TIMER_NO);

    servoIndex = ESP32_ISR_Servos.setupServo(PIN_D5, MIN_MICROS, MAX_MICROS);

    ESP32_ISR_Servos.disableAll();
    Serial.println("OK");

    if (servoIndex != -1)
        Serial.println(F("Setup Servo OK"));
    else
        Serial.println(F("Setup Servo failed"));

    customKeypad.begin();

    finger.begin(57600);
    delay(5);
    if (finger.verifyPassword())
    {
        Serial.println("Found fingerprint sensor!");
    }
    else
    {
        Serial.println("Did not find fingerprint sensor :(");
        while (1)
        {
            delay(1);
        }
    }


    display.init();

    display.flipScreenVertically();
    display.setFont(ArialMT_Plain_10);

    // pwd = int2string(ReadData("pwd"), 6);
    Serial.println(pwd);

    BLEDevice::init("MyESP32");
    BLEServer *pServer = BLEDevice::createServer();

    BLEService *pService = pServer->createService(SERVICE_UUID);

    BLECharacteristic *pCharacteristic = pService->createCharacteristic(
                                            CHARACTERISTIC_UUID,
                                            BLECharacteristic::PROPERTY_READ |
                                            BLECharacteristic::PROPERTY_WRITE
                                        );

    pCharacteristic->setCallbacks(new MyCallbacks());

    pCharacteristic->setValue("Hello World");
    pService->start();

    BLEAdvertising *pAdvertising = pServer->getAdvertising();
    pAdvertising->start();
}

int getFingerprintID() {
  uint8_t p;
  bool breakLoop = false;
  while(!breakLoop) {
    uint8_t p = finger.getImage();
    switch (p)
    {
    case FINGERPRINT_OK:
        Serial.println("Image taken");
        breakLoop = true;
        break;
    case FINGERPRINT_NOFINGER:
        Serial.println("No finger detected");
        // return p;
        break;
    case FINGERPRINT_PACKETRECIEVEERR:
        Serial.println("Communication error");
        // return p;
        break;
    case FINGERPRINT_IMAGEFAIL:
        Serial.println("Imaging error");
        // return p;
        break;
    default:
        Serial.println("Unknown error");
        // return p;
        break;
    }
    delay(10);
  }

  // OK success!

  p = finger.image2Tz();
  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image converted");
      break;
    case FINGERPRINT_IMAGEMESS:
      Serial.println("Image too messy");
      return -1;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      return -1;
    case FINGERPRINT_FEATUREFAIL:
      Serial.println("Could not find fingerprint features");
      return -1;
    case FINGERPRINT_INVALIDIMAGE:
      Serial.println("Could not find fingerprint features");
      return -1;
    default:
      Serial.println("Unknown error");
      return -1;
  }

  // OK converted!
  p = finger.fingerSearch();
  if (p == FINGERPRINT_OK) {
    Serial.println("Found a print match!");
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
    return -1;
  } else if (p == FINGERPRINT_NOTFOUND) {
    Serial.println("Did not find a match");
    return -1;
  } else {
    Serial.println("Unknown error");
    return -1;
  }

  // found a match!
  Serial.print("Found ID #"); Serial.print(finger.fingerID);
  Serial.print(" with confidence of "); Serial.println(finger.confidence);

  return finger.fingerID;
}

uint8_t getFingerprintEnroll(int id)
{
    display.clear();
    int p = -1;
    Serial.print("Waiting for valid finger to enroll as #");
    Serial.println(id);
    display.drawString(0, 23, "Waiting for finger");
    display.display();
    while (p != FINGERPRINT_OK)
    {
        p = finger.getImage();
        switch (p)
        {
        case FINGERPRINT_OK:
            Serial.println("Image taken");
            break;
        case FINGERPRINT_NOFINGER:
            Serial.println(".");
            break;
        case FINGERPRINT_PACKETRECIEVEERR:
            Serial.println("Communication error");
            break;
        case FINGERPRINT_IMAGEFAIL:
            Serial.println("Imaging error");
            break;
        default:
            Serial.println("Unknown error");
            break;
        }
    }

    // OK success!

    p = finger.image2Tz(1);
    switch (p)
    {
    case FINGERPRINT_OK:
        Serial.println("Image converted");
        break;
    case FINGERPRINT_IMAGEMESS:
        Serial.println("Image too messy");
        return p;
    case FINGERPRINT_PACKETRECIEVEERR:
        Serial.println("Communication error");
        return p;
    case FINGERPRINT_FEATUREFAIL:
        Serial.println("Could not find fingerprint features");
        return p;
    case FINGERPRINT_INVALIDIMAGE:
        Serial.println("Could not find fingerprint features");
        return p;
    default:
        Serial.println("Unknown error");
        return p;
    }

    Serial.println("Remove finger");
    display.clear();
    display.drawString(0, 23, "Remove your finger");
    display.display();
    delay(2000);
    p = 0;
    while (p != FINGERPRINT_NOFINGER)
    {
        p = finger.getImage();
    }
    Serial.print("ID ");
    Serial.println(id);
    p = -1;
    Serial.println("Place same finger again");
    display.clear();
    display.drawString(0, 23, "Place same finger");
    display.display();
    while (p != FINGERPRINT_OK)
    {
        p = finger.getImage();
        switch (p)
        {
        case FINGERPRINT_OK:
            Serial.println("Image taken");
            break;
        case FINGERPRINT_NOFINGER:
            Serial.print(".");
            break;
        case FINGERPRINT_PACKETRECIEVEERR:
            Serial.println("Communication error");
            break;
        case FINGERPRINT_IMAGEFAIL:
            Serial.println("Imaging error");
            break;
        default:
            Serial.println("Unknown error");
            break;
        }
    }

    // OK success!

    p = finger.image2Tz(2);
    switch (p)
    {
    case FINGERPRINT_OK:
        Serial.println("Image converted");
        break;
    case FINGERPRINT_IMAGEMESS:
        Serial.println("Image too messy");
        return p;
    case FINGERPRINT_PACKETRECIEVEERR:
        Serial.println("Communication error");
        return p;
    case FINGERPRINT_FEATUREFAIL:
        Serial.println("Could not find fingerprint features");
        return p;
    case FINGERPRINT_INVALIDIMAGE:
        Serial.println("Could not find fingerprint features");
        return p;
    default:
        Serial.println("Unknown error");
        return p;
    }

    // OK converted!
    Serial.print("Creating model for #");
    Serial.println(id);
    display.clear();
    display.drawString(0, 23, "Creating model");
    display.display();

    p = finger.createModel();
    if (p == FINGERPRINT_OK)
    {
        Serial.println("Prints matched!");
    }
    else if (p == FINGERPRINT_PACKETRECIEVEERR)
    {
        Serial.println("Communication error");
        return p;
    }
    else if (p == FINGERPRINT_ENROLLMISMATCH)
    {
        Serial.println("Fingerprints did not match");
        return p;
    }
    else
    {
        Serial.println("Unknown error");
        return p;
    }

    Serial.print("ID ");
    Serial.println(id);
    p = finger.storeModel(id);
    if (p == FINGERPRINT_OK)
    {
        Serial.println("Stored!");
        display.clear();
        display.drawString(0, 23, "Stored!");
        display.display();
        delay(2000);
        display.clear();
    }
    else
    {
        display.clear();
        display.drawString(0, 23, "Error!");
        display.display();
        delay(2000);
        display.clear();
        if (p == FINGERPRINT_PACKETRECIEVEERR)
        {
            Serial.println("Communication error");
            return p;
        }
        else if (p == FINGERPRINT_BADLOCATION)
        {
            Serial.println("Could not store in that location");
            return p;
        }
        else if (p == FINGERPRINT_FLASHERR)
        {
            Serial.println("Error writing to flash");
            return p;
        }
        else
        {
            Serial.println("Unknown error");
            return p;
        }
    }

    display.clear();
    return true;
}

void enroll_fingerprint()
{   
    finger.getTemplateCount();
    int id = finger.templateCount;
    Serial.print("Enrolling ID #");
    Serial.println(id);
    while (!getFingerprintEnroll(id));
}

bool get_fingerprint_id()
{
    int id = getFingerprintID();
    if (id == -1)
        return false;
    else
    {
        Serial.print("ID #");
        Serial.println(id);
        return true;
    }
}

char pop_stack()
{
    char lastChar = stack[stack.length() - 1];
    stack.remove(stack.length() - 1);
    return lastChar;
}

bool str_contains(String stack, String sub)
{
    int subLen = sub.length();
    int stackLen = stack.length();
    for (int i = 0; stackLen - i >= subLen; i++)
    {
        // Serial.println(stack.substring(i,i+subLen)+"?="+sub);
        if (stack.substring(i, i + subLen) == sub)
            return true;
    }
    return false;
}

bool isInt(String s)
{
    for (int i = 0; i < s.length(); i++)
    {
        if (!isDigit(s.charAt(i)))
            return false;
    }
    return true;
}

void open_lock()
{
    display.clear();
    display.setFont(ArialMT_Plain_24);  
    display.drawString(0, 0, "Welcome\nHome!");
    display.display();
    ESP32_ISR_Servos.enableAll();
    ESP32_ISR_Servos.setPosition(servoIndex, 90);
    delay(1000);
    ESP32_ISR_Servos.disableAll();
    delay(4000);
    ESP32_ISR_Servos.enableAll();
    ESP32_ISR_Servos.setPosition(servoIndex, 0);
    delay(1000);
    ESP32_ISR_Servos.disableAll();
    display.setFont(ArialMT_Plain_10);
    display.clear();
}

String hidden_password(String password)
{
    String result = "";
    for (int i = 0; i < password.length(); i++)
    {
        result += "*";
    }
    return result;
}

void process_stack()
{
    display.clear();
    if (stack.equals(""))
        return;
    char lastChar = stack[stack.length() - 1];
    switch (lastChar)
    {
    case '#':
        pop_stack();
        if (MODE == PWD_CHANGE_MODE)
        {
            if (stack.length() != 6 || !isInt(stack))
            {
                Serial.println("PASSWORD MUST BE 6 DIGITS");
                MODE = ADMIN_MODE;
            }else{
                pwd = stack;
                Serial.print("PASSWORD CHANGED TO ");
                Serial.println(pwd);
                // StoreData("pwd", pwd);
                MODE = ADMIN_MODE;
            }
        }
        else if (str_contains(stack, adminPwd))
        {
            Serial.println("Admin Password Detected");
            MODE = ADMIN_MODE;
        }
        else if (str_contains(stack, pwd))
        {
            Serial.println("Password detected!");
            if (MODE == NORMAL_MODE)
            {
                open_lock();
            }
        }
        else
        {
            Serial.println("Wrong password!");
            display.drawString(0, 12, "Wrong password!");
        }
        stack = "";
        break;
    case '*':
        pop_stack();
        if (stack.length() > 0)
            pop_stack();
        break;
    case 'A':
        switch (MODE)
        {
        case ADMIN_MODE:
            MODE = PWD_CHANGE_MODE;
            break;
        case NORMAL_MODE:
            // MODE = FINGER_MODE;  
            display.clear();
            display.drawString(0, 23, "Put your finger");
            display.display();
            for(int i = 5; i > 0; i--){
                if(get_fingerprint_id()){
                    open_lock();
                    break;
                }else{
                    Serial.println("Fingerprint not found!");
                    display.clear();
                    display.drawString(0, 23, "No match fingerprint! Try again \n You have "+String(i)+" chances left");
                    display.display();
                    delay(1500);
                }
            }
            display.clear();
            break;
        }
        stack = "";
    case 'B':
        switch (MODE)
        {
        case ADMIN_MODE:
            enroll_fingerprint();
            break;
        case NORMAL_MODE:
            break;
        }
        stack = "";
    case 'C':
        switch (MODE)
        {
        case ADMIN_MODE:
            enroll_fingerprint();
            break;
        case NORMAL_MODE:
            break;
        }
        stack = "";
    case 'D':
        switch (MODE)
        {
        case ADMIN_MODE:
            MODE = NORMAL_MODE;
            break;
        case NORMAL_MODE:
            break;
        }
        stack = "";
    default:
        break;
    }

    switch(MODE){
        case ADMIN_MODE:
            display.drawString(0, 23, "ADMIN MODE \nA:CHG PWD B:NEW FNG\nC:NEW NFC D:EXIT");
            break;
        case NORMAL_MODE:
            display.drawString(0, 23, "PASSWORD MODE \nA:Fingerprint B:NFC");
            break;
        case PWD_CHANGE_MODE:
            display.drawString(0, 23, "Please enter new password");
            break;
    }
    display.drawString(0, 0, hidden_password(stack));
    display.display();
}

void loop()
{
    customKeypad.tick();

    display.clear();
    display.drawString(0, 23, "PASSWORD MODE \nA:Fingerprint B:NFC");
    display.display();

    while (customKeypad.available())
    {
        keypadEvent e = customKeypad.read();
        Serial.print((char)e.bit.KEY);
        if (e.bit.EVENT == KEY_JUST_PRESSED)
        {
            Serial.println(" pressed");
        }
        else if (e.bit.EVENT == KEY_JUST_RELEASED)
        {
            Serial.println(" released");
            stack += (char)e.bit.KEY;
            Serial.println(stack);
            process_stack();
        }
    }

    delay(10);
}