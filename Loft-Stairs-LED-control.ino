//ESP32 Code in Arduino C++ to control a basic, non-addressable, RGB LED strip with its own PSU.
// ***REMEMBER TO CHANGE THE WIFI SSID & WIFI AND OTA PASSWORDS TO SUIT YOUR NETWORK***

#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h> 
#include <time.h>
#include <WebServer.h>

// Add this for telnet support
WiFiServer telnetServer(23);
WiFiClient telnetClient;

// Web server for browser-based serial monitor
WebServer webServer(80);
String logBuffer = "";
const int maxLogLength = 4000; // Keep last 4000 characters

#define RED_PIN 14
#define GREEN_PIN 26
#define BLUE_PIN 33
#define PIR_PIN_1 13  // PIR sensor 1 pin
#define PIR_PIN_2 4   // PIR sensor 2 pin

// Your Wi-Fi credentials
const char *ssid = "YOUR SSID HERE";
const char *password = "Your Password Here";

// Fade State
enum FadeState {
    FADE_IN,
    ON,
    FADE_OUT,
    OFF
};
FadeState currentFadeState = OFF; // Start off

unsigned long stateStartTime = 0;
int currentDutyCycle = 0;
const int fadeInDuration = 1000; // Total fade-in time in milliseconds
const int fadeOutDuration = 2000; // Total fade-out time in milliseconds
const int onDuration = 30000;      // Duration for ON state in milliseconds
const int stepDurationInMs = fadeInDuration / 255; // Time per brightness step

bool fadeActive = false; // To track if the fade effect is active
int activePIR = -1; // Track which PIR is active (-1 = none, 1 = PIR 1, 2 = PIR 2)

// Enhanced debug functions that work with web monitor
void debugPrint(String message) {
    Serial.print(message);           // Still send to USB serial
    if (telnetClient && telnetClient.connected()) {
        telnetClient.print(message); // Also send to telnet
    }
    // Add to web log buffer
    logBuffer += message;
    if (logBuffer.length() > maxLogLength) {
        logBuffer = logBuffer.substring(logBuffer.length() - maxLogLength + 1000);
    }
}

void debugPrintln(String message) {
    Serial.println(message);
    if (telnetClient && telnetClient.connected()) {
        telnetClient.println(message);
    }
    // Add to web log buffer with newline
    logBuffer += message + "\n";
    if (logBuffer.length() > maxLogLength) {
        logBuffer = logBuffer.substring(logBuffer.length() - maxLogLength + 1000);
    }
}

void debugPrintf(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    Serial.print(buffer);
    if (telnetClient && telnetClient.connected()) {
        telnetClient.print(buffer);
    }
    // Add to web log buffer
    logBuffer += String(buffer);
    if (logBuffer.length() > maxLogLength) {
        logBuffer = logBuffer.substring(logBuffer.length() - maxLogLength + 1000);
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(RED_PIN, OUTPUT);
    pinMode(GREEN_PIN, OUTPUT);
    pinMode(BLUE_PIN, OUTPUT);
    pinMode(PIR_PIN_1, INPUT); // PIR input pin 1
    pinMode(PIR_PIN_2, INPUT); // PIR input pin 2

    // Initialize LEDs to off
    analogWrite(RED_PIN, LOW);
    analogWrite(GREEN_PIN, LOW);
    analogWrite(BLUE_PIN, LOW);

    // Connect to Wi-Fi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        debugPrintln("Connecting to WiFi...");
    }
    debugPrintln("Connected to WiFi");
    debugPrint("IP address: ");
    debugPrintln(WiFi.localIP().toString());

    // Start telnet server after WiFi connection
    telnetServer.begin();
    telnetServer.setNoDelay(true);
    debugPrintln("Telnet server started on port 23");

    // Start web server for browser-based serial monitor
    webServer.on("/", []() {
        String html = "<!DOCTYPE html><html><head>";
        html += "<title>ESP32 LED Controller - Serial Monitor</title>";
        html += "<meta http-equiv='refresh' content='2'>";
        html += "<style>";
        html += "body { background: #000; color: #0f0; font-family: 'Courier New', monospace; margin: 20px; }";
        html += "h1 { color: #fff; border-bottom: 2px solid #0f0; padding-bottom: 10px; }";
        html += ".info { color: #ff0; margin-bottom: 15px; }";
        html += "pre { background: #111; padding: 15px; border: 1px solid #333; white-space: pre-wrap; word-wrap: break-word; max-height: 80vh; overflow-y: auto; }";
        html += "</style></head><body>";
        html += "<h1>🔌 ESP32 LED Controller - Live Serial Monitor</h1>";
        html += "<div class='info'>📡 Connected to: " + WiFi.localIP().toString() + " | 🔄 Auto-refresh: 2 seconds | 📅 " + String(__DATE__) + " " + String(__TIME__) + "</div>";
        html += "<pre>" + logBuffer + "</pre>";
        html += "<div class='info'>💡 This page auto-refreshes every 2 seconds. Use your browser's Back button to return.</div>";
        html += "</body></html>";
        webServer.send(200, "text/html", html);
    });
    
    webServer.begin();
    debugPrintln("Web Serial Monitor started!");
    debugPrintf("🌐 Open your browser to: http://%s\n", WiFi.localIP().toString().c_str());
    debugPrintf("📺 This is your OTA Serial Monitor replacement!\n");

    // Configure OTA
    ArduinoOTA.setHostname("ESP32-LED-Controller");
    ArduinoOTA.setPassword("");
    
    ArduinoOTA.onStart([]() {
        debugPrintln("OTA Update Starting...");
    });
    
    ArduinoOTA.onEnd([]() {
        debugPrintln("\nOTA Update Complete!");
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        debugPrintf("Progress: %u%%\r", (progress / (total / 100)));
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        debugPrintf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) debugPrintln("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) debugPrintln("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) debugPrintln("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) debugPrintln("Receive Failed");
        else if (error == OTA_END_ERROR) debugPrintln("End Failed");
    });
    
    ArduinoOTA.begin();
    debugPrintln("OTA Ready");

    // Initialize NTP time - UK timezone (GMT/BST)
    configTime(0, 3600, "pool.ntp.org", "time.nist.gov");

    // Run LED test sequence
    runLEDTest();

    debugPrintln("PIR-Triggered Red LED Fade with Retrigger Initialized");
}

void runLEDTest() {
    debugPrintln("Starting LED test sequence...");
    
    // Test basic colors first
    debugPrintln("Testing RED LEDs...");
    analogWrite(RED_PIN, 255);
    analogWrite(GREEN_PIN, 0);
    analogWrite(BLUE_PIN, 0);
    delay(1000);
    
    debugPrintln("Testing GREEN LEDs...");
    analogWrite(RED_PIN, 0);
    analogWrite(GREEN_PIN, 255);
    analogWrite(BLUE_PIN, 0);
    delay(1000);
    
    debugPrintln("Testing BLUE LEDs...");
    analogWrite(RED_PIN, 0);
    analogWrite(GREEN_PIN, 0);
    analogWrite(BLUE_PIN, 255);
    delay(1000);
    
    // Test BST time-based color settings
    debugPrintln("Testing BST color settings...");
    
    // BST: Green 40%
    debugPrintln("BST: Green 40%...");
    analogWrite(RED_PIN, 0 * (102 / 255.0));
    analogWrite(GREEN_PIN, 255 * (102 / 255.0));
    analogWrite(BLUE_PIN, 0 * (102 / 255.0));
    delay(1000);
    
    // BST: Green 80%
    debugPrintln("BST: Green 80%...");
    analogWrite(RED_PIN, 0 * (204 / 255.0));
    analogWrite(GREEN_PIN, 255 * (204 / 255.0));
    analogWrite(BLUE_PIN, 0 * (204 / 255.0));
    delay(1000);
    
    // BST: Red 40%
    debugPrintln("BST: Red 40%...");
    analogWrite(RED_PIN, 255 * (102 / 255.0));
    analogWrite(GREEN_PIN, 0 * (102 / 255.0));
    analogWrite(BLUE_PIN, 0 * (102 / 255.0));
    delay(1000);
    
    // BST: Orange 55%
    debugPrintln("BST: Orange 55%...");
    analogWrite(RED_PIN, 255 * (140 / 255.0));
    analogWrite(GREEN_PIN, 165 * (140 / 255.0));
    analogWrite(BLUE_PIN, 0 * (140 / 255.0));
    delay(1000);
    
    // BST: White 100%
    debugPrintln("BST: White 100%...");
    analogWrite(RED_PIN, 255 * (255 / 255.0));
    analogWrite(GREEN_PIN, 255 * (255 / 255.0));
    analogWrite(BLUE_PIN, 255 * (255 / 255.0));
    delay(1000);
    
    // BST: Orange 75%
    debugPrintln("BST: Orange 75%...");
    analogWrite(RED_PIN, 255 * (191 / 255.0));
    analogWrite(GREEN_PIN, 165 * (191 / 255.0));
    analogWrite(BLUE_PIN, 0 * (191 / 255.0));
    delay(1000);
    
    // BST: Orange 60%
    debugPrintln("BST: Orange 60%...");
    analogWrite(RED_PIN, 255 * (153 / 255.0));
    analogWrite(GREEN_PIN, 165 * (153 / 255.0));
    analogWrite(BLUE_PIN, 0 * (153 / 255.0));
    delay(1000);
    
    // Test GMT time-based color settings
    debugPrintln("Testing GMT color settings...");
    
    // GMT: Green 40%
    debugPrintln("GMT: Green 40%...");
    analogWrite(RED_PIN, 0 * (102 / 255.0));
    analogWrite(GREEN_PIN, 255 * (102 / 255.0));
    analogWrite(BLUE_PIN, 0 * (102 / 255.0));
    delay(1000);
    
    // GMT: Green 80%
    debugPrintln("GMT: Green 80%...");
    analogWrite(RED_PIN, 0 * (204 / 255.0));
    analogWrite(GREEN_PIN, 255 * (204 / 255.0));
    analogWrite(BLUE_PIN, 0 * (204 / 255.0));
    delay(1000);
    
    // GMT: Red 40% (extended hours)
    debugPrintln("GMT: Red 40% (extended)...");
    analogWrite(RED_PIN, 255 * (102 / 255.0));
    analogWrite(GREEN_PIN, 0 * (102 / 255.0));
    analogWrite(BLUE_PIN, 0 * (102 / 255.0));
    delay(1000);
    
    // GMT: Orange 55%
    debugPrintln("GMT: Orange 55%...");
    analogWrite(RED_PIN, 255 * (140 / 255.0));
    analogWrite(GREEN_PIN, 165 * (140 / 255.0));
    analogWrite(BLUE_PIN, 0 * (140 / 255.0));
    delay(1000);
    
    // GMT: White 100%
    debugPrintln("GMT: White 100%...");
    analogWrite(RED_PIN, 255 * (255 / 255.0));
    analogWrite(GREEN_PIN, 255 * (255 / 255.0));
    analogWrite(BLUE_PIN, 255 * (255 / 255.0));
    delay(1000);
    
    // GMT: Orange 75%
    debugPrintln("GMT: Orange 75%...");
    analogWrite(RED_PIN, 255 * (191 / 255.0));
    analogWrite(GREEN_PIN, 165 * (191 / 255.0));
    analogWrite(BLUE_PIN, 0 * (191 / 255.0));
    delay(1000);
    
    // GMT: Orange 60%
    debugPrintln("GMT: Orange 60%...");
    analogWrite(RED_PIN, 255 * (153 / 255.0));
    analogWrite(GREEN_PIN, 165 * (153 / 255.0));
    analogWrite(BLUE_PIN, 0 * (153 / 255.0));
    delay(1000);
    
    // Turn off all LEDs
    analogWrite(RED_PIN, 0);
    analogWrite(GREEN_PIN, 0);
    analogWrite(BLUE_PIN, 0);
    
    debugPrintln("LED test sequence complete. All BST and GMT settings tested.");
    delay(500);
}

void loop() {
    // Handle telnet connections
    if (telnetServer.hasClient()) {
        if (!telnetClient || !telnetClient.connected()) {
            if (telnetClient) telnetClient.stop();
            telnetClient = telnetServer.available();
            debugPrintln("Telnet client connected!");
        }
    }

    // Handle web server requests
    webServer.handleClient();

    ArduinoOTA.handle();
    
    unsigned long currentMillis = millis();

    // Get current time from the ESP32's system clock
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        debugPrintln("Failed to obtain time");
        return;
    }
    int currentHour = timeinfo.tm_hour; // Current hour in 24-hour format
    int currentMinute = timeinfo.tm_min; // Current minute
    bool isDST = timeinfo.tm_isdst > 0; // Check if currently in daylight saving (BST)

    // Check for PIR trigger from either PIR sensor
    int pirState1 = digitalRead(PIR_PIN_1);
    int pirState2 = digitalRead(PIR_PIN_2);

    if (fadeActive) {
        // If the fade effect is active, only extend the ON state for the active PIR
        if (activePIR == 1 && pirState1 == HIGH) {
            // Triggering PIR 1 again while it is active
            debugPrintln("Retrigger from PIR 1, extending fade ON duration...");
            stateStartTime = currentMillis; // Reset the ON timer
        } else if (activePIR == 2 && pirState2 == HIGH) {
            // Triggering PIR 2 again while it is active
            debugPrintln("Retrigger from PIR 2, extending fade ON duration...");
            stateStartTime = currentMillis; // Reset the ON timer
        }
    } else {
        // Only trigger the fade effect if fade is not active
        if (pirState1 == HIGH && activePIR == -1) {
            // PIR 1 triggered and no fade is active
            debugPrintln("Motion detected by PIR Sensor 1! Activating fade effect...");
            currentFadeState = FADE_IN;
            stateStartTime = currentMillis;
            currentDutyCycle = 0;
            fadeActive = true;
            activePIR = 1; // Set PIR 1 as the active PIR
        } else if (pirState2 == HIGH && activePIR == -1) {
            // PIR 2 triggered and no fade is active
            debugPrintln("Motion detected by PIR Sensor 2! Activating fade effect...");
            currentFadeState = FADE_IN;
            stateStartTime = currentMillis;
            currentDutyCycle = 0;
            fadeActive = true;
            activePIR = 2; // Set PIR 2 as the active PIR
        }
    }

    // Handle the time-based color and brightness
    int red, green, blue, brightness;

    if (isDST) {
        // BST (British Summer Time) settings
        if (currentHour == 4 && currentMinute >= 18 && currentMinute <= 22) {
            // 04:18 - 04:22: Green, 40% brightness
            red = 0; green = 255; blue = 0; brightness = 102;
        } else if (currentHour == 16 && currentMinute >= 18 && currentMinute <= 22) {
            // 16:18 - 16:22: Green, 80% brightness
            red = 0; green = 255; blue = 0; brightness = 204;
        } else if (currentHour >= 0 && currentHour < 5) {
            // 00:00 - 04:59: Red, 40% brightness
            red = 255; green = 0; blue = 0; brightness = 102;
        } else if (currentHour >= 5 && currentHour < 8) {
            // 05:00 - 07:59: Orange, 55% brightness
            red = 255; green = 165; blue = 0; brightness = 140;
        } else if (currentHour >= 8 && currentHour < 17) {
            // 08:00 - 16:59: White, 100% brightness
            red = 255; green = 255; blue = 255; brightness = 255;
        } else if (currentHour >= 17 && currentHour < 22) {
            // 17:00 - 21:59: Orange, 75% brightness
            red = 255; green = 165; blue = 0; brightness = 191;
        } else {
            // 22:00 - 23:59: Orange, 60% brightness
            red = 255; green = 165; blue = 0; brightness = 153;
        }
    } else {
        // GMT (Greenwich Mean Time) settings
        if (currentHour == 4 && currentMinute >= 18 && currentMinute <= 22) {
            // 04:18 - 04:22: Green, 40% brightness
            red = 0; green = 255; blue = 0; brightness = 102;
        } else if (currentHour == 16 && currentMinute >= 18 && currentMinute <= 22) {
            // 16:18 - 16:22: Green, 80% brightness
            red = 0; green = 255; blue = 0; brightness = 204;
        } else if (currentHour >= 0 && currentHour < 7) {
            // 00:00 - 06:59: Red, 40% brightness
            red = 255; green = 0; blue = 0; brightness = 102;
        } else if (currentHour >= 7 && currentHour < 8) {
            // 07:00 - 07:59: Orange, 55% brightness
            red = 255; green = 165; blue = 0; brightness = 140;
        } else if (currentHour >= 8 && currentHour < 16) {
            // 08:00 - 15:59: White, 100% brightness
            red = 255; green = 255; blue = 255; brightness = 255;
        } else if (currentHour >= 16 && currentHour < 22) {
            // 16:00 - 21:59: Orange, 75% brightness
            red = 255; green = 165; blue = 0; brightness = 191;
        } else {
            // 22:00 - 23:59: Orange, 60% brightness
            red = 255; green = 165; blue = 0; brightness = 153;
        }
    }

    // DEBUG: Print time, timezone, and color values when motion is detected
    if (fadeActive) {
        debugPrintf("Time: %02d:%02d (%s) | RGB: %d,%d,%d | Brightness: %d\n", 
                   currentHour, currentMinute, isDST ? "BST" : "GMT", red, green, blue, brightness);
    }

    // Handle fade effect
    if (fadeActive) {
        redFadeEffect(red, green, blue, brightness);  // Trigger fade effect with specified color and brightness
    }
}

void redFadeEffect(int red, int green, int blue, int brightness) {
    unsigned long currentMillis = millis();

    switch (currentFadeState) {
        case FADE_IN:
            if (currentMillis - stateStartTime >= stepDurationInMs) {
                stateStartTime = currentMillis;
                if (currentDutyCycle <= 255) {
                    int redVal = red * (currentDutyCycle / 255.0) * (brightness / 255.0);
                    int greenVal = green * (currentDutyCycle / 255.0) * (brightness / 255.0);
                    int blueVal = blue * (currentDutyCycle / 255.0) * (brightness / 255.0);
                    
                    // DEBUG: Show what values are being sent to pins
                    debugPrintf("FADE_IN - Cycle:%d | Pins-> R:%d G:%d B:%d\n", currentDutyCycle, redVal, greenVal, blueVal);
                    
                    analogWrite(RED_PIN, redVal);
                    analogWrite(GREEN_PIN, greenVal);
                    analogWrite(BLUE_PIN, blueVal);
                    currentDutyCycle++;
                } else {
                    currentFadeState = ON;
                    stateStartTime = currentMillis;
                    debugPrintln("Fade-in complete. LED is now ON.");
                }
            }
            break;

        case ON:
            {
                int redVal = red * (brightness / 255.0);
                int greenVal = green * (brightness / 255.0);
                int blueVal = blue * (brightness / 255.0);
                
                // DEBUG: Show what values are being sent to pins
                debugPrintf("ON - Pins-> R:%d G:%d B:%d\n", redVal, greenVal, blueVal);
                
                analogWrite(RED_PIN, redVal);
                analogWrite(GREEN_PIN, greenVal);
                analogWrite(BLUE_PIN, blueVal);
                
                if (currentMillis - stateStartTime >= onDuration) { // Keep ON for set duration
                    currentFadeState = FADE_OUT;
                    stateStartTime = currentMillis;
                    currentDutyCycle = 255;
                    debugPrintln("ON state finished. Starting fade-out.");
                }
            }
            break;

        case FADE_OUT:
            if (currentMillis - stateStartTime >= (fadeOutDuration / 255)) {
                stateStartTime = currentMillis;
                if (currentDutyCycle >= 0) {
                    int redVal = red * (currentDutyCycle / 255.0) * (brightness / 255.0);
                    int greenVal = green * (currentDutyCycle / 255.0) * (brightness / 255.0);
                    int blueVal = blue * (currentDutyCycle / 255.0) * (brightness / 255.0);
                    
                    analogWrite(RED_PIN, redVal);
                    analogWrite(GREEN_PIN, greenVal);
                    analogWrite(BLUE_PIN, blueVal);
                    currentDutyCycle--;
                } else {
                    currentFadeState = OFF;
                    analogWrite(RED_PIN, LOW);
                    analogWrite(GREEN_PIN, LOW);
                    analogWrite(BLUE_PIN, LOW);
                    debugPrintln("Fade-out complete. LEDs off.");
                    fadeActive = false; // Reset the fade active flag
                    activePIR = -1; // Reset active PIR
                }
            }
            break;

        case OFF:
            // LEDs are already off, no action needed here.
            break;
    }
}
