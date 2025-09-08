#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include "config.h"

// ----------------------------
// Custom libraries
// ----------------------------
#include <NetworkManager.h>
#include <WiFi/WiFiNetworkManager.h>
#include <HTTP/PostLogHttp.h>
#include <RumpshiftLogger.h>
#include <TimeHelper.h>
#include <PinManager.h>
#include <Boards/UnoR4WiFi.h>

// ----------------------------
// State Variables
// ----------------------------
unsigned long itemCount = 0; // Current item count
bool lastSensorState = HIGH; // For edge detection
bool lastResetState = HIGH;  // For reset button

// ----------------------------
// WiFi & Network Setup
// ----------------------------
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASS;
const char *server = LAN_IP;
constexpr int port = 8000;

// Initialize logger
RumpshiftLogger logger(BAUD_RATE, DEBUG_LEVEL, true);

// Initialize network manager and HTTP logger
NetworkManager *network = new WiFiNetworkManager(ssid, password, &logger);
PostLogHttp postHttpLogger(*network, &logger, API_PATH, false);

// ----------------------------
// Pin Manager
// ----------------------------
PinManager pinManager(&logger);

// Logical pin constants (will be assigned in setup)
int SENSOR = -1;
int RESET = -1;

// ----------------------------
// Time Management
// ----------------------------
TimeHelper timeHelper; // Handles NTP updates and ISO timestamps

// ----------------------------
// JSON Document (global)
// ----------------------------
StaticJsonDocument<512> logDoc;

// Forward declarations
void sendLog();

// ----------------------------
// Setup Function
// ----------------------------
void setup()
{
    // Initialize serial logging
    logger.begin();
    logger.info("Starting Item Counter...");

    // ----------------------------
    // Assign and configure pins
    // ----------------------------
    SENSOR = pinManager.assignPin("SENSOR", UnoR4WiFi::PIN::D2);
    RESET = pinManager.assignPin("RESET", UnoR4WiFi::PIN::D4);

    pinMode(SENSOR, INPUT_PULLUP);
    pinMode(RESET, INPUT_PULLUP);

    // Configure network
    network->setRemote(server, port);
    network->begin();
    network->printStatus();

    // Initialize NTP client and capture start timestamp
    timeHelper.begin();

    // Initialize HTTP logger
    postHttpLogger.begin();
}

// ----------------------------
// Main Loop
// ----------------------------
void loop()
{
    // --- Item Counting Logic ---
    bool currentSensorState = digitalRead(SENSOR);
    if (lastSensorState == HIGH && currentSensorState == LOW)
    {
        itemCount++;
        logger.info("Item detected! Count = " + String(itemCount));
    }
    lastSensorState = currentSensorState;

    // --- Reset Button Logic ---
    bool resetState = digitalRead(RESET);
    if (lastResetState == HIGH && resetState == LOW)
    {
        logger.info("Reset pressed! Sending log...");

        if (network->isConnected())
        {
            sendLog();     // Send log to server
            itemCount = 0; // Reset counter after sending
        }
        else
        {
            logger.warn("WiFi not connected, cannot send log.");
        }
    }
    lastResetState = resetState;

    // Maintain network and time
    network->maintainConnection();
    timeHelper.update(); // Keep NTP client updated

    delay(10); // Small debounce delay
}

// ----------------------------
// Send Log to Server
// ----------------------------
void sendLog()
{
    // Clear previous contents
    logDoc.clear();

    // Database ID
    logDoc["database_id"] = DATABASE_ID;

    // Properties
    logDoc["User"] = "TestUser";
    logDoc["Count"] = itemCount;

    // Timestamps
    JsonObject startTs = logDoc.createNestedObject("Start Timestamp");
    startTs["start"] = timeHelper.getStartTimeISO();

    JsonObject endTs = logDoc.createNestedObject("End Timestamp");
    endTs["start"] = timeHelper.getUTCTimeISO();

    // Notes
    logDoc["Notes"] = "Automated log entry";

    // Serialize JSON payload
    String payload;
    serializeJson(logDoc, payload);
    logger.debug("Prepared payload: " + payload);

    // Send log
    postHttpLogger.log(payload);
}
