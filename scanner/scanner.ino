#include <BLEDevice.h>
#include <BLEScan.h>

BLEUUID targetUUID("12345678-1234-1234-1234-1234567890ab");
BLEScan* pBLEScan;

class MyCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice dev) {
        if (dev.haveServiceUUID() && dev.getServiceUUID().equals(targetUUID)) {
            // On envoie un format simple : NOM,RSSI
            Serial.print(dev.getName().c_str());
            Serial.print(",");
            Serial.println(dev.getRSSI());
        }
    }
};

void setup() {
    Serial.begin(115200);
    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99); // Scan presque continu
}

void loop() {
    pBLEScan->start(1, false); // Scan d'une seconde
}