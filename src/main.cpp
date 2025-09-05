#include <Arduino.h>
#include <NetworkManager.h>
#include <WiFi/WiFiNetworkManager.h>
#include <HTTP/PostLogHttp.h>
#include <RumpshiftLogger.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>   // Needed for NTP
#include <NTPClient.h> // Needed for NTP
#include <time.h>
#include "config.h"

// ----------------------------
// Function Declarations
// ----------------------------
void sendLog();
String getUTCTimeISO();

// ----------------------------
// NTP / Time Setup
// ----------------------------
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000); // UTC, refresh every 60s
String startTimeISO;                                    // Start timestamp captured at setup

// ----------------------------
// Pin Definitions
// ----------------------------
const int SENSOR_PIN = 2; // Transistor collector → D2
const int RESET_PIN = 4;  // Button to send log → D4

// ----------------------------
// State Variables
// ----------------------------
unsigned long itemCount = 0; // Current item count
bool lastState = HIGH;       // Last sensor state for edge detection
bool lastResetState = HIGH;  // Last reset button state

// ----------------------------
// WiFi & Network Setup
// ----------------------------
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASS;
const char *server = LAN_IP;
const int port = 8000;

// Initialize logger, network manager, and HTTP logger
RumpshiftLogger logger(BAUD_RATE, DEBUG_LEVEL, true);
NetworkManager *network = new WiFiNetworkManager(ssid, password, &logger);
PostLogHttp postHttpLogger(*network, &logger, API_PATH, false);

// ----------------------------
// JSON Document (global)
// ----------------------------
StaticJsonDocument<512> logDoc;

// ----------------------------
// Setup Function
// ----------------------------
void setup()
{
    // Initialize logger first
    logger.begin();
    logger.info("Starting Item Counter...");

    // Configure pins
    pinMode(SENSOR_PIN, INPUT_PULLUP);
    pinMode(RESET_PIN, INPUT_PULLUP);

    // Configure network
    network->setRemote(server, port);
    network->begin();
    network->printStatus();

    // Initialize NTP client and capture start time
    timeClient.begin();
    timeClient.update();
    startTimeISO = getUTCTimeISO();

    // Initialize HTTP logger
    postHttpLogger.begin();
}

// ----------------------------
// Main Loop
// ----------------------------
void loop()
{
    // --- Item Counting Logic ---
    bool currentState = digitalRead(SENSOR_PIN);
    if (lastState == HIGH && currentState == LOW)
    {
        itemCount++;
        logger.info("Item detected! Count = " + String(itemCount));
    }
    lastState = currentState;

    // --- Reset Button Logic ---
    bool resetState = digitalRead(RESET_PIN);
    if (lastResetState == HIGH && resetState == LOW)
    {
        logger.info("Reset pressed! Sending log...");

        if (network->isConnected())
        {
            sendLog();     // Send log to Notion
            itemCount = 0; // Reset counter after sending
        }
        else
        {
            logger.warn("WiFi not connected, cannot send log.");
        }
    }
    lastResetState = resetState;

    // Maintain network connection
    network->maintainConnection();
    timeClient.update(); // Keep NTP client updated

    delay(10); // Small debounce delay
}

// ----------------------------
// Send Log to Notion
// ----------------------------
void sendLog()
{
    // Clear previous contents
    logDoc.clear();

    // Database ID
    logDoc["database_id"] = DATABASE_ID;

    // Simple properties
    logDoc["User"] = "TestUser";
    logDoc["Count"] = itemCount;

    // Timestamps
    JsonObject startTs = logDoc.createNestedObject("Start Timestamp");
    startTs["start"] = startTimeISO;

    JsonObject endTs = logDoc.createNestedObject("End Timestamp");
    endTs["start"] = getUTCTimeISO();

    // Notes
    logDoc["Notes"] = "Automated log entry";

    // Serialize JSON payload
    String payload;
    serializeJson(logDoc, payload);
    logger.debug("Prepared payload: " + payload);

    // Send log using PostLogHttp
    postHttpLogger.log(payload);
}

// ----------------------------
// Get UTC Time in ISO 8601
// ----------------------------
String getUTCTimeISO()
{
    time_t rawTime = timeClient.getEpochTime();
    struct tm *timeInfo = gmtime(&rawTime);

    char buffer[25];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             timeInfo->tm_year + 1900,
             timeInfo->tm_mon + 1,
             timeInfo->tm_mday,
             timeInfo->tm_hour,
             timeInfo->tm_min,
             timeInfo->tm_sec);

    return String(buffer);
}
