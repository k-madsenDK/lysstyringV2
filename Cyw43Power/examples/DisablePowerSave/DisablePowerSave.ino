#include <WiFi.h>
#include <Cyw43Power.h>

const char* ssid = "YOUR_SSID";
const char* pass = "YOUR_PASS";

void setup() {
  Serial.begin(115200);
  Serial.print("Connecting");
  WiFi.begin(ssid, pass);
  for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; i++) {
    Serial.print(".");
    delay(250);
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi failed");
    return;
  }
  Serial.print("IP: "); Serial.println(WiFi.localIP());

  // Disable power save for better stability/latency on some APs
  bool ok = Cyw43Power::setPowerMode(Cyw43Power::NoSave);
  Serial.print("NoSave: "); Serial.println(ok ? "OK" : "UNAVAILABLE");

  int8_t rssi;
  if (Cyw43Power::getRSSI(rssi)) {
    Serial.print("RSSI(dBm)="); Serial.println(rssi);
  }
}

void loop() {
  // Re-apply mode periodically to survive reconnects
  static unsigned long t = 0;
  if (WiFi.status() == WL_CONNECTED && millis() - t > 30000) {
    Cyw43Power::setPowerMode(Cyw43Power::NoSave);
    t = millis();
  }
  delay(50);
}
