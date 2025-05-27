#include <Arduino.h>
#include <ShiftRegister74HC595.h>

#define RED1 21
#define GREEN1 18
#define YELLOW1 19
#define RED2 16
#define GREEN2 17
#define YELLOW2 5

ShiftRegister74HC595<8> sr2(2, 0, 4);
ShiftRegister74HC595<8> sr1(12, 14, 27); 

// Traffic light system variables
volatile int count1 = 0;
volatile int count2 = 0;
volatile int prevCount1 = 0;
volatile int prevCount2 = 0;
unsigned long prevTime1 = 0;
unsigned long prevTime2 = 0;
const long interval = 1000;
int countdown1 = 1;
int countdown2 = 1;
int currLight1 = RED1;
int currLight2 = RED2;
bool changingLight1 = false;
bool changingLight2 = false;

// Yellow blinking mode configuration
const unsigned long YELLOW_START_HOUR = 22;
const unsigned long YELLOW_START_MINUTE = 00;
const unsigned long YELLOW_END_HOUR = 22;
const unsigned long YELLOW_END_MINUTE = 04; //(5h sáng)
const unsigned long BLINK_INTERVAL = 500;
unsigned long lastBlinkTime = 0;
bool isYellowMode = false;
bool yellowState = false;

// 7-segment display codes
uint8_t digitCode[] = {
  B11000000, B11111001, B10100100, B10110000, B10011001,
  B10010010, B10000010, B11111000, B10000000, B10011000
};

// Time tracking variables
unsigned long startMillis;
unsigned long baseHour = 21;    // Starting hour for testing
unsigned long baseMinute = 59;   // Starting minute for testing
unsigned long elapsedMinutes = 0;
unsigned long prevMinute = 99;  // For tracking minute changes

void getCurrentTime(unsigned long& hour, unsigned long& minute) {
  // Calculate elapsed time since start
  unsigned long currentMillis = millis();
  elapsedMinutes = (currentMillis - startMillis) / 60000; // Convert to minutes
  
  // Calculate current hour and minute
  unsigned long totalMinutes = baseMinute + elapsedMinutes;
  hour = (baseHour + (totalMinutes / 60)) % 24;
  minute = totalMinutes % 60;
}

void setup() {
  Serial.begin(115200);
  pinMode(RED1, OUTPUT);
  pinMode(GREEN1, OUTPUT);
  pinMode(YELLOW1, OUTPUT);
  pinMode(RED2, OUTPUT);
  pinMode(GREEN2, OUTPUT);
  pinMode(YELLOW2, OUTPUT);
  
  startMillis = millis();
  resetLights();
}

void loop() {
  unsigned long currentHour, currentMinute;
  getCurrentTime(currentHour, currentMinute);
  
  // Check if it's time for yellow blinking mode
  if (isYellowBlinkingTime(currentHour, currentMinute)) {
    if (!isYellowMode) {
      enterYellowMode();
    }
    handleYellowBlinking(millis());
  } else {
    if (isYellowMode) {
      exitYellowMode();
    }
    handleNormalOperation(millis());
  }
}

bool isYellowBlinkingTime(unsigned long currentHour, unsigned long currentMinute) {
  unsigned long startTimeInMinutes = YELLOW_START_HOUR * 60 + YELLOW_START_MINUTE;
  unsigned long endTimeInMinutes = YELLOW_END_HOUR * 60 + YELLOW_END_MINUTE;
  unsigned long currentTimeInMinutes = currentHour * 60 + currentMinute;
  
  bool isInYellowPeriod = (currentTimeInMinutes >= startTimeInMinutes && 
                          currentTimeInMinutes < endTimeInMinutes);
                          
  return isInYellowPeriod;
}

void handleYellowBlinking(unsigned long currentTime) {
  if (currentTime - lastBlinkTime >= BLINK_INTERVAL) {
    lastBlinkTime = currentTime;
    yellowState = !yellowState;
    digitalWrite(YELLOW1, yellowState);
    digitalWrite(YELLOW2, yellowState);
    
    // Turn off other lights
    digitalWrite(RED1, LOW);
    digitalWrite(GREEN1, LOW);
    digitalWrite(RED2, LOW);
    digitalWrite(GREEN2, LOW);
    updateDisplay(0, 1);  // Reset for display 1
    updateDisplay(0, 2);  // Reset for display 2
  }
}

void handleNormalOperation(unsigned long currentTime) {
  if (Serial.available() > 0) {
    String data = Serial.readStringUntil('\n');
    int commaIndex = data.indexOf(',');
    if (commaIndex != -1) {
      prevCount1 = count1;
      prevCount2 = count2;
      
      count1 = data.substring(0, commaIndex).toInt();
      count2 = data.substring(commaIndex + 1).toInt();
      if ((prevCount1 > prevCount2 && count1 == 0) || 
          (prevCount2 > prevCount1 && count2 == 0) ||
          (prevCount1 > prevCount2 && count1 < count2) ||
          (prevCount2 > prevCount1 && count2 < count1) ||
          (prevCount1 == prevCount2 && count1 != count2)) {
        
        resetSystem();
      }
      
      updateTrafficLights();
    }
  }

  updateCountdown(currentTime, 1);
  updateCountdown(currentTime, 2);
}

void enterYellowMode() {
  isYellowMode = true;
  resetLights();
  changingLight1 = false;
  changingLight2 = false;
}

void exitYellowMode() {
  isYellowMode = false;
  resetLights();
  yellowState = false;
}

void resetSystem() {
  resetLights();
  
  // Turn on yellow for both lanes
  digitalWrite(YELLOW1, HIGH);
  digitalWrite(YELLOW2, HIGH);
  
  // 3-second countdown on both displays
  for(int i = 1 ; i > 0; i--) {
    updateDisplay(0, 1);
    updateDisplay(0, 2);
    delay(1000);
  }
  changingLight1 = false;
  changingLight2 = false;
  countdown1 = 1;
  countdown2 = 1;
  currLight1 = RED1;
  currLight2 = RED2;
  updateDisplay(0, 1);
  updateDisplay(0, 2);
  delay(1000);
}

void resetLights() {
  digitalWrite(RED1, LOW);
  digitalWrite(GREEN1, LOW);
  digitalWrite(YELLOW1, LOW);
  digitalWrite(RED2, LOW);
  digitalWrite(GREEN2, LOW);
  digitalWrite(YELLOW2, LOW);
}

void updateTrafficLights() {
  if (count1 == count2) {
    if (!changingLight1) setLight(GREEN1, 27, 1);
    if (!changingLight2) setLight(RED2, 30, 2);
  }
  else if (count1 > count2) {
    if (!changingLight1) setLight(GREEN1, 36, 1);
    if (!changingLight2) setLight(RED2, 38, 2);
  } else {
    if (!changingLight1) setLight(RED1, 38, 1);
    if (!changingLight2) setLight(GREEN2, 36, 2);
  }
}

void setLight(int pin, int time, int dir) {
  if (dir == 1) {
    resetLaneLights(1);
    digitalWrite(pin, HIGH);
    currLight1 = pin;
    countdown1 = time;
    changingLight1 = true;
  } else {
    resetLaneLights(2);
    digitalWrite(pin, HIGH);
    currLight2 = pin;
    countdown2 = time;
    changingLight2 = true;
  }
}

void resetLaneLights(int lane) {
  if (lane == 1) {
    digitalWrite(RED1, LOW);
    digitalWrite(GREEN1, LOW);
    digitalWrite(YELLOW1, LOW);
  } else {
    digitalWrite(RED2, LOW);
    digitalWrite(GREEN2, LOW);
    digitalWrite(YELLOW2, LOW);
  }
}

void updateCountdown(unsigned long currentTime, int dir) {
  unsigned long *prevTime = (dir == 1) ? &prevTime1 : &prevTime2;
  int *countdown = (dir == 1) ? &countdown1 : &countdown2;
  int *currLight = (dir == 1) ? &currLight1 : &currLight2;
  bool *changingLight = (dir == 1) ? &changingLight1 : &changingLight2;

  if (*changingLight && (currentTime - *prevTime >= interval)) {
    *prevTime = currentTime;
    if (*countdown > 1) {
      (*countdown)--;
      updateDisplay(*countdown, dir);
    } else {
      *changingLight = false;
      
      if (count1 > count2) {
        if (*currLight == (dir == 1 ? GREEN1 : GREEN2)) {
          setLight(dir == 1 ? YELLOW1 : YELLOW2, 3, dir);
        } else if (*currLight == (dir == 1 ? YELLOW1 : YELLOW2)) {
          setLight(dir == 1 ? RED1 : RED2, dir == 1 ? 21 : 38, dir);
        } else if (*currLight == (dir == 1 ? RED1 : RED2)) {
          setLight(dir == 1 ? GREEN1 : GREEN2, dir == 1 ? 36 : 19, dir);
        }
      } else if (count1 < count2) {
        if (*currLight == (dir == 1 ? GREEN1 : GREEN2)) {
          setLight(dir == 1 ? YELLOW1 : YELLOW2, 3, dir);
        } else if (*currLight == (dir == 1 ? YELLOW1 : YELLOW2)) {
          setLight(dir == 1 ? RED1 : RED2, dir == 1 ? 38 : 21, dir);
        } else if (*currLight == (dir == 1 ? RED1 : RED2)) {
          setLight(dir == 1 ? GREEN1 : GREEN2, dir == 1 ? 19 : 36, dir);
        }
      } else {
        if (*currLight == (dir == 1 ? GREEN1 : GREEN2)) {
          setLight(dir == 1 ? YELLOW1 : YELLOW2, 3, dir);
        } else if (*currLight == (dir == 1 ? YELLOW1 : YELLOW2)) {
          setLight(dir == 1 ? RED1 : RED2, 30, dir);
        } else if (*currLight == (dir == 1 ? RED1 : RED2)) {
          setLight(dir == 1 ? GREEN1 : GREEN2, 27, dir);
        }
      }
    }
  }
}

void updateDisplay(int value, int dir) {
  int digit1 = value / 10;
  int digit2 = value % 10;
  uint8_t numbers[] = {digitCode[digit1], digitCode[digit2], 0, 0, 0, 0, 0, 0};
  if (dir == 1) {
    sr1.setAll(numbers);
  } else {
    sr2.setAll(numbers);
  }
}