#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>



// Hardware Pin Definitions


// SPI bus (explicit for ESP32-C6)
#define SPI_SCK_PIN          6       // RC522 SCK
#define SPI_MISO_PIN         2       // RC522 MISO
#define SPI_MOSI_PIN         7       // RC522 MOSI
#define SS_PIN               5       // RC522 SDA/SS
#define RST_PIN              4       // RC522 RST

// I2C bus (explicit for ESP32-C6, must not overlap SPI)
#define I2C_SDA_PIN          10      // LCD SDA
#define I2C_SCL_PIN          11      // LCD SCL

// Other peripherals
#define SERVO_PIN            20      // Servo 
#define REGISTER_BUTTON_PIN  8       // Register new card button
#define ERASE_BUTTON_PIN     3       // Erase button



// Hardware Objects

LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo doorServo;
MFRC522 rfid(SS_PIN, RST_PIN);



// System Variables

const int MAX_CARDS = 10;

String registeredCards[MAX_CARDS];
int cardCount     = 0;
int totalEntries  = 0;
int accessGranted = 0;
int accessDenied  = 0;



// WiFi Settings

const char* ssid     = "PUT YOUR OWN WIFI SSID/NAME";
const char* password = "PUT YOUR WIFI PASSWORD";



// ThingSpeak Settings

const char* thingSpeakApiKey = "PUT YOUR OWN API KEY FROM THINGSPEAK";
const char* thingSpeakHost   = "api.thingspeak.com";



// Function Prototypes

void registerCard();
void eraseAllCards();
bool isCardRegistered(String cardID);
String readCardUID();

void grantAccess();
void denyAccess();

void saveCardsToEEPROM();
void loadCardsFromEEPROM();

void writeStringToEEPROM(int addr, String data);
String readStringFromEEPROM(int addr);

void connectWiFi();
void updateThingSpeak();



// Setup

void setup() {

    Serial.begin(9600);

    // Explicit I2C pins required on ESP32-C6
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    // Explicit SPI pins required on ESP32-C6
    SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SS_PIN);

    rfid.PCD_Init();

    lcd.init();
    lcd.backlight();
    lcd.clear();

    lcd.print("RFID System");
    lcd.setCursor(0, 1);
    lcd.print("Initializing...");

    doorServo.attach(SERVO_PIN);
    doorServo.write(0);

    pinMode(REGISTER_BUTTON_PIN, INPUT_PULLUP);
    pinMode(ERASE_BUTTON_PIN, INPUT_PULLUP);

    EEPROM.begin(512);
    loadCardsFromEEPROM();

    connectWiFi();

    delay(2000);

    lcd.clear();
    lcd.print("System Ready");

    updateThingSpeak();
}



// Main Loop

void loop() {

    if (digitalRead(REGISTER_BUTTON_PIN) == LOW) {
        registerCard();
    }

    if (digitalRead(ERASE_BUTTON_PIN) == LOW) {
        eraseAllCards();
    }

    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {

        String cardID = readCardUID();

        rfid.PICC_HaltA();
        rfid.PCD_StopCrypto1();

        totalEntries++;

        if (isCardRegistered(cardID)) {
            grantAccess();
            accessGranted++;
        } else {
            denyAccess();
            accessDenied++;
        }

        updateThingSpeak();
    }
}



// Read RC522 UID

String readCardUID() {

    String uid = "";

    for (byte i = 0; i < rfid.uid.size; i++) {

        if (rfid.uid.uidByte[i] < 0x10) {
            uid += "0";
        }

        uid += String(rfid.uid.uidByte[i], HEX);

        if (i < rfid.uid.size - 1) {
            uid += " ";
        }
    }

    uid.toUpperCase();
    return uid;
}



// Card Registration

void registerCard() {

    lcd.clear();
    lcd.print("Scan to register");

    while (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
        delay(100);
    }

    String cardID = readCardUID();

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();

    if (cardCount < MAX_CARDS) {

        registeredCards[cardCount++] = cardID;

        saveCardsToEEPROM();

        lcd.clear();
        lcd.print("Card registered");
        lcd.setCursor(0, 1);
        lcd.print(cardID);

        updateThingSpeak();

    } else {

        lcd.clear();
        lcd.print("Max cards reached");
    }

    delay(2000);

    lcd.clear();
    lcd.print("System Ready");
}



// Card Erase

void eraseAllCards() {

    cardCount = 0;

    saveCardsToEEPROM();

    lcd.clear();
    lcd.print("All cards erased");

    updateThingSpeak();

    delay(2000);

    lcd.clear();
    lcd.print("System Ready");
}



// Card Verification

bool isCardRegistered(String cardID) {

    for (int i = 0; i < cardCount; i++) {

        if (registeredCards[i] == cardID) {
            return true;
        }
    }

    return false;
}



// Access Control

void grantAccess() {

    lcd.clear();
    lcd.print("Access Granted");

    doorServo.write(90);

    delay(3000);

    doorServo.write(0);

    lcd.clear();
    lcd.print("System Ready");
}


void denyAccess() {

    lcd.clear();
    lcd.print("Access Denied");

    delay(2000);

    lcd.clear();
    lcd.print("System Ready");
}



// EEPROM Save

void saveCardsToEEPROM() {

    for (int i = 0; i < MAX_CARDS; i++) {

        writeStringToEEPROM(
            i * 50,
            (i < cardCount) ? registeredCards[i] : ""
        );
    }

    EEPROM.commit();
}



// EEPROM Load

void loadCardsFromEEPROM() {

    cardCount = 0;

    for (int i = 0; i < MAX_CARDS; i++) {

        String card = readStringFromEEPROM(i * 50);

        if (card.length() > 0) {
            registeredCards[cardCount++] = card;
        }
    }
}



// EEPROM String Writer

void writeStringToEEPROM(int addr, String data) {

    int len = data.length();

    EEPROM.write(addr, len);

    for (int i = 0; i < len; i++) {
        EEPROM.write(addr + 1 + i, data[i]);
    }
}



// EEPROM String Reader

String readStringFromEEPROM(int addr) {

    int len = EEPROM.read(addr);
    String data = "";

    for (int i = 0; i < len; i++) {
        data += char(EEPROM.read(addr + 1 + i));
    }

    return data;
}



// WiFi Connection

void connectWiFi() {

    WiFi.begin(ssid, password);

    lcd.clear();
    lcd.print("Connecting WiFi");

    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        lcd.print(".");
    }

    lcd.clear();
    lcd.print("WiFi Connected");

    delay(1000);
}



// ThingSpeak Upload

void updateThingSpeak() {

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi disconnected. Attempting reconnect...");
        connectWiFi();
        return;
    }

    HTTPClient http;

    String url =
        "http://api.thingspeak.com/update?api_key=" +
        String(thingSpeakApiKey);

    url += "&field1=" + String(cardCount);
    url += "&field2=" + String(totalEntries);
    url += "&field3=" + String(accessGranted);
    url += "&field4=" + String(accessDenied);

    http.begin(url);

    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {

        Serial.println("HTTP Response code: " + String(httpResponseCode));
        Serial.println("ThingSpeak response: " + http.getString());

        lcd.clear();
        lcd.print("Update success");

    } else {

        Serial.print("Error on sending GET: ");
        Serial.println(httpResponseCode);

        lcd.clear();
        lcd.print("Update failed");
    }

    http.end();

    delay(2000);

    lcd.clear();
    lcd.print("System Ready");
}