#include <LiquidCrystal.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// --- Network Credentials (INSERT YOUR KEYS HERE) ---
const char* ssid = "b3bop";
const char* password = "vfr4mju7";
const int   wifi_channel = 10;
const char* geminiApiKey = "AIzaSyDgFQI4kIupv2oSMSSvLkpSnH4SAnyfyd0";

// --- Hardware & Pin Bindings ---
const int rs = 22, en = 23, d4 = 18, d5 = 19, d6 = 21, d7 = 32;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

const int ROW_COUNT = 7;
const int COL_COUNT = 8;

const int rowPins[ROW_COUNT] = {13, 14, 25, 26, 2, 33, 12}; 
const int colPins[COL_COUNT] = {4, 5, 15, 16, 17, 27, 34, 35}; 

const char keyMap[ROW_COUNT][COL_COUNT] = {
  {'/', '*', ' ', '6', '5', '4', '1', '0'},  
  {' ', 'D', '9', 'A', '8', '7', '2', '.'},  
  {' ', ',', ' ', ' ', ' ', 'R', '3', 'E'},  
  {'t', 'c', 's', ' ', ')', '(', '+', 'N'},  
  {'l', 'L', '^', 'S', 'q', 'f', '-', '='},  
  {'>', ' ', ' ', ' ', 'C', ' ', ' ', ' '},  
  {' ', '<', 'U', ' ', '@', '#', ' ', ' '}   
};

// --- Core State Machine ---
enum CalcState { STATE_NORMAL, STATE_SHIFT, STATE_ALPHA };
CalcState currentState = STATE_NORMAL;

// --- Memory Safe Buffer ---
const int BUFFER_SIZE = 256; // Expanded to handle AI response lengths
char inputBuffer[BUFFER_SIZE];
int bufferIndex = 0;
int displayWindowStart = 0;

// --- T9 Multi-Tap Variables ---
unsigned long lastT9PressTime = 0;
char lastT9Key = ' ';
int t9CycleIndex = 0;
const unsigned long T9_TIMEOUT = 800; 

// --- Scanner Variables ---
char lastScanKey = ' ';
char registeredKey = ' ';
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50; 

//MATH ENGINE
#include <math.h>
#include <string.h>
#include <stdlib.h>

// --- Parser Error States ---
enum ParseError {
    PARSE_OK = 0,
    PARSE_ERR_SYNTAX,
    PARSE_ERR_DIV_ZERO,
    PARSE_ERR_PARENS,
    PARSE_ERR_STACK_OVERFLOW
};

class MathEngine {
private:
    // Fixed stack sizes to guarantee zero heap allocations
    static const int MAX_STACK = 32; 
    
    double valStack[MAX_STACK];
    int valTop;

    char opStack[MAX_STACK];
    int opTop;

    // --- Core Stack Operations ---
    bool pushVal(double v) {
        if (valTop >= MAX_STACK - 1) return false;
        valStack[++valTop] = v;
        return true;
    }

    bool popVal(double& v) {
        if (valTop < 0) return false;
        v = valStack[valTop--];
        return true;
    }

    bool pushOp(char op) {
        if (opTop >= MAX_STACK - 1) return false;
        opStack[++opTop] = op;
        return true;
    }

    bool popOp(char& op) {
        if (opTop < 0) return false;
        op = opStack[opTop--];
        return true;
    }

    // --- Operator Precedence Matrix ---
    int getPrecedence(char op) {
        switch (op) {
            case '+': 
            case '-': return 1;
            case '*': 
            case '/': return 2;
            case '^': return 3;
            // Trigonometry and Logs (Highest precedence as unary operations)
            case 's': // sin
            case 'c': // cos
            case 't': // tan
            case 'l': // ln
            case 'L': // log10
            case 'q': // sqrt
                return 4; 
            default:  return 0;
        }
    }

    // --- Arithmetic Execution Unit ---
    ParseError applyOperator() {
        char op;
        if (!popOp(op)) return PARSE_ERR_SYNTAX;

        // Unary functions (Trig, Log, Sqrt)
        if (op == 's' || op == 'c' || op == 't' || op == 'l' || op == 'L' || op == 'q') {
            double a;
            if (!popVal(a)) return PARSE_ERR_SYNTAX;
            
            double result = 0;
            switch(op) {
                case 's': result = sin(a * M_PI / 180.0); break; // Assuming DEG mode for the Casio
                case 'c': result = cos(a * M_PI / 180.0); break;
                case 't': result = tan(a * M_PI / 180.0); break;
                case 'l': result = log(a); break;
                case 'L': result = log10(a); break;
                case 'q': 
                    if (a < 0) return PARSE_ERR_SYNTAX; // Real numbers only
                    result = sqrt(a); 
                    break;
            }
            return pushVal(result) ? PARSE_OK : PARSE_ERR_STACK_OVERFLOW;
        }

        // Binary functions (+, -, *, /, ^)
        double a, b;
        if (!popVal(b) || !popVal(a)) return PARSE_ERR_SYNTAX;

        double result = 0;
        switch (op) {
            case '+': result = a + b; break;
            case '-': result = a - b; break;
            case '*': result = a * b; break;
            case '/': 
                if (b == 0.0) return PARSE_ERR_DIV_ZERO;
                result = a / b; 
                break;
            case '^': result = pow(a, b); break;
            default: return PARSE_ERR_SYNTAX;
        }
        
        return pushVal(result) ? PARSE_OK : PARSE_ERR_STACK_OVERFLOW;
    }

public:
    MathEngine() : valTop(-1), opTop(-1) {}

    // --- Primary Parsing Engine ---
    ParseError evaluate(const char* expr, double& finalResult) {
        valTop = -1;
        opTop = -1;
        
        const char* p = expr;
        bool lastWasOp = true; // Tracks state to handle Unary Minus

        while (*p != '\0') {
            // 1. Skip Whitespace
            if (*p == ' ') {
                p++;
                continue;
            }

            // 2. Parse Numbers (Decimals handled natively by strtod)
            if ((*p >= '0' && *p <= '9') || *p == '.') {
                char* nextPtr;
                double val = strtod(p, &nextPtr);
                if (!pushVal(val)) return PARSE_ERR_STACK_OVERFLOW;
                p = nextPtr;
                lastWasOp = false;
                continue;
            }

            // 3. Parse Scientific / Trigonometric Functions
            if (*p >= 'a' && *p <= 'z') {
                if (strncmp(p, "sin", 3) == 0) { pushOp('s'); p += 3; }
                else if (strncmp(p, "cos", 3) == 0) { pushOp('c'); p += 3; }
                else if (strncmp(p, "tan", 3) == 0) { pushOp('t'); p += 3; }
                else if (strncmp(p, "log", 3) == 0) { pushOp('L'); p += 3; }
                else if (strncmp(p, "ln", 2) == 0) { pushOp('l'); p += 2; }
                else if (strncmp(p, "sqrt", 4) == 0) { pushOp('q'); p += 4; }
                else return PARSE_ERR_SYNTAX;
                
                lastWasOp = true;
                continue;
            }

            // 4. Handle Parentheses
            if (*p == '(') {
                if (!pushOp(*p)) return PARSE_ERR_STACK_OVERFLOW;
                p++;
                lastWasOp = true;
                continue;
            }

            if (*p == ')') {
                while (opTop >= 0 && opStack[opTop] != '(') {
                    ParseError err = applyOperator();
                    if (err != PARSE_OK) return err;
                }
                if (opTop < 0) return PARSE_ERR_PARENS; // Mismatched parentheses
                
                char discard;
                popOp(discard); // Discard the '('
                p++;
                lastWasOp = false;
                continue;
            }

            // 5. Handle Operators
            if (*p == '+' || *p == '-' || *p == '*' || *p == '/' || *p == '^') {
                
                // --- UNARY MINUS HACK ---
                // If the minus sign is at the start, or immediately follows an operator/parenthesis,
                // we push a '0' to the value stack to turn it into a standard subtraction (0 - x).
                if (*p == '-' && lastWasOp) {
                    if (!pushVal(0.0)) return PARSE_ERR_STACK_OVERFLOW;
                } 
                else {
                    // Standard Operator Precedence checking
                    while (opTop >= 0 && opStack[opTop] != '(' && 
                           getPrecedence(opStack[opTop]) >= getPrecedence(*p)) {
                        ParseError err = applyOperator();
                        if (err != PARSE_OK) return err;
                    }
                }
                
                if (!pushOp(*p)) return PARSE_ERR_STACK_OVERFLOW;
                p++;
                lastWasOp = true;
                continue;
            }

            // Unrecognized character
            return PARSE_ERR_SYNTAX;
        }

        // 6. Flush remaining operators in the stack
        while (opTop >= 0) {
            if (opStack[opTop] == '(' || opStack[opTop] == ')') return PARSE_ERR_PARENS;
            ParseError err = applyOperator();
            if (err != PARSE_OK) return err;
        }

        // 7. Final Output Extraction
        if (valTop != 0) return PARSE_ERR_SYNTAX; // Stack should have exactly 1 value left
        
        popVal(finalResult);
        return PARSE_OK;
    }
};

void setup() {
  Serial.begin(115200);
  
  lcd.begin(16, 2);
  lcd.cursor();
  lcd.blink();
  
  // Matrix Hardware Init
  for (int i = 0; i < ROW_COUNT; i++) {
    pinMode(rowPins[i], OUTPUT);
    digitalWrite(rowPins[i], HIGH); 
  }
  for (int i = 0; i < COL_COUNT; i++) {
    if (colPins[i] == 34 || colPins[i] == 35) pinMode(colPins[i], INPUT); 
    else pinMode(colPins[i], INPUT_PULLUP); 
  }

  memset(inputBuffer, 0, BUFFER_SIZE);
  
  // Background Wi-Fi Boot
  WiFi.begin(ssid, password, wifi_channel);
  lcd.print("CAS1O");
  delay(1500); // Allow physical voltages to settle
  
  renderUI(); 
}

void loop() {
  scanMatrix();
  checkT9Timeout(); 
}

// --- Background Matrix Scanner ---
void scanMatrix() {
  char currentKey = ' ';
  
  for (int r = 0; r < ROW_COUNT; r++) {
    digitalWrite(rowPins[r], LOW); 
    delayMicroseconds(20); 
    
    for (int c = 0; c < COL_COUNT; c++) {
      if (digitalRead(colPins[c]) == LOW) { 
        currentKey = keyMap[r][c];
      }
    }
    digitalWrite(rowPins[r], HIGH); 
  }

  if (currentKey != lastScanKey) lastDebounceTime = millis(); 

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (currentKey != registeredKey) {
      registeredKey = currentKey;
      if (registeredKey != ' ') {
        processKeystroke(registeredKey); 
      }
    }
  }
  lastScanKey = currentKey;
  delay(10); 
}

// --- Brain of the Calculator ---
void processKeystroke(char key) {
  
  if (key == 'C') { // AC Key
    memset(inputBuffer, 0, BUFFER_SIZE);
    bufferIndex = 0;
    displayWindowStart = 0;
    currentState = STATE_NORMAL;
    resetT9();
    renderUI();
    return;
  }

  if (key == '@') { currentState = (currentState == STATE_ALPHA) ? STATE_NORMAL : STATE_ALPHA; resetT9(); renderUI(); return; }
  if (key == '#') { currentState = (currentState == STATE_SHIFT) ? STATE_NORMAL : STATE_SHIFT; resetT9(); renderUI(); return; }

  // Execute Routing
  if (key == '=') {
    if (currentState == STATE_ALPHA) {
      triggerCloudInference(); 
    } else {
      evaluateLocalExpression();
    }
    currentState = STATE_NORMAL;
    return;
  }

  // Deletion Logic
  if (key == 'D') { 
    if (bufferIndex > 0) {
      inputBuffer[--bufferIndex] = '\0';
      if (displayWindowStart > 0 && bufferIndex < displayWindowStart + 16) {
        displayWindowStart--;
      }
    }
    resetT9();
    renderUI();
    return;
  }

  // Sliding Viewport Navigation (Arrow Keys)
  if (key == '<') { if (displayWindowStart > 0) displayWindowStart--; renderUI(); return; }
  if (key == '>') { if (displayWindowStart < bufferIndex - 15) displayWindowStart++; renderUI(); return; }

  // Input Routing
  //
  //if (bufferIndex < BUFFER_SIZE - 1) {
  //  if (currentState == STATE_ALPHA) {
  //    handleT9Input(key);
  //  } else {
  //    inputBuffer[bufferIndex++] = key;
  //    inputBuffer[bufferIndex] = '\0';
  //    if (currentState == STATE_SHIFT) currentState = STATE_NORMAL; 
  //  }
  //  
  //  if (bufferIndex > 16) displayWindowStart = bufferIndex - 16;
  //  renderUI();
  //}

  // 4. Input Routing (Normal Math vs. T9 Alpha)
  if (bufferIndex < BUFFER_SIZE - 1) {
    if (currentState == STATE_ALPHA) {
      handleT9Input(key);
    } else {
      // --- NEW: Macro Expansion for Math Functions ---
      const char* expandedString = nullptr;
      switch(key) {
          case 's': expandedString = "sin("; break; // Make sure 's' is mapped to your Sine key!
          case 'c': expandedString = "cos("; break;
          case 't': expandedString = "tan("; break;
          case 'q': expandedString = "sqrt("; break;
          case 'l': expandedString = "ln("; break;
          case 'L': expandedString = "log("; break;
          case 'S': expandedString = "sqr("; break;
      }

      if (expandedString != nullptr) {
          // Inject the full word and auto-open the parenthesis
          int len = strlen(expandedString);
          if (bufferIndex + len < BUFFER_SIZE - 1) {
              strcat(inputBuffer, expandedString);
              bufferIndex += len;
          }
      } else {
          // Normal single-character (numbers/operators) insertion
          inputBuffer[bufferIndex++] = key;
          inputBuffer[bufferIndex] = '\0';
      }
      
      if (currentState == STATE_SHIFT) currentState = STATE_NORMAL; 
    }
    
    // Auto-scroll the display if we type past the 16th character
    if (bufferIndex > 16) displayWindowStart = bufferIndex - 16;
    renderUI();
  }
}

// --- The Network & AI Pipeline ---
void triggerCloudInference() {
  lcd.clear();
  lcd.print("N3tw0rk Ch3ck");
  
  if (WiFi.status() != WL_CONNECTED) {
    lcd.setCursor(0, 1);
    lcd.print("ERR: No W1-51");
    delay(2000);
    renderUI();
    return;
  }

  lcd.setCursor(0, 1);
  lcd.print("B3B0P");

  WiFiClientSecure client;
  client.setInsecure(); 
  HTTPClient http;

  String url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=";
  url += geminiApiKey;

  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> reqDoc;
  String contextualPrompt = "Solve concisely. No markdown. Provide just enough information. Math and working only where necessary: ";
  contextualPrompt += inputBuffer;
  reqDoc["contents"][0]["parts"][0]["text"] = contextualPrompt;
  
  String requestBody;
  serializeJson(reqDoc, requestBody);

  int httpResponseCode = http.POST(requestBody);

  if (httpResponseCode == 200) {
    // --- DEBUG INJECTION START ---
    // Instead of streaming, we pull the entire payload into a String
    String rawPayload = http.getString();
    
    //Serial.println("\n========= RAW GEMINI API PAYLOAD =========");
    //Serial.println(rawPayload);
    //erial.println("==========================================\n");

    // Bumped memory allocation temporarily just in case it's a size issue
    DynamicJsonDocument doc(4096); 
    DeserializationError error = deserializeJson(doc, rawPayload);

    if (!error) {
      const char* responseText = doc["candidates"][0]["content"]["parts"][0]["text"];
      
      memset(inputBuffer, 0, BUFFER_SIZE);
      strncpy(inputBuffer, responseText, BUFFER_SIZE - 1);
      bufferIndex = strlen(inputBuffer);
      displayWindowStart = 0; 
      //Serial.println("Parse SUCCESS.");
      
    } else {
      // Print the exact reason the parse failed
      //Serial.print("ArduinoJson Error: ");
      //Serial.println(error.c_str());
      
      memset(inputBuffer, 0, BUFFER_SIZE);
      strncpy(inputBuffer, "Parse Failed", 17);
    }
    // --- DEBUG INJECTION END ---
    
  } else {
    // If it's a 400 or 500 level error, print the body anyway to see why it was rejected
    String errorPayload = http.getString();
    //Serial.println("\n========= HTTP ERROR RESPONSE =========");
    //Serial.println(errorPayload);
    //Serial.println("=======================================\n");
    
    memset(inputBuffer, 0, BUFFER_SIZE);
    sprintf(inputBuffer, "HTTP ERR: %d", httpResponseCode);
  }
  
  http.end();
  bufferIndex = strlen(inputBuffer);
  renderUI(); 
}

void evaluateLocalExpression() {
    MathEngine engine;
    double result = 0.0;

    ParseError status = engine.evaluate(inputBuffer, result);

    memset(inputBuffer, 0, BUFFER_SIZE); // Clear buffer for output
    
    switch(status) {
        case PARSE_OK:
            // dtostrf is highly optimized for AVR/ESP arrays (avoids sprintf float bloating)
            dtostrf(result, 6, 4, inputBuffer); 
            break;
        case PARSE_ERR_DIV_ZERO:
            strncpy(inputBuffer, "Math ERROR: Div 0", 17);
            break;
        case PARSE_ERR_SYNTAX:
            strncpy(inputBuffer, "Syntax ERROR", 13);
            break;
        case PARSE_ERR_PARENS:
            strncpy(inputBuffer, "Parenthesis ERR", 16);
            break;
        case PARSE_ERR_STACK_OVERFLOW:
            strncpy(inputBuffer, "Mem Overflow", 13);
            break;
    }

    bufferIndex = strlen(inputBuffer);
    displayWindowStart = 0;
    renderUI();
}

// --- The T9 Engine ---
void handleT9Input(char key) {
  if (key == lastT9Key && (millis() - lastT9PressTime) < T9_TIMEOUT) {
    t9CycleIndex++;
    inputBuffer[bufferIndex - 1] = getT9Char(key, t9CycleIndex); 
  } 
  else {
    t9CycleIndex = 0;
    char mappedChar = getT9Char(key, t9CycleIndex);
    inputBuffer[bufferIndex++] = mappedChar;
    inputBuffer[bufferIndex] = '\0';
  }
  lastT9Key = key;
  lastT9PressTime = millis();
}

char getT9Char(char key, int cycle) {
  switch(key) {
    case '7': { const char chars[] = {'7','A','B','C'}; return chars[cycle % 4]; }
    case '8': { const char chars[] = {'8','D','E','F'}; return chars[cycle % 4]; }
    case '9': { const char chars[] = {'9','G','H','I'}; return chars[cycle % 4]; }
    case '4': { const char chars[] = {'4','J','K','L'}; return chars[cycle % 4]; }
    case '5': { const char chars[] = {'5','M','N','O'}; return chars[cycle % 4]; }
    case '6': { const char chars[] = {'6','P','Q','R'}; return chars[cycle % 4]; }
    case '1': { const char chars[] = {'1','S','T','U'}; return chars[cycle % 4]; }
    case '2': { const char chars[] = {'2','V','W','X'}; return chars[cycle % 4]; }
    case '3': { const char chars[] = {'3','Y','Z',' '}; return chars[cycle % 4]; }
    case '0': { const char chars[] = {'0','(',')','?'}; return chars[cycle % 4]; }
    default: return key; 
  }
}

void checkT9Timeout() {
  if (currentState == STATE_ALPHA && lastT9Key != ' ' && (millis() - lastT9PressTime) > T9_TIMEOUT) {
    resetT9(); 
  }
}

void resetT9() {
  lastT9Key = ' ';
  t9CycleIndex = 0;
}

// --- Screen Rendering ---
void renderUI() {
  lcd.setCursor(0, 0);
  lcd.print("                "); 
  lcd.setCursor(0, 0);
  
  if (currentState == STATE_NORMAL) lcd.print("154-(10/2)");
  else if (currentState == STATE_SHIFT) lcd.print("254-(10/2)");
  else if (currentState == STATE_ALPHA) lcd.print("354-(10/2)");

  lcd.setCursor(0, 1);
  lcd.print("                "); 
  lcd.setCursor(0, 1);
  
  char viewWindow[17];
  strncpy(viewWindow, inputBuffer + displayWindowStart, 16);
  viewWindow[16] = '\0'; 
  
  lcd.print(viewWindow);
  
  int cursorScreenPos = bufferIndex - displayWindowStart;
  if (cursorScreenPos > 15) cursorScreenPos = 15;
  lcd.setCursor(cursorScreenPos, 1);
}