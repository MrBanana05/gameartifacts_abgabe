/*
 * Pins:
 * SDA - See CS_PIN_X
 * SCK - D5
 * MOSI - D7
 * MISO - D6
 * RST - D3
 * 3,3 V - 3,3 V
 * GND - GND
 */

/*
 * Verwendete Quellen:
 * - RFID-Code des vorherigen Semesters
 * - Example "ReadNUID" aus der MFRC522 Library
 * - Example "mqtt_basic" der PubSubClient Library
 */

#include <SPI.h>
#include <MFRC522.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#define RST_PIN D3

// Chip Select Pins der einzelnen RFID Reader
#define CS_PIN_0 D0
#define CS_PIN_1 D1
#define CS_PIN_2 D4
#define CS_PIN_3 D8

#define RFID_COUNT 4

// Wurde Slot X geloest?
bool solved[RFID_COUNT];
// Gesamtes Raetsel geloest?
bool allSolved = false;

MFRC522 rfid[RFID_COUNT];
MFRC522::MIFARE_Key key; 
const byte CS_PINS[4] = {CS_PIN_0, CS_PIN_1, CS_PIN_2, CS_PIN_3};

const String SSID = "gameartifacts";
const String PASSWORD = "blablabla";

// Wird aktiviert, wenn Verbindung zum WLAN fehlschlaegt
// Praktisch zum Testen
bool offlineMode = false;

WiFiClient espClient;
PubSubClient mqttClient(espClient);
const char* MQTT_BROKER = "192.168.22.205";
const char* TOPIC_SOLVED = "/artifakt/rfid/solved";
const char* TOPIC_RESET = "/artifakt/rfid/reset";

// Korrekte Zuordnungen der IDs der Keys
byte nuidTarget[4][7] = 
{
  {0x04, 0xC4, 0xCD, 0x1A, 0x94, 0x11, 0x90},
  {0x04, 0xCA, 0x64, 0x1A, 0x94, 0x11, 0x90},
  {0x04, 0x20, 0xCB, 0x1A, 0x94, 0x11, 0x90},
  {0x04, 0x79, 0x70, 0x1A, 0x94, 0x11, 0x91}
};

// Eingelesenen IDs
byte nuidPICC[4][4];

// Callback Funktion fuer MQTT
void callback(char* topic, byte* payload, unsigned int length) {
  if(strcmp(topic, TOPIC_RESET) == 0) {
    Serial.println("Received RESET message!");
    reset();
  }
  else if(strcmp(topic, TOPIC_SOLVED) == 0)
  {
    Serial.println("Received SOLVED message!");
    if(!allSolved)
    {
      success();
    }
  }
  else
  {
    Serial.print("Received message on unknown topic: ");
    Serial.println(topic);
    Serial.print("Message: ");
    for (int i = 0; i < length; i++) {
      Serial.print((char)payload[i]);
    }
    Serial.println();
  }
}

// Verbinden mit WLAN
void setupWifi ()
{
  Serial.print("Connecting to ");
  Serial.println(SSID);
  WiFi.begin(SSID, PASSWORD);
  int i = 0;
  while(WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
    i++;
    if (i >= 20)
    {
      WiFi.disconnect(true);
      offlineMode = true;
      Serial.println("\nConnection failed, activated offlineMode.");
      return;
    }
  }

  Serial.println("\nWiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

// MQTT konfigurieren
void setupMqtt()
{
  mqttClient.setServer(MQTT_BROKER, 1883);
  mqttClient.setCallback(callback);
}

// Initialisierung der benoetigten Werte, Reader, WLAN, MQTT etc
void setup () 
{ 
  Serial.begin(9600);
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }

  initVars();
  setupWifi();
  if(!offlineMode)
    setupMqtt();
  initRFIDs();
}

// Initialisierung der Pins und anschliessend der RFID Reader
void initRFIDs ()
{
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }

  for(int i = 0; i < 4; i++)
  {
    pinMode(CS_PINS[i], OUTPUT);
    digitalWrite(CS_PINS[i], HIGH);
  }

  delay(500);

  SPI.begin();

  for(int i = 0; i < RFID_COUNT; i++)
  {
    initRFID(i);
  }
}

// Einzelne RFID Reader initialisieren
void initRFID (int index)
{
  Serial.print(F("Starting Reader "));
  Serial.print(index);
  Serial.println(F("..."));
  rfid[index].PCD_Init(CS_PINS[index], RST_PIN);
  
  // Sicherstellen, dass Initialisierung abgeschlossen wurde
  delay(100);

  Serial.print(F("Reader "));
  Serial.print(index);
  Serial.print(F(": "));
  rfid[index].PCD_DumpVersionToSerial();
}

// Initialisieren von Variablen, u.a. benoetigt fuer Reset
void initVars ()
{
  for(int i = 0; i < RFID_COUNT; i++)
  {
    solved[i] = false;
  }
  
  allSolved = false;
}

// Verbinden mit MQTT Broker, falls nicht bereits verbunden
void reconnect() {
	while (!mqttClient.connected()) {
		Serial.print("Reconnecting...");
		if (!mqttClient.connect("RFID_Artifact")) {
			Serial.print("failed, rc=");
			Serial.print(mqttClient.state());
			Serial.println(" retrying in 5 seconds");
			delay(5000);
		}
	}

  mqttClient.subscribe(TOPIC_SOLVED);
  mqttClient.subscribe(TOPIC_RESET);
}

void loop ()
{
  // MQTT behandeln, falls Offline Mode nicht aktiv ist
  if(!offlineMode)
  {
    if (!mqttClient.connected()) {
      reconnect();
    }

    mqttClient.loop();
  }

  // Lesen der Keys, falls Raetsel nicht bereits geloest
  if(!checkSolved())
  {  
    for(int i = 0; i < RFID_COUNT; i++)
    {
      readNUID(rfid[i], i);
    }
  }
}

// Einzeln pruefen, ob Key in Slot i korrekt ist
void checkNUIDs (int i)
{
  if(nuidTarget[i][0] == nuidPICC[i][0] &&
  nuidTarget[i][1] == nuidPICC[i][1] &&
  nuidTarget[i][2] == nuidPICC[i][2] &&
  nuidTarget[i][3] == nuidPICC[i][3])
  {
    Serial.println("Karte " + String(i) + " erkannt!");
    solved[i] = true;
    return;
  }
  else
  {
    solved[i] = false;
  }
  Serial.println("Karte im falschen Slot oder falsche Karte!");
}

// Einlesen einer ID, falls Key vorhanden
void readNUID (MFRC522 rfid, int index)
{
  if (!rfid.PICC_IsNewCardPresent())
  {
    return;
  }

  if (!rfid.PICC_ReadCardSerial())
  {
    return;
  }

  for (byte i = 0; i < 4; i++) {
    nuidPICC[index][i] = rfid.uid.uidByte[i];
  }

  Serial.println("The NUID tag in slot " + String(index) + " is:");
  Serial.print(F("In hex: "));
  printHex(rfid.uid.uidByte, rfid.uid.size);
  Serial.println();

  rfid.PICC_HaltA();
  
  rfid.PCD_StopCrypto1();

  checkNUIDs(index);
}

// Pruefen, ob Raetsel geloest wurde
bool checkSolved ()
{
  for(int i = 0; i < RFID_COUNT; i++)
  {
    if(solved[i] == false)
      return false;
  }
  if(!allSolved)
    success();
  return true;
}

// Ueber Serial und MQTT benachrichtigen, dass Raetsel geloest wurde und weitere Tests deaktivieren
void success ()
{
  Serial.println("Raetsel geloest!");
  allSolved = true;
  mqttClient.publish(TOPIC_SOLVED, "RFID solved!");
}

// Zuruecksetzen des Raetsels
void reset ()
{
  initVars();
}

// Ausgeben einer NUID in Hex
void printHex (byte *buffer, byte bufferSize) 
{
  for (byte i = 0; i < bufferSize; i++) {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], HEX);
  }
}