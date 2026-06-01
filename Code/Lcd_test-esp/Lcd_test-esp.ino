#include <LiquidCrystal.h>

// --- Locked Parallel LCD Pin Binding ---
const int rs = 22, en = 23, d4 = 18, d5 = 19, d6 = 21, d7 = 32;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

// --- Matrix Architecture ---
const int ROW_COUNT = 7;
const int COL_COUNT = 8;

// THE PIN SWAP: Moved GPIO 2 to Rows, Moved GPIO 27 to Columns
const int rowPins[ROW_COUNT] = {13, 14, 25, 26, 2, 33, 12}; 
const int colPins[COL_COUNT] = {4, 5, 15, 16, 17, 27, 34, 35}; 

// --- Physical Key Mapping ---
const char keyMap[ROW_COUNT][COL_COUNT] = {
  // K21, K22, K23, K24, K31, K32, K33, K34
  {'/', '*', ' ', '6', '5', '4', '1', '0'},   // K01
  {' ', 'D', '9', 'A', '8', '7', '2', '.'},   // K02 (D=DEL, A=AC)
  {' ', ',', ' ', ' ', ' ', 'R', '3', 'E'},   // K03 (R=RCL, E=EXP)
  {'t', 'c', ' ', ' ', 'G', '-', '+', 'N'},   // K04 (t=tan, c=cos, G=DEG, N=Ans)
  {'l', 'L', '^', 'S', 'q', 'f', '_', '='},   // K11 (l=ln, L=log, S=x^2, q=sqrt, f=frac, _=sub)
  {'>', ' ', ' ', ' ', 'C', ' ', ' ', ' '},   // K13 (>=Right, C=nCr)
  {' ', '<', 'U', ' ', '@', '#', ' ', ' '}    // K14 (<=Left, U=Up, @=ALPHA, #=SHIFT)
};

// --- Fortified Debounce State Tracking ---
char lastScanKey = ' ';
char registeredKey = ' ';
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50; 

void setup() {
  Serial.begin(115200);
  delay(500); 
  
  lcd.begin(16, 2);
  lcd.clear();
  lcd.cursor(); 
  lcd.blink();
  lcd.print("Diagnostics:");
  lcd.setCursor(0, 1);
  lcd.print("Matrix Ready...");
  
  Serial.println("\n--- FX-82MS Swapped-Pin Validation Boot ---");

  // Initialize Row Pins (Outputs)
  for (int i = 0; i < ROW_COUNT; i++) {
    pinMode(rowPins[i], OUTPUT);
    digitalWrite(rowPins[i], HIGH); 
  }

  // Initialize Column Pins (Inputs)
  for (int i = 0; i < COL_COUNT; i++) {
    if (colPins[i] == 34 || colPins[i] == 35) {
      pinMode(colPins[i], INPUT); // Rely on your physical 10k resistors
    } else {
      pinMode(colPins[i], INPUT_PULLUP); 
    }
  }
}

void loop() {
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

  if (currentKey != lastScanKey) {
    lastDebounceTime = millis(); 
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (currentKey != registeredKey) {
      registeredKey = currentKey;
      
      if (registeredKey != ' ') {
        fireDiagnosticOutput(registeredKey); 
      }
    }
  }
  
  lastScanKey = currentKey;
  delay(10); 
}

void fireDiagnosticOutput(char key) {
  Serial.print("Hardware Interrupt -> Key Registered: [ ");
  Serial.print(key);
  Serial.println(" ]");

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Input Detected:");
  lcd.setCursor(0, 1);
  lcd.print("[ ");
  lcd.print(key);
  lcd.print(" ]");
}