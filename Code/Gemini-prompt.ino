#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <LiquidCrystal.h> // Include the LCD Library

// Wi-Fi and API Setup
const char* ssid = "Ray 2.4Ghz";
const char* password = "vfr4mju7";
const char* geminiApiKey = "AIzaSyDgFQI4kIupv2oSMSSvLkpSnH4SAnyfyd0"; 

String serverUrl = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=" + String(geminiApiKey);

// Initialize LCD: LiquidCrystal lcd(RS, E, D4, D5, D6, D7)
// Based on your pins: RS=2, E=16, D4=5, D5=18, D6=19, D7=21
// (Remember: RW pin must be moved to GND!)
LiquidCrystal lcd(2, 16, 17, 18, 19, 21);

void setup() {
  Serial.begin(115200);
  
  // Set up the LCD's number of columns and rows:
  lcd.begin(16, 2);
  lcd.clear();
  lcd.print("Booting...");

  WiFi.begin(ssid, password);
  
  Serial.print("Connecting to Wi-Fi");
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");
  
  int col = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    // Print loading dots on the second line of the LCD
    if (col < 16) {
      lcd.setCursor(col, 1);
      lcd.print(".");
      col++;
    }
  }
  
  Serial.println("\nConnected! IP Address: " + WiFi.localIP().toString());
  lcd.clear();
  lcd.print("WiFi Connected!");
  delay(1000);
  
  // Keep the prompt short so the response fits nicely on a tiny screen
  sendGeminiPrompt("Give me a 2-word greeting");
}

void loop() {
  // Empty
}

void sendGeminiPrompt(String prompt) {
  if (WiFi.status() == WL_CONNECTED) {
    lcd.clear();
    lcd.print("Thinking...");

    WiFiClientSecure client;
    client.setInsecure(); 
    HTTPClient http;
    
    if (http.begin(client, serverUrl)) {
      http.addHeader("Content-Type", "application/json");

      DynamicJsonDocument doc(1024);
      doc["contents"][0]["parts"][0]["text"] = prompt;
      
      String requestBody;
      serializeJson(doc, requestBody);

      Serial.println("Sending request to Gemini...");
      int httpResponseCode = http.POST(requestBody);

      if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.println("Response Code: " + String(httpResponseCode));
        parseGeminiResponse(response);
      } else {
        Serial.print("Error on sending POST: ");
        Serial.println(http.errorToString(httpResponseCode).c_str());
        lcd.clear();
        lcd.print("API Error!");
      }
      http.end();
    }
  }
}

void parseGeminiResponse(String jsonPayload) {
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, jsonPayload);

  if (error) {
    Serial.print("JSON parsing failed: ");
    Serial.println(error.c_str());
    lcd.clear();
    lcd.print("Parser Error");
    return;
  }

  if (doc["candidates"][0]["content"]["parts"][0]["text"]) {
    String aiResponse = doc["candidates"][0]["content"]["parts"][0]["text"];
    aiResponse.trim(); // Clean up any leading/trailing newlines or spaces
    
    Serial.println("\n--- Gemini Response ---");
    Serial.println(aiResponse);
    Serial.println("-----------------------\n");

    // Display handling for the 16x2 screen
    lcd.clear();
    
    // If response is longer than 16 characters, split it manually
    if (aiResponse.length() > 16) {
      String line1 = aiResponse.substring(0, 16);
      String line2 = aiResponse.substring(16, 32); // Max 32 characters total fit on screen
      
      lcd.setCursor(0, 0);
      lcd.print(line1);
      lcd.setCursor(0, 1);
      lcd.print(line2);
    } else {
      // Fits entirely on the first line
      lcd.setCursor(0, 0);
      lcd.print(aiResponse);
    }

  } else {
    Serial.println("Unexpected JSON structure.");
    lcd.clear();
    lcd.print("Bad Response");
  }
}