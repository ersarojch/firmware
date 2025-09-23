#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>

// --- CONFIG ---
const char* ssid = "abc";
const char* password = "123456789";

// GitHub raw links
const char* firmwareUrl = "https://raw.githubusercontent.com/ersarojch/firmware/main/ota.ino.esp32da.bin";
const char* versionUrl  = "https://raw.githubusercontent.com/ersarojch/firmware/main/version.txt";

// ----------------
Preferences prefs;

String latestVersion; // fetched from GitHub
String currentVersion; // stored in preferences

void connectWiFi() {
  Serial.printf("Connecting to WiFi SSID: %s\n", ssid);
  WiFi.begin(ssid, password);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
    if (millis() - start > 30000) {
      Serial.println();
      Serial.println("WiFi connect timeout. Will retry in loop.");
      return;
    }
  }
  Serial.println();
  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());
}

String fetchLatestVersion() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  if (!http.begin(client, versionUrl)) {
    Serial.println("HTTP begin failed for version.txt");
    return "";
  }

  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String ver = http.getString();
    ver.trim(); // remove spaces/newlines
    http.end();
    return ver;
  } else {
    Serial.printf("Failed to fetch version.txt, code: %d\n", httpCode);
  }
  http.end();
  return "";
}

void performOTA() {
  Serial.println("Starting OTA update...");

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  if (!http.begin(client, firmwareUrl)) {
    Serial.println("HTTP begin failed for firmware");
    return;
  }

  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();
    WiFiClient * stream = http.getStreamPtr();

    if (!Update.begin(contentLength)) {
      Serial.printf("Not enough space for OTA. Error: %s\n", Update.errorString());
      http.end();
      return;
    }

    size_t written = Update.writeStream(*stream);
    Serial.printf("Written bytes: %u / %d\n", (unsigned)written, contentLength);

    if (Update.end() && Update.isFinished()) {
      Serial.println("✅ OTA update finished successfully.");

      // save new version permanently
      prefs.begin("ota", false);
      prefs.putString("current_version", latestVersion);
      prefs.putBool("just_updated", true);
      prefs.end();

      Serial.printf("Stored new version: %s in Preferences\n", latestVersion.c_str());

      Serial.println("Restarting to apply new firmware...");
      delay(500);
      ESP.restart();
    } else {
      Serial.printf("Update failed. Error: %s\n", Update.errorString());
    }
  } else {
    Serial.printf("Firmware download failed, HTTP code: %d\n", httpCode);
  }

  http.end();
}


void printPostUpdateMessageIfAny() {
  prefs.begin("ota", false);
  bool justUpdated = prefs.getBool("just_updated", false);
  currentVersion = prefs.getString("current_version", "0.0.0");

  if (justUpdated) {
    Serial.println();
    Serial.println("===== FIRMWARE BOOT AFTER OTA =====");
    Serial.printf("Firmware version: %s\n", currentVersion.c_str());
    Serial.println("✅ Updated Successfully (previous boot was OTA).");
    Serial.println("===================================");
    prefs.remove("just_updated");
  } else {
    Serial.printf("Booting normally. Current firmware: %s\n", currentVersion.c_str());
  }

  prefs.end();
}

void setup() {
  Serial.begin(115200);
  delay(200);

  connectWiFi();
  printPostUpdateMessageIfAny();

  // 1. Fetch latest version from GitHub every boot
  latestVersion = fetchLatestVersion();

  if (latestVersion == "") {
    Serial.println("⚠️ Could not fetch version info. Skipping OTA.");
    return;
  }

  // 2. Load current version from Preferences
  prefs.begin("ota", false);
  currentVersion = prefs.getString("current_version", "0.0.0");
  prefs.end();

  // 3. Compare
  if (latestVersion != currentVersion) {
    Serial.printf("🔔 New firmware available: %s (current: %s)\n",
                  latestVersion.c_str(), currentVersion.c_str());
    performOTA();
  } else {
    Serial.printf("✅ Already on latest firmware: %s\n", currentVersion.c_str());
  }
}

void loop() {
  // Periodically check GitHub for updates again
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 60000) { // check every 60 sec
    lastCheck = millis();
    String newVer = fetchLatestVersion();
    if (newVer != "" && newVer != currentVersion) {
      Serial.printf("🔔 Update found in loop: %s (current: %s)\n", newVer.c_str(), currentVersion.c_str());
      performOTA();
    }
  }

  Serial.printf("Running firmware version: %s\n", currentVersion.c_str());
  delay(5000);
}
