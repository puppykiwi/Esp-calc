#include <LiquidCrystal.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <NetworkUdp.h>
#include <ArduinoOTA.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_chip_info.h>

// --- Network Credentials ---
const char* ssid = "Tenda-354102";
const char* password = "vfr4mju7";
const char* geminiApiKey = "AIzaSyDgFQI4kIupv2oSMSSvLkpSnH4SAnyfyd0";

// --- Hardware & Pin Bindings ---
const int rs = 22, en = 23, d4 = 18, d5 = 19, d6 = 21, d7 = 32;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

const int ROW_COUNT = 7;
const int COL_COUNT = 8;
const int rowPins[ROW_COUNT] = {13, 14, 25, 26, 2, 33, 12};
const int colPins[COL_COUNT] = {4, 5, 15, 16, 17, 27, 34, 35};

const char keyMap[ROW_COUNT][COL_COUNT] = {
  // K21, K22, K23, K24, K31, K32, K33, K34
  {'/', '*', '!', '6', '5', '4', '1', '0'},   // K01
  {'A', 'D', '9', 'A', '8', '7', '2', '.'},   // K02 (D=DEL, A=AC)
  {'m', ',', ')', '(', 'i', '>', '3', 'E'},   // K03 (R=RCL, E=EXP, i=imaginary)
  {'t', 'c', 's', 'H', 'G', '_', '+', '='},   // K04 (t=tan, c=cos, G=DEG, N=Ans)
  {'l', 'L', '^', 'S', 'q', '<', '-', 'N'},   // K11 (l=ln, L=log, S=x^2, q=sqrt, f=frac, _=sub)
  {'>', 'd', '~', 'p', 'e', 'x', ' ', ' '},   // K13 (>=Right, C=nCr, x=inverse)
  {'U', '<', ' ', ' ', '@', '#', ' ', 'A'}    // K14 (<=Left, U=Up, @=ALPHA, #=SHIFT)
};

// --- Core State Machine & Flags ---
enum CalcState { STATE_NORMAL, STATE_SHIFT, STATE_ALPHA, STATE_OTA_MODE };
CalcState currentState = STATE_NORMAL;
bool isHyperbolic = false; // Sequential modifier flag for hyp functions

// --- Memory Safe Buffer ---
const int BUFFER_SIZE = 1024; 
char inputBuffer[BUFFER_SIZE];
int bufferIndex = 0;
int displayWindowStart = 0;

// --- Circular History Buffer ---
const int HISTORY_CAPACITY = 5;      // Number of past calculations/responses to store
const int HISTORY_MAX_LEN = 1024;     // Max characters per history entry (truncates long AI responses to save RAM)
char historyBuffer[HISTORY_CAPACITY][HISTORY_MAX_LEN];
int historyHead = 0;                 // Pointer to where the next entry will be written
int historyCount = 0;                // Total items currently in history
int historyBrowseIndex = -1;         // Tracks where we are when scrolling up/down (-1 means live input)

// --- Predictive Text Engine (Stored in Flash) ---

const char* const predictiveDict[] = {
  "simplex", "calculate", "condition", "constant", "derive", 
  "assignment", "define", "equation", "evaluate", "explain", "find", 
  "primal", "function", "integral", "tableau", "linear", 
  "max", "profit", "solve", "step", "theorem", "variable", 
  "contractor",
  "optimal", "activity", "allocation", "applicant", "artificial", "operations", "angle", "cost", 
  "average", "cpm", "bound", "capacity", "basis", 
  "client", "constraint", "critical", "decision", 
  "degenerate", "demand", "destination", "deterministic", "dual", 
  "case", "center", "circuit",  "class", "earliest",
  "coefficient", "estimate", "expected", "common", "extreme", 
  "component", "feasible", "flow", "configuration", "constant", 
  "continuous", "coupled", "coupling", "critical", "game", 
  "graphical", "heuristic", "cycle", "hungarian", "determine", "increment", "infeasibility", "initial", "integer", 
  "inventory", "direct", "iteration", "job", "latest", 
  "leaving", "limit", "dual", "dynamic", 
  "edge", "efficiency", "markov", "element", 
  "matrix", "energy", "most-likely", "equivalent", "even", 
  "external", "network", "factor", "minimize", "feedback", "filter", "forward", 
  "frequency", "fundamental", "gain", "objective", "optimistic", 
  "origin", "has", "half", "penalty", "pert", "high", 
  "ideal", "pessimistic", "pivot", "policy", "predecessor", 
  "input", "instant", "probability", "problem", "internal",  "profit", 
  "project", "lag", "level", "linear", "load", 
  "loop", "loss", "low", "queueing", "redundant", 
  "maximum", "medium", "requirement", "minimum", "research", "resource",
  "model", "restriction", "salesman", "saving", 
  "schedule", "negative", "network", "sensitivity", "nonlinear", 
  "output", "shadow", "operating", "operation", "order", "shortest", "overall", "simulation", 
  "parallel", "pair", "slack", "solution", "parameter", 
  "source", "peak", "percentage", "period", "phase", 
  "plot", "point", "state", "stochastic", "positive", 
  "potential", "power", "primary", "product", "surplus", 
  "system", "tableau", "quality", "transportation", 
  "resistance", "tree", "rating", "ratio", "transition", 
  "unbounded", "reflected", "region", "relation", "repetition", 
  "unit", "unrestricted", "utilization", "variance", "response", 
  "reverse", "vertex", "rise", "assignment", "root", 
  "allocation", "second", "secondary", "self", "series", 
  "shift", "short", "shunt", "signal", "artificial", 
  "single", "sink", "basic", "slope", "small", 
  "source", "bound", "capacity", "square", 
  "stage", "straight", "sum", "supply", 
  "cost", "degenerate", "demand", "terminal", "deterministic",
  "theorem", "dummy", "earliest", "time", 
  "tolerance", "total",  "transfer", "estimate", 
  "extreme", "feasible", "heuristic", "using", "increment", "upper", 
  "value", "variation", "initial", "iteration", 
  "width", "latest",
};

// --- Flash-Stored Reference Bank ---
// Note: Index matches the physical numeric key (except 3, which is cubed)
const char* const formulaBank[] = {
  "Ideal Gas: PV=nRT",                        // Key 0
  "Impedance: Z=R+i(Xl-Xc)",                  // Key 1 (Circuits & Network Theory)
  "Thermo 1st Law: dU=dQ-dW",                 // Key 2 (Thermodynamics)
  "",                                         // Key 3 (Reserved for ^3)
  "CFG Prod: X -> aXb | eps",                 // Key 4 (Compiler Construction)
  "Power: P=VI*cos(phi)",                     // Key 5 (Power Electronics)
  "Laplace: L{t^n} = n!/s^(n+1)",             // Key 6 
  "Euler: e^(ix) = cos(x)+i*sin(x)",          // Key 7
  "Quad: x=(-b+sqrt(b^2-4ac))/2a",            // Key 8
  "Cap Energy: E=0.5*C*V^2"                   // Key 9
};

const int DICT_SIZE = sizeof(predictiveDict) / sizeof(predictiveDict[0]);

char currentPrediction[16] = ""; // Stores the remaining letters of the matched word

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

// --- OTA Updates ---
uint32_t last_ota_time = 0;

unsigned long lastMetricTime = 0;

void printSystemMetrics() {
  Serial.println("\n=============================================");
  Serial.println("          ESP32 SYSTEM METRICS REPORT        ");
  Serial.println("=============================================");

  // 1. CPU & CLOCK SPECIFICATIONS
  uint32_t cpu_speed = ESP.getCpuFreqMHz();
  Serial.printf("CPU Clock Frequency    : %u MHz\n", cpu_speed);
  Serial.printf("System Uptime          : %lu seconds\n", millis() / 1000);
  
  // Chip Revision Details
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  Serial.printf("CPU Cores              : %d\n", chip_info.cores);

  // 2. SRAM (HEAP MEMORY) METRICS
  uint32_t total_free_heap = ESP.getFreeHeap();
  uint32_t min_free_heap = ESP.getMinFreeHeap();
  uint32_t max_alloc_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

  Serial.println("\n--- SRAM (Heap memory Allocation) ---");
  Serial.printf("Current Free Heap      : %u Bytes (%0.2f KB)\n", total_free_heap, (float)total_free_heap / 1024.0);
  Serial.printf("Historical Minimum Heap: %u Bytes (%0.2f KB)\n", min_free_heap, (float)min_free_heap / 1024.0);
  Serial.printf("Largest Contiguous Block: %u Bytes (%0.2f KB)\n", max_alloc_block, (float)max_alloc_block / 1024.0);
  
  // Quick Fragmentation Check
  float fragmentation = 100.0 * (1.0 - ((float)max_alloc_block / (float)total_free_heap));
  Serial.printf("Heap Fragmentation Est : %0.2f%%\n", fragmentation);

  // 3. FLASH STORAGE FOOTPRINT
  uint32_t sketch_size = ESP.getSketchSize();
  uint32_t free_sketch_space = ESP.getFreeSketchSpace();
  uint32_t total_sketch_space = sketch_size + free_sketch_space;

  Serial.println("\n--- Flash Partition Metrics ---");
  Serial.printf("Used Flash Space       : %u Bytes (%0.2f KB)\n", sketch_size, (float)sketch_size / 1024.0);
  Serial.printf("Free OTA Flash Window  : %u Bytes (%0.2f KB)\n", free_sketch_space, (float)free_sketch_space / 1024.0);
  Serial.printf("Total App Partition Size: %u Bytes (%0.2f KB)\n", total_sketch_space, (float)total_sketch_space / 1024.0);
  
  float flash_usage_percent = ((float)sketch_size / (float)total_sketch_space) * 100.0;
  Serial.printf("Flash Storage Sub-load : %0.2f%%\n", flash_usage_percent);

  // 4. NETWORK SIGNAL INSIGHTS
  Serial.println("\n--- Silicon Network Profile ---");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Wi-Fi Status           : CONNECTED\n");
    Serial.printf("Signal Strength (RSSI) : %d dBm\n", WiFi.RSSI());
    Serial.print( "Local IP Assignment    : "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi Status           : DISCONNECTED");
  }
  Serial.println("=============================================\n");
}

// --- Complex Number Data Representation ---
struct Complex {
  double real;
  double imag;
  Complex(double r = 0, double i = 0) : real(r), imag(i) {}

  Complex operator+(const Complex& other) const { return Complex(real + other.real, imag + other.imag); }
  Complex operator-(const Complex& other) const { return Complex(real - other.real, imag - other.imag); }
  Complex operator*(const Complex& other) const {
    return Complex(real * other.real - imag * other.imag, real * other.imag + imag * other.real);
  }
  Complex operator/(const Complex& other) const {
    double den = other.real * other.real + other.imag * other.imag;
    return Complex((real * other.real + imag * other.imag) / den, (imag * other.real - real * other.imag) / den);
  }
};

enum ParseError {
    PARSE_OK = 0,
    PARSE_ERR_SYNTAX,
    PARSE_ERR_DIV_ZERO,
    PARSE_ERR_PARENS,
    PARSE_ERR_STACK_OVERFLOW
};

// --- Advanced Math Parser ---
class MathEngine {
private:
    static const int MAX_STACK = 32;
    Complex valStack[MAX_STACK]; // Stack handles complex objects natively
    int valTop;
    char opStack[MAX_STACK];
    int opTop;

    bool pushVal(Complex v) {
        if (valTop >= MAX_STACK - 1) return false;
        valStack[++valTop] = v;
        return true;
    }

    bool popVal(Complex& v) {
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

    int getPrecedence(char op) {
        switch (op) {
            case '+': 
            case '-': return 1;
            case '*': 
            case '/': return 2;
            case '^': return 3;
            case 's': // sin  / sinh
            case 'c': // cos  / cosh
            case 't': // tan  / tanh
            case 'S': // asin / asinh
            case 'C': // acos / acosh
            case 'T': // atan / atanh
            case 'l': // ln
            case 'L': // log10
            case 'q': // sqrt
                return 4;
            default:  return 0;
        }
    }

    ParseError applyOperator() {
        char op;
        if (!popOp(op)) return PARSE_ERR_SYNTAX;

        // Unary Evaluation Branch
        if (strchr("sctSCTlLq", op)) {
            Complex a;
            if (!popVal(a)) return PARSE_ERR_SYNTAX;
            Complex result;

            switch(op) {
                case 's': result = Complex(sin(a.real * M_PI / 180.0), 0); break;
                case 'c': result = Complex(cos(a.real * M_PI / 180.0), 0); break;
                case 't': result = Complex(tan(a.real * M_PI / 180.0), 0); break;
                case 'S': result = Complex(asin(a.real) * 180.0 / M_PI, 0); break; 
                case 'C': result = Complex(acos(a.real) * 180.0 / M_PI, 0); break; 
                case 'T': result = Complex(atan(a.real) * 180.0 / M_PI, 0); break; 
                case 'l': result = Complex(log(a.real), 0); break;
                case 'L': result = Complex(log10(a.real), 0); break;
                case 'q': 
                    if (a.real < 0) {
                        result = Complex(0, sqrt(-a.real)); // Imaginary roots evaluation
                    } else {
                        result = Complex(sqrt(a.real), 0);
                    }
                    break;
            }
            return pushVal(result) ? PARSE_OK : PARSE_ERR_STACK_OVERFLOW;
        }

        // Binary Evaluation Branch
        Complex a, b;
        if (!popVal(b) || !popVal(a)) return PARSE_ERR_SYNTAX;
        Complex result;

        switch (op) {
            case '+': result = a + b; break;
            case '-': result = a - b; break;
            case '*': result = a * b; break;
            case '/': 
                if (b.real == 0.0 && b.imag == 0.0) return PARSE_ERR_DIV_ZERO;
                result = a / b; 
                break;
            case '^': 
                result = Complex(pow(a.real, b.real), 0); // Real base power scaling
                break;
            default: return PARSE_ERR_SYNTAX;
        }
        
        return pushVal(result) ? PARSE_OK : PARSE_ERR_STACK_OVERFLOW;
    }

public:
    MathEngine() : valTop(-1), opTop(-1) {}

    ParseError evaluate(const char* expr, Complex& finalResult) {
        valTop = -1;
        opTop = -1;
        const char* p = expr;
        bool lastWasOp = true;

        while (*p != '\0') {
            if (*p == ' ') { p++; continue; }

            // Parse Imaginary Marker Standalone
            if (*p == 'i') {
                if (!pushVal(Complex(0, 1))) return PARSE_ERR_STACK_OVERFLOW;
                p++;
                lastWasOp = false;
                continue;
            }

            if ((*p >= '0' && *p <= '9') || *p == '.') {
                char* nextPtr;
                double val = strtod(p, &nextPtr);
                
                // Tie trailing "i" modifications directly into parsed values
                if (*nextPtr == 'i') {
                    if (!pushVal(Complex(0, val))) return PARSE_ERR_STACK_OVERFLOW;
                    p = nextPtr + 1;
                } else {
                    if (!pushVal(Complex(val, 0))) return PARSE_ERR_STACK_OVERFLOW;
                    p = nextPtr;
                }
                lastWasOp = false;
                continue;
            }

            if (*p >= 'a' && *p <= 'z' || *p >= 'A' && *p <= 'Z') {
                if (strncmp(p, "asinh", 5) == 0) { /* Hyperbolic processing via AI cloud hook or placeholder */ p += 5; }
                else if (strncmp(p, "asin", 4) == 0) { pushOp('S'); p += 4; }
                else if (strncmp(p, "acos", 4) == 0) { pushOp('C'); p += 4; }
                else if (strncmp(p, "atan", 4) == 0) { pushOp('T'); p += 4; }
                else if (strncmp(p, "sin", 3) == 0)  { pushOp('s'); p += 3; }
                else if (strncmp(p, "cos", 3) == 0)  { pushOp('c'); p += 3; }
                else if (strncmp(p, "tan", 3) == 0)  { pushOp('t'); p += 3; }
                else if (strncmp(p, "log", 3) == 0)  { pushOp('L'); p += 3; }
                else if (strncmp(p, "ln", 2) == 0)   { pushOp('l'); p += 2; }
                else if (strncmp(p, "sqrt", 4) == 0) { pushOp('q'); p += 4; }
                else return PARSE_ERR_SYNTAX;
                lastWasOp = true;
                continue;
            }

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
                if (opTop < 0) return PARSE_ERR_PARENS;
                char discard;
                popOp(discard);
                p++;
                lastWasOp = false;
                continue;
            }

            if (*p == '+' || *p == '-' || *p == '*' || *p == '/' || *p == '^') {
                if (*p == '-' && lastWasOp) {
                    if (!pushVal(Complex(0, 0))) return PARSE_ERR_STACK_OVERFLOW;
                } else {
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
            return PARSE_ERR_SYNTAX;
        }

        while (opTop >= 0) {
            if (opStack[opTop] == '(' || opStack[opTop] == ')') return PARSE_ERR_PARENS;
            ParseError err = applyOperator();
            if (err != PARSE_OK) return err;
        }

        if (valTop != 0) return PARSE_ERR_SYNTAX;
        popVal(finalResult);
        return PARSE_OK;
    }
};

void setup() {
  Serial.begin(115200);
  
  lcd.begin(16, 2);
  lcd.cursor();
  lcd.blink();

  for (int i = 0; i < ROW_COUNT; i++) {
    pinMode(rowPins[i], OUTPUT);
    digitalWrite(rowPins[i], HIGH); 
  }
  for (int i = 0; i < COL_COUNT; i++) {
    if (colPins[i] == 34 || colPins[i] == 35) pinMode(colPins[i], INPUT);
    else pinMode(colPins[i], INPUT_PULLUP); 
  }

  memset(inputBuffer, 0, BUFFER_SIZE);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  lcd.print("CAS1O");
  delay(1500);
  
  renderUI();
}

void loop() {
  if (currentState == STATE_OTA_MODE) {
    ArduinoOTA.handle();
  } else {
    scanMatrix();
    checkT9Timeout();

    // Non-blocking background report dump
    if (millis() - lastMetricTime > 60000) { 
      printSystemMetrics();
      lastMetricTime = millis();
    }
  }
}

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

void updatePrediction() {
  memset(currentPrediction, 0, sizeof(currentPrediction));
  if (bufferIndex == 0 || inputBuffer[bufferIndex - 1] == ' ') return;

  // 1. Isolate the current word being typed
  int start = bufferIndex - 1;
  while (start >= 0 && inputBuffer[start] != ' ') {
    start--;
  }
  start++; 
  int len = bufferIndex - start;
  
  if (len == 0) return;

  if (len > 16) len = 16;
  char currentWord[17];
  memset(currentWord, 0, sizeof(currentWord)); // Wipe the local buffer clean
  strncpy(currentWord, &inputBuffer[start], len);
  currentWord[len] = '\0';

  // --- THE FIX: Convert to lowercase for dictionary matching ---
  for(int i = 0; i < len; i++) {
    if(currentWord[i] >= 'A' && currentWord[i] <= 'Z') {
      currentWord[i] += 32; // Fast ASCII shift to lowercase
    }
  }
  // -----------------------------------------------------------

  // 2. Scan Flash array for a prefix match
  for (int i = 0; i < DICT_SIZE; i++) {
    // We can now safely use standard strncmp since cases align
    if (strncmp(currentWord, predictiveDict[i], len) == 0) {
      
      // Match found! Copy the remainder of the dictionary word
      strcpy(currentPrediction, predictiveDict[i] + len);
      
      // (Optional) If you want the autocomplete to append in UPPERCASE to match T9:
      for(int j = 0; j < strlen(currentPrediction); j++) {
         if(currentPrediction[j] >= 'a' && currentPrediction[j] <= 'z') {
             currentPrediction[j] -= 32; 
         }
      }
      
      break; 
    }
  }
}

// --- Dynamic Input Parsing Engine ---
void processKeystroke(char key) {
  
  if (key == 'A') { 
    memset(inputBuffer, 0, BUFFER_SIZE);
    bufferIndex = 0;
    displayWindowStart = 0;
    currentState = STATE_NORMAL;
    isHyperbolic = false;
    resetT9();
    lcd.clear();
    renderUI();
    return;
  }

  if (key == '@') { currentState = (currentState == STATE_ALPHA) ? STATE_NORMAL : STATE_ALPHA; resetT9(); renderUI(); return; }
  if (key == '#') { currentState = (currentState == STATE_SHIFT) ? STATE_NORMAL : STATE_SHIFT; resetT9(); renderUI(); return; }
  if (key == 'p') { startOTAServer(); return; }

  if (key == 'N') {
    memset(currentPrediction, 0, sizeof(currentPrediction));

    if (currentState == STATE_ALPHA) {
      triggerCloudInference();
    } else {
      evaluateLocalExpression();
    }
    currentState = STATE_NORMAL;
    isHyperbolic = false;
    return;
  }

  if (key == 'D') { 
    if (bufferIndex > 0) {
      inputBuffer[--bufferIndex] = '\0';
      if (displayWindowStart > 0 && bufferIndex < displayWindowStart + 16) {
        displayWindowStart--;
      }
    }
    resetT9();
    if (currentState == STATE_ALPHA) updatePrediction();
    renderUI();
    return;
  }

  if (key == 'm') { 
    if (bufferIndex < BUFFER_SIZE - 1) {
      inputBuffer[bufferIndex++] = ' '; // Inject space
      inputBuffer[bufferIndex] = '\0';
      
      // Auto-scroll the view window to keep the cursor visible
      if (bufferIndex > 16) displayWindowStart = bufferIndex - 16;
      
      // Clean up prediction and T9 states for the new word
      if (currentState == STATE_ALPHA) {
        resetT9();
        memset(currentPrediction, 0, sizeof(currentPrediction));
      }
      renderUI();
    }
    return;
  }

  if (key == '<') { if (displayWindowStart > 0) displayWindowStart--; renderUI(); return; }
  if (key == '>') { if (displayWindowStart < bufferIndex - 15) displayWindowStart++; renderUI(); return; }

  if (key == 'E') { 
    if (currentState == STATE_ALPHA && strlen(currentPrediction) > 0) {
          // 1. Inject the prediction and append a space
          int predLen = strlen(currentPrediction);
          if (bufferIndex + predLen + 1 < BUFFER_SIZE - 1) {
              strcat(inputBuffer, currentPrediction);
              strcat(inputBuffer, " "); // Add space to start the next word natively
              bufferIndex = strlen(inputBuffer);
              memset(currentPrediction, 0, sizeof(currentPrediction));

              // 2. NEW AUTO-SCROLL LOGIC
              // Snap the display window to the end of the newly extended buffer
              if (bufferIndex > 16) {
                displayWindowStart = bufferIndex - 16;
              } else {
                displayWindowStart = 0;
              }
          }
        } else {
          // Standard manual viewport slide
          if (displayWindowStart < bufferIndex - 15) displayWindowStart++; 
        }
        renderUI(); 
        return; 
      }

  // Sequential Hyperbolic Handler (Triggers via standard tracking flags)
  if (key == 'H') { 
    isHyperbolic = !isHyperbolic;
    return;
  }

  if (key == '~') { 
    showNetworkStatus(); 
    return; 
  }

  if (key == 'e') { pushToHistory(inputBuffer); return; }

  // --- History Navigation (Up Key) ---
  if (key == 'U') { 
    if (historyCount == 0) return; // Nothing in history

    // If we aren't browsing yet, start at the most recent entry
    if (historyBrowseIndex == -1) {
      historyBrowseIndex = (historyHead - 1 + HISTORY_CAPACITY) % HISTORY_CAPACITY;
    } else {
      // Move backward in time
      int oldestIndex = (historyCount < HISTORY_CAPACITY) ? 0 : historyHead;
      if (historyBrowseIndex != oldestIndex) {
        historyBrowseIndex = (historyBrowseIndex - 1 + HISTORY_CAPACITY) % HISTORY_CAPACITY;
      }
    }

    // Load the history entry into the active buffer
    memset(inputBuffer, 0, BUFFER_SIZE);
    strncpy(inputBuffer, historyBuffer[historyBrowseIndex], BUFFER_SIZE - 1);
    bufferIndex = strlen(inputBuffer);
    displayWindowStart = (bufferIndex > 16) ? bufferIndex - 16 : 0;
    
    renderUI();
    return;
  }

  // --- History Navigation (Down Key - Replace 'd' with your actual down key) ---
  if (key == 'd') {
    if (historyCount == 0 || historyBrowseIndex == -1) return;

    int newestIndex = (historyHead - 1 + HISTORY_CAPACITY) % HISTORY_CAPACITY;
    
    if (historyBrowseIndex == newestIndex) {
      // If we are at the newest entry and hit down, clear the screen back to a fresh prompt
      historyBrowseIndex = -1;
      memset(inputBuffer, 0, BUFFER_SIZE);
      bufferIndex = 0;
      displayWindowStart = 0;
    } else {
      // Move forward in time
      historyBrowseIndex = (historyBrowseIndex + 1) % HISTORY_CAPACITY;
      
      // Load the newer entry
      memset(inputBuffer, 0, BUFFER_SIZE);
      strncpy(inputBuffer, historyBuffer[historyBrowseIndex], BUFFER_SIZE - 1);
      bufferIndex = strlen(inputBuffer);
      displayWindowStart = (bufferIndex > 16) ? bufferIndex - 16 : 0;
    }
    
    renderUI();
    return;
  }

  if (bufferIndex < BUFFER_SIZE - 1) {
    if (currentState == STATE_ALPHA) {
      handleT9Input(key);
      updatePrediction();
    } else {
      const char* expandedString = nullptr;

      if (currentState == STATE_SHIFT) {
          const char* shiftStr = nullptr;
          
          // --- NEW: Formula Bank Routing ---
          // Check if the key is a number between '0' and '9' (excluding '3')
          if (key >= '0' && key <= '9' && key != '3') {
              int formulaIndex = key - '0'; // Fast ASCII math to get the integer
              shiftStr = formulaBank[formulaIndex];
          } 
          // ---------------------------------
          else {
              // Standard Shift Mappings
              switch(key) {
                  case 's': shiftStr = isHyperbolic ? "asinh(" : "asin("; break;
                  case 'c': shiftStr = isHyperbolic ? "acosh(" : "acos("; break;
                  case 't': shiftStr = isHyperbolic ? "atanh(" : "atan("; break;
                  case '3': shiftStr = "^3"; break;     
                  case 'x': shiftStr = "^-1"; break;   
                  case 'i': shiftStr = "i"; break;
              }
          }

          if (shiftStr != nullptr) {
              int len = strlen(shiftStr);
              if (bufferIndex + len < BUFFER_SIZE - 1) {
                  strcat(inputBuffer, shiftStr);
                  bufferIndex += len;
              }
          }
          
          currentState = STATE_NORMAL; 
          isHyperbolic = false;        
      }
      
      else {
          switch(key) {
              case 's': expandedString = isHyperbolic ? "sinh(" : "sin("; break;
              case 'c': expandedString = isHyperbolic ? "cosh(" : "cos("; break;
              case 't': expandedString = isHyperbolic ? "tanh(" : "tan("; break;
              case 'q': expandedString = "sqrt("; break;
              case 'l': expandedString = "ln("; break;
              case 'L': expandedString = "log("; break;
              case 'S': expandedString = "^2"; break;
          }
          isHyperbolic = false; // Reset modifier tracking context
      }

      if (expandedString != nullptr) {
          int len = strlen(expandedString);
          if (bufferIndex + len < BUFFER_SIZE - 1) {
              strcat(inputBuffer, expandedString);
              bufferIndex += len;
          }
      } else {
          inputBuffer[bufferIndex++] = key;
          inputBuffer[bufferIndex] = '\0';
      }
    }
   
    if (bufferIndex > 16) displayWindowStart = bufferIndex - 16;
    renderUI();
  }
}

// --- Real-Time Network Diagnostics ---
void showNetworkStatus() {
  lcd.clear();
  lcd.noBlink(); // Pause the cursor
  
  if (WiFi.status() == WL_CONNECTED) {
    // Top Line: Truncate SSID if it's crazy long
    lcd.setCursor(0, 0);
    String currentSSID = WiFi.SSID();
    if (currentSSID.length() > 16) currentSSID = currentSSID.substring(0, 16);
    lcd.print(currentSSID);

    // Bottom Line: Live RSSI
    lcd.setCursor(0, 1);
    lcd.print("RSSI: ");
    lcd.print(WiFi.RSSI());
    lcd.print(" dBm");
  } else {
    lcd.setCursor(0, 0);
    lcd.print("W1-51 Offline");
    lcd.setCursor(0, 1);
  }

  // Hold the screen so you can actually read it
  delay(2500);

  // Restore the UI state completely
  lcd.clear();
  lcd.blink();
  renderUI();
}

void startOTAServer() {
  currentState = STATE_OTA_MODE;
  lcd.clear();
  lcd.noBlink();
  lcd.print("Initializing OTA");
  if (WiFi.status() != WL_CONNECTED) {
    lcd.setCursor(0, 1);
    lcd.print("Connecting WiFi.");
    while (WiFi.status() != WL_CONNECTED) {
      delay(250);
    }
  }

  ArduinoOTA
    .onStart([]() {
      lcd.clear();
      lcd.print("OTA Update Start");
    })
    .onEnd([]() {
      lcd.clear();
      lcd.print("Update Success!");
      delay(1000);
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      if (millis() - last_ota_time > 250) {
        int percent = progress / (total / 100);
        lcd.setCursor(0, 1);
        lcd.print("Current:");
        lcd.print(percent);
        lcd.print("%   ");
        last_ota_time = millis();
      }
    })
    .onError([](ota_error_t error) {
      lcd.clear();
      lcd.print("OTA Error!");
      delay(3000);
      memset(inputBuffer, 0, BUFFER_SIZE);
      bufferIndex = 0;
      currentState = STATE_NORMAL;
      lcd.blink();
      renderUI();
    });

  ArduinoOTA.begin();
  lcd.clear();
  lcd.print("Ready for Flash ");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP().toString());
}

void pushToHistory(const char* text) {
  if (strlen(text) == 0) return; // Don't save empty states

  // Clear the target slot and copy the string safely
  memset(historyBuffer[historyHead], 0, HISTORY_MAX_LEN);
  strncpy(historyBuffer[historyHead], text, HISTORY_MAX_LEN - 1);
  
  // Advance the head pointer circularly
  historyHead = (historyHead + 1) % HISTORY_CAPACITY;
  
  if (historyCount < HISTORY_CAPACITY) {
    historyCount++;
  }
  
  // Reset the browse state so typing starts fresh
  historyBrowseIndex = -1; 
}

void triggerCloudInference() {
  pushToHistory(inputBuffer);
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

  String url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.1-flash-lite:generateContent?key=";
  url += geminiApiKey;

  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> reqDoc;
  String contextualPrompt = "You are a student sitting for an analogue electronics exam. No markdown. Provide working in sequential order and use abbreviations where possible. limit your entire response to a maximum of 1000 characters:: ";
  contextualPrompt += inputBuffer;
  reqDoc["contents"][0]["parts"][0]["text"] = contextualPrompt;
  
  String requestBody;
  serializeJson(reqDoc, requestBody);

  int retryCount = 0;
  bool connected = false;
  while (retryCount < 5 && !connected) {
    if (http.begin(client, url)) {
      connected = true;
    } else {
      retryCount++;
      delay(500);
    }
  }
  
  if (!connected) {
     strncpy(inputBuffer, "Conn Error -11", 15);
     renderUI();
     return;
  }

  int httpResponseCode = http.POST(requestBody);
  if (httpResponseCode == 200) {
    String rawPayload = http.getString();
    Serial.println("\n========= RAW GEMINI API PAYLOAD =========");
    Serial.println(rawPayload);
    Serial.println("==========================================\n");
    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, rawPayload);

    if (!error) {
      const char* responseText = doc["candidates"][0]["content"]["parts"][0]["text"];
      memset(inputBuffer, 0, BUFFER_SIZE);
      strncpy(inputBuffer, responseText, BUFFER_SIZE - 1);
      Serial.println("Parse SUCCESS.");
      pushToHistory(inputBuffer);
    } else {
      memset(inputBuffer, 0, BUFFER_SIZE);
      strncpy(inputBuffer, "Parse Failed", 17);
      Serial.print("ArduinoJson Error: ");
      Serial.println(error.c_str());
    }
  } else {
    //If it's a 400 or 500 level error, print the body anyway to see why it was rejected
    String errorPayload = http.getString();
    Serial.println("\n========= HTTP ERROR RESPONSE =========");
    Serial.println(errorPayload);
    Serial.println("=======================================\n");
    memset(inputBuffer, 0, BUFFER_SIZE);
    sprintf(inputBuffer, "HTTP ERR: %d", httpResponseCode);
  }
  
  http.end();
  // -- CURSOR RESET LOGIC --
  displayWindowStart = 0; // Locks the screen to the beginning of the text
  //bufferIndex = 0;        // Forces the blinking cursor to the far left (position 0)
  bufferIndex = strlen(inputBuffer);
  renderUI(); 
}

// --- Evaluator Formatted Complex String Rendering ---
void evaluateLocalExpression() {
    MathEngine engine;
    Complex result(0, 0);
    ParseError status = engine.evaluate(inputBuffer, result);

    memset(inputBuffer, 0, BUFFER_SIZE); 
    
    switch(status) {
        case PARSE_OK:
            if (abs(result.imag) > 0.0001) {
                // Dynamically format matching sign signatures for clean real + imaginary viewing
                if (result.imag >= 0) {
                    sprintf(inputBuffer, "%.2f+%.2fi", result.real, result.imag);
                } else {
                    sprintf(inputBuffer, "%.2f%.2fi", result.real, result.imag);
                }
            } else {
                dtostrf(result.real, 6, 4, inputBuffer);
            }
            break;
        case PARSE_ERR_DIV_ZERO: strncpy(inputBuffer, "Math ERROR: Div 0", 17); break;
        case PARSE_ERR_SYNTAX: strncpy(inputBuffer, "Syntax ERROR", 13); break;
        case PARSE_ERR_PARENS: strncpy(inputBuffer, "Parenthesis ERR", 16); break;
        case PARSE_ERR_STACK_OVERFLOW: strncpy(inputBuffer, "Mem Overflow", 13); break;
    }

    bufferIndex = strlen(inputBuffer);
    displayWindowStart = 0;
    renderUI();
}

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
    case '0': { const char chars[] = {'0',',','.','?'}; return chars[cycle % 4]; }
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

void renderUI() {
  lcd.setCursor(0, 0);
  lcd.print("                "); 
  lcd.setCursor(0, 0);

  if (currentState == STATE_ALPHA) {
      if (strlen(currentPrediction) > 0) {
        // Show the suggestion bracketed on the top line
        lcd.print("-> ");
        lcd.print(currentPrediction);
        Serial.println(currentPrediction);
      } else {
        lcd.print("354-(10/2)");
      }
    }
    else if (currentState == STATE_NORMAL) {
      lcd.print(isHyperbolic ? "HYP" : "154-(10/2)");
    }
    else if (currentState == STATE_SHIFT) {
      lcd.print("254-(10/2)");
    }

  lcd.setCursor(0, 1);
  lcd.print("                "); 
  lcd.setCursor(0, 1);
  
  if (displayWindowStart < 0) displayWindowStart = 0;
  if (displayWindowStart > bufferIndex) displayWindowStart = (bufferIndex > 0) ? bufferIndex - 1 : 0;
  
  char viewWindow[17];
  memset(viewWindow, 0, 17);
  
  int charsToCopy = bufferIndex - displayWindowStart;
  if (charsToCopy > 16) charsToCopy = 16;
  if (charsToCopy < 0) charsToCopy = 0;
  
  strncpy(viewWindow, inputBuffer + displayWindowStart, charsToCopy);
  lcd.print(viewWindow);
  int cursorScreenPos = bufferIndex - displayWindowStart;
  if (cursorScreenPos > 15) cursorScreenPos = 15;
  if (cursorScreenPos < 0) cursorScreenPos = 0;
  lcd.setCursor(cursorScreenPos, 1);
}