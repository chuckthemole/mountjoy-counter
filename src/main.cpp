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
RumpusHttpClient httpClient(*network, &logger);

// ----------------------------
// Pin Manager
// ----------------------------
PinManager pinManager(&logger);

// Logical pin constants
int SENSOR = -1;
int RESET = -1;

// ----------------------------
// Time Management
// ----------------------------
TimeHelper timeHelper;

// ----------------------------
// JSON Document (global)
// ----------------------------
StaticJsonDocument<512> logDoc;

// ----------------------------
// Task Info (set in setup)
// ----------------------------
String currentUser = "Unknown";
String currentNotes = "";

// ----------------------------
// Forward declarations
// ----------------------------
void sendLog();
void configureTaskInfo();

// ----------------------------
// Setup Function
// ----------------------------
int LED_PIN = 13; // Use built-in LED or any free digital pin

void setup()
{
    // Initialize serial logging
    logger.begin();
    logger.info("Starting Item Counter...");

    // Configure LED pin
    pinMode(LED_PIN, OUTPUT);

    // Blink LED 5 times while setup is running
    for (int i = 0; i < 5; i++)
    {
        digitalWrite(LED_PIN, HIGH);
        delay(200);
        digitalWrite(LED_PIN, LOW);
        delay(200);
    }

    // Configure pins
    SENSOR = pinManager.assignPin("SENSOR", UnoR4WiFi::PIN::D2);
    RESET = pinManager.assignPin("RESET", UnoR4WiFi::PIN::D4);
    pinMode(SENSOR, INPUT_PULLUP);
    pinMode(RESET, INPUT_PULLUP);

    // Configure network
    network->setRemote(server, port);
    network->begin();
    network->printStatus();

    // Initialize time
    timeHelper.begin();

    // Initialize HTTP logger
    postHttpLogger.begin();
    httpClient.begin();

    // Fetch connection info and configure task details
    configureTaskInfo();

    // Turn LED on permanently to indicate loop is running
    digitalWrite(LED_PIN, HIGH);
}

// ----------------------------
// Main Loop
// ----------------------------
void loop()
{
    // Item Counting
    bool currentSensorState = digitalRead(SENSOR);
    if (lastSensorState == HIGH && currentSensorState == LOW)
    {
        itemCount++;
        logger.info("Item detected! Count = " + String(itemCount));
    }
    lastSensorState = currentSensorState;

    // Reset button
    bool resetState = digitalRead(RESET);
    if (lastResetState == HIGH && resetState == LOW)
    {
        logger.info("Reset pressed! Sending log...");
        if (network->isConnected())
        {
            sendLog();
            itemCount = 0;
        }
        else
        {
            logger.warn("WiFi not connected, cannot send log.");
        }
    }
    lastResetState = resetState;

    // Maintain network and time
    network->maintainConnection();
    timeHelper.update();
    delay(10);
}

// ----------------------------
// Configure Task Info in Setup
// ----------------------------
void configureTaskInfo()
{
    if (!httpClient.isConnected())
    {
        logger.warn("Cannot fetch task info, network not connected.");
        return;
    }

    // GET request to your task-status endpoint
    String url = String("/api/arduino_consumer/arduino/task-status/") + String(LAN_IP) + "/";
    String response = httpClient.get(url);
    int status = httpClient.lastStatusCode();
    logger.info("HTTP GET " + url + " => " + String(status));

    if (status == 200 && response.length() > 0)
    {
        logger.debug("inside 200");
        StaticJsonDocument<256> doc;
        DeserializationError err = deserializeJson(doc, response);

        if (err)
        {
            logger.warn("JSON deserialization error: " + String(err.c_str()));
            return;
        }

        if (doc.is<JsonArray>() && doc.size() > 0)
        {
            JsonObject task = doc[0].as<JsonObject>(); // cast properly
            currentUser = task["taskName"] | "Unknown";
            currentNotes = task["notes"] | "";
            logger.info("Configured task info: User=" + currentUser + ", Notes=" + currentNotes);
        }
        else
        {
            logger.warn("Task info JSON not an array or empty");
        }
    }
    else
    {
        logger.warn("Failed to fetch task info or empty response.");
    }
}

// ----------------------------
// Send Log to Server
// ----------------------------
void sendLog()
{
    logDoc.clear();
    logDoc["database_id"] = DATABASE_ID;
    logDoc["User"] = currentUser;
    logDoc["Count"] = itemCount;

    JsonObject startTs = logDoc.createNestedObject("Start Timestamp");
    startTs["start"] = timeHelper.getStartTimeISO();

    JsonObject endTs = logDoc.createNestedObject("End Timestamp");
    endTs["start"] = timeHelper.getUTCTimeISO();

    logDoc["Notes"] = currentNotes;

    String payload;
    serializeJson(logDoc, payload);
    logger.debug("Prepared payload: " + payload);
    postHttpLogger.log(payload);
}
