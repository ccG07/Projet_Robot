#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

// --- PINS SPÉCIFIQUES ESP32-C3 ---
#define L_AV 4
#define L_RE 5
#define R_AV 6
#define R_RE 7

const char* ssid = "robot";
const char* password = "pass1234";
const int portUDP = 1234;

WiFiUDP udp;
unsigned long lastPacketTime = 0;
 
void stopperRobot() {
  digitalWrite(L_AV, LOW); digitalWrite(L_RE, LOW);
  digitalWrite(R_AV, LOW); digitalWrite(R_RE, LOW);
}
void setup() {
  Serial.begin(115200);
  Serial.setTimeout(10);
  delay(2000);

  // --- MODE STATION (Se connecte au routeur) ---
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Connexion au WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n✅ CONNECTÉ !");
  Serial.print("Adresse IP du robot : ");
  Serial.println(WiFi.localIP()); // TRÈS IMPORTANT : Note cette IP pour ton Pi

  // Configuration des moteurs
  pinMode(L_AV, OUTPUT); pinMode(L_RE, OUTPUT);
  pinMode(R_AV, OUTPUT); pinMode(R_RE, OUTPUT);
  stopperRobot();
  
  udp.begin(portUDP);
}

void loop() {
  // --- 1. SERIAL HANDSHAKE ---
  if (Serial.available() > 0) {
    if (Serial.readStringUntil('\n').indexOf("CONNECT") >= 0) {
      Serial.println("ACK_PI"); 
    }
  }

  // --- 2. RÉCEPTION UDP ---
  int packetSize = udp.parsePacket();
  if (packetSize) {
    lastPacketTime = millis();
    char buffer[255];
    int len = udp.read(buffer, 255);
    if (len > 0) buffer[len] = 0;

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, buffer);

    if (!error) {
      lastPacketTime = millis(); // ON NE RESET LE TIMER QUE SI LE JSON EST BON
      const char* cmd = doc["cmd"] | "STOP";
      int v = doc["v"] | 255;
      int m_g = doc["m_g"] | 0;
      int m_d = doc["m_d"] | 0;

      Serial.print(" Action Pi: "); Serial.println(cmd);

      // Moteur Gauche (Pins 4 et 5)
      if (m_g == 1) { analogWrite(L_AV, v); digitalWrite(L_RE, LOW); }
      else if (m_g == -1) { digitalWrite(L_AV, LOW); analogWrite(L_RE, v); }
      else { digitalWrite(L_AV, LOW); digitalWrite(L_RE, LOW); }

      // Moteur Droit (Pins 6 et 7)
      if (m_d == 1) { analogWrite(R_AV, v); digitalWrite(R_RE, LOW); }
      else if (m_d == -1) { digitalWrite(R_AV, LOW); analogWrite(R_RE, v); }
      else { digitalWrite(R_AV, LOW); digitalWrite(R_RE, LOW); }
    }
  }

  // --- 3. SÉCURITÉ ---
if (lastPacketTime > 0 && (millis() - lastPacketTime > 1500)) { 
    // On laisse 1.5s de marge au lieu de 1s pour éviter les micro-coupures
    stopperRobot();
    // Serial.println(" Sécurité : Pas de signal UDP");
  }
  
  yield(); // Important pour éviter que le Watchdog ne se fâche
}


// Petite fonction pour nettoyer la loop
void applyMotors(int m_g, int m_d, int v) {
    if (m_g == 1) { analogWrite(L_AV, v); digitalWrite(L_RE, LOW); }
    else if (m_g == -1) { digitalWrite(L_AV, LOW); analogWrite(L_RE, v); }
    else { digitalWrite(L_AV, LOW); digitalWrite(L_RE, LOW); }

    if (m_d == 1) { analogWrite(R_AV, v); digitalWrite(R_RE, LOW); }
    else if (m_d == -1) { digitalWrite(R_AV, LOW); analogWrite(R_RE, v); }
    else { digitalWrite(R_AV, LOW); digitalWrite(R_RE, LOW); }
}