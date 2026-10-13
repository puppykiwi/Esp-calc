#include <LiquidCrystal.h>

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
  {'t', 'c', 's', ' ', 'G', '-', '+', 'N'},  
  {'l', 'L', '^', 'S', 'q', 'f', '_', '='},  
  {'>', ' ', ' ', ' ', 'C', ' ', ' ', ' '},  
  {' ', '<', 'U', ' ', '@', '#', ' ', ' '}   
};

// --- Core State Machine ---
enum CalcState { STATE_NORMAL, STATE_SHIFT, STATE_ALPHA };
CalcState currentState = STATE_NORMAL;

// --- Memory Safe Buffer (No Strings allowed) ---
const int BUFFER_SIZE = 128;
char inputBuffer[BUFFER_SIZE];
int bufferIndex = 0;
int displayWindowStart = 0;

// --- T9 Multi-Tap Variables ---
unsigned long lastT9PressTime = 0;
char lastT9Key = ' ';
int t9CycleIndex = 0;
const unsigned long T9_TIMEOUT = 800; // 800ms to lock in a character

// --- Scanner Variables ---
char lastScanKey = ' ';
char registeredKey = ' ';
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50; 

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
  renderUI(); // Initial Screen Draw
}

void loop() {
  scanMatrix();
  checkT9Timeout(); // Background task to lock in Alpha characters
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
  delay(10); // EMI mitigation
}

// --- Brain of the Calculator ---
void processKeystroke(char key) {
  
  // 1. System Overrides (All Clear)
  if (key == 'A') { // AC Key
    memset(inputBuffer, 0, BUFFER_SIZE);
    bufferIndex = 0;
    displayWindowStart = 0;
    currentState = STATE_NORMAL;
    resetT9();
    renderUI();
    return;
  }

  // 2. Mode Toggles
  if (key == '@') { // ALPHA Toggle
    currentState = (currentState == STATE_ALPHA) ? STATE_NORMAL : STATE_ALPHA;
    resetT9();
    renderUI();
    return;
  }
  if (key == '#') { // SHIFT Toggle
    currentState = (currentState == STATE_SHIFT) ? STATE_NORMAL : STATE_SHIFT;
    resetT9();
    renderUI();
    return;
  }

  // 3. Deletion Logic
  if (key == 'D') { // DEL Key
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

  // 4. Input Routing (Normal Math vs. T9 Alpha)
  if (bufferIndex < BUFFER_SIZE - 1) {
    if (currentState == STATE_ALPHA) {
      handleT9Input(key);
    } else {
      // Normal Mode: Just insert the character
      inputBuffer[bufferIndex++] = key;
      inputBuffer[bufferIndex] = '\0';
      if (currentState == STATE_SHIFT) currentState = STATE_NORMAL; // Auto-revert shift
    }
    
    // Auto-scroll the display if we type past the 16th character
    if (bufferIndex > 16) displayWindowStart = bufferIndex - 16;
    renderUI();
  }
}

// --- The T9 Engine ---
void handleT9Input(char key) {
  // If hitting the same key rapidly, cycle the character
  if (key == lastT9Key && (millis() - lastT9PressTime) < T9_TIMEOUT) {
    t9CycleIndex++;
    inputBuffer[bufferIndex - 1] = getT9Char(key, t9CycleIndex); // Overwrite last char
  } 
  // If it's a new key, or timeout expired, insert new character
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
  // T9 Dictionary
  switch(key) {
    case '7': { const char chars[] = {'A','B','C','7'}; return chars[cycle % 4]; }
    case '8': { const char chars[] = {'D','E','F','8'}; return chars[cycle % 4]; }
    case '9': { const char chars[] = {'G','H','I','9'}; return chars[cycle % 4]; }
    case '4': { const char chars[] = {'J','K','L','4'}; return chars[cycle % 4]; }
    case '5': { const char chars[] = {'M','N','O','5'}; return chars[cycle % 4]; }
    case '6': { const char chars[] = {'P','Q','R','6'}; return chars[cycle % 4]; }
    case '1': { const char chars[] = {'S','T','U','1'}; return chars[cycle % 4]; }
    case '2': { const char chars[] = {'V','W','X','2'}; return chars[cycle % 4]; }
    case '3': { const char chars[] = {'Y','Z',' ','3'}; return chars[cycle % 4]; }
    case '0': { const char chars[] = {'.',',','?','0'}; return chars[cycle % 4]; }
    default: return key; // Non-numpad keys remain unchanged
  }
}

void checkT9Timeout() {
  if (currentState == STATE_ALPHA && lastT9Key != ' ' && (millis() - lastT9PressTime) > T9_TIMEOUT) {
    resetT9(); // Lock in the character by expiring the timer
  }
}

void resetT9() {
  lastT9Key = ' ';
  t9CycleIndex = 0;
}

// --- Screen Rendering ---
void renderUI() {
  // 1. Draw Top Header (State)
  lcd.setCursor(0, 0);
  lcd.print("                "); // Clear top row
  lcd.setCursor(0, 0);
  
  if (currentState == STATE_NORMAL) lcd.print("[SYS: NORMAL]");
  else if (currentState == STATE_SHIFT) lcd.print("[SYS: SHIFT]");
  else if (currentState == STATE_ALPHA) lcd.print("[SYS: ALPHA]");

  // 2. Draw Bottom Window (Input Buffer)
  lcd.setCursor(0, 1);
  lcd.print("                "); // Clear bottom row
  lcd.setCursor(0, 1);
  
  // Slice the 16-character visible window out of the main array
  char viewWindow[17];
  strncpy(viewWindow, inputBuffer + displayWindowStart, 16);
  viewWindow[16] = '\0'; 
  
  lcd.print(viewWindow);
  
  // Set the hardware cursor to the exact current typing position
  int cursorScreenPos = bufferIndex - displayWindowStart;
  if (cursorScreenPos > 15) cursorScreenPos = 15;
  lcd.setCursor(cursorScreenPos, 1);
}