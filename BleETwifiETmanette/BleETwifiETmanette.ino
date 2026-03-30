#include <WiFi.h>
#include <AsyncUDP.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// --- Configuration des Pins (Pont en H) ---
#define M_GAUCHE_1 4
#define M_GAUCHE_2 5
#define M_DROIT_1 6
#define M_DROIT_2 7

// --- Paramètres Réseau ---
const char* ssid = "ROBOT_ADAM_PEAK";
const char* password = "pass1234";
const int portUDP = 1234;

// --- Paramètres GPS (BLE) ---
BLEUUID targetUUID("12345678-1234-1234-1234-1234567890ab");
unsigned long lastScanTime = 0;

// --- Objets Globaux ---
AsyncUDP udp;
BLEScan* pBLEScan;

// --- Prototypes des fonctions ---
void faireAvancer(int v); void faireReculer(int v);
void tournerGauche(int v); void tournerDroite(int v);
void stopperRobot();

// --- Classe Callback pour le BLE (Évite les crashs Wi-Fi) ---
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      if (advertisedDevice.haveServiceUUID() && advertisedDevice.getServiceUUID().equals(targetUUID)) {
        int rssi = advertisedDevice.getRSSI();
        String nom = advertisedDevice.haveName() ? advertisedDevice.getName().c_str() : "Balise_Cégep";
        
        // Envoi immédiat des données GPS au Pi
        String gpsJson = "{\"type\":\"GPS\", \"balise\":\"" + nom + "\", \"rssi\":" + String(rssi) + "}";
        udp.broadcastTo(gpsJson.c_str(), portUDP);
        Serial.println("GPS -> Pi: " + nom + " (" + String(rssi) + "dBm)");
      }
    }
};

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Configuration des moteurs
  pinMode(M_GAUCHE_1, OUTPUT); pinMode(M_GAUCHE_2, OUTPUT);
  pinMode(M_DROIT_1, OUTPUT); pinMode(M_DROIT_2, OUTPUT);
  stopperRobot();

  // 1. Reset Forcé de la Radio (Pour éviter le "fantôme" MESH)
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  delay(500);
  
  if(WiFi.softAP(ssid, password, 11, 0, 4)) {
    Serial.println(">>>> Wi-Fi PEAK ACTIF : " + String(ssid));
    Serial.print("IP : "); Serial.println(WiFi.softAPIP());
  }

  // 2. Configuration UDP
  if(udp.listen(portUDP)) {
    udp.onPacket([](AsyncUDPPacket packet) {
      StaticJsonDocument<200> doc;
      DeserializationError error = deserializeJson(doc, packet.data(), packet.length());
      
      if (!error) {
        const char* cmd = doc["cmd"];
        int v = doc["v"]; 

        if (strcmp(cmd, "AVANCE") == 0) faireAvancer(v);
        else if (strcmp(cmd, "RECULER") == 0) faireReculer(v);
        else if (strcmp(cmd, "GAUCHE") == 0) tournerGauche(v);
        else if (strcmp(cmd, "DROITE") == 0) tournerDroite(v);
        else if (strcmp(cmd, "STOP") == 0) stopperRobot();
      }
    });
  }

  // 3. Configuration BLE (Mode Asynchrone)
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(150);
  pBLEScan->setWindow(100);

  Serial.println(">>>> SYSTÈME TOTALEMENT PRÊT <<<<");
}

void loop() {
  // Scan BLE toutes les 10 secondes (pour laisser le Wi-Fi respirer)
  if (millis() - lastScanTime > 10000) {
    Serial.println("Scan GPS en cours...");
    // Le 'false' à la fin indique un scan non-bloquant
    pBLEScan->start(1, nullptr, false); 
    lastScanTime = millis();
  }
}

// --- Implémentation des Moteurs ---
void faireAvancer(int v) {
  analogWrite(M_GAUCHE_1, v); digitalWrite(M_GAUCHE_2, LOW);
  analogWrite(M_DROIT_1, v); digitalWrite(M_DROIT_2, LOW);
  Serial.println("Moteurs: AVANCE");
}

void faireReculer(int v) {
  digitalWrite(M_GAUCHE_1, LOW); analogWrite(M_GAUCHE_2, v);
  digitalWrite(M_DROIT_1, LOW); analogWrite(M_DROIT_2, v);
  Serial.println("Moteurs: RECULER");
}

void tournerGauche(int v) {
  analogWrite(M_GAUCHE_1, 0); analogWrite(M_DROIT_1, v);
  digitalWrite(M_DROIT_2, LOW);
  Serial.println("Moteurs: GAUCHE");
}

void tournerDroite(int v) {
  analogWrite(M_GAUCHE_1, v); digitalWrite(M_GAUCHE_2, LOW);
  analogWrite(M_DROIT_1, 0);
  Serial.println("Moteurs: DROITE");
}

void stopperRobot() {
  digitalWrite(M_GAUCHE_1, LOW); digitalWrite(M_GAUCHE_2, LOW);
  digitalWrite(M_DROIT_1, LOW); digitalWrite(M_DROIT_2, LOW);
  Serial.println("Moteurs: STOP");
}