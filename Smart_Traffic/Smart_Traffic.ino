// ESP32 Traffic Light Controller – Improved Version
// Sửa lỗi nút bấm khó nhận tín hiệu và thêm chuyển tiếp đèn vàng

#define RED13       21
#define GREEN13     18
#define YELLOW13    19
#define RED24       16
#define GREEN24     17
#define YELLOW24    5

#define DATA_PIN_13  2
#define CLOCK_PIN_13 22
#define LATCH_PIN_13 4
#define DATA_PIN_24  25
#define CLOCK_PIN_24 26
#define LATCH_PIN_24 27

// Button pins
#define BTN_PRIORITY_13  12  // Nút 1: Ưu tiên lane 13
#define BTN_PRIORITY_24  13  // Nút 2: Ưu tiên lane 24
#define BTN_RESET       14   // Nút 3: Reset/Hủy
#define BTN_YELLOW      15   // Nút 4: Chế độ đèn vàng

int lane13 = 0;
int lane24 = 0;
bool lastGreenWas13 = false;
bool aiDataValid = false;

// Button control variables
enum ControlMode { 
  NORMAL,           // Chế độ bình thường (AI)
  PRIORITY_13,      // Ưu tiên lane 13
  PRIORITY_24,      // Ưu tiên lane 24
  YELLOW_MODE,      // Chế độ đèn vàng
  TRANSITION_TO_PRIORITY_13,    // Chuyển tiếp sang ưu tiên 13
  TRANSITION_TO_PRIORITY_24,    // Chuyển tiếp sang ưu tiên 24
  TRANSITION_FROM_PRIORITY      // Chuyển tiếp từ ưu tiên về bình thường
};

ControlMode currentMode = NORMAL;
ControlMode pendingMode = NORMAL;  // Mode đang chờ chuyển đổi
unsigned long modeStartTime = 0;
const unsigned long PRIORITY_DURATION = 2 * 60 * 500; // 5 phút (sửa lại cho đúng)

// Button debounce - đơn giản và hiệu quả
unsigned long lastButtonPress[4] = {0, 0, 0, 0};
const unsigned long DEBOUNCE_DELAY = 300;  // Tăng lên 300ms để chắc chắn

// Ma trận màu
enum MatrixColor { RED_M, GREEN_M };

// Font chữ 0-9, 7 hàng
const uint8_t digitFont[10][7] = {
  {0b01110,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110},
  {0b00100,0b01100,0b00100,0b00100,0b00100,0b00100,0b01110},
  {0b01110,0b10001,0b00001,0b00110,0b01000,0b10000,0b11111},
  {0b01110,0b10001,0b00001,0b00110,0b00001,0b10001,0b01110},
  {0b00010,0b00110,0b01010,0b10010,0b11111,0b00010,0b00010},
  {0b11111,0b10000,0b11110,0b00001,0b00001,0b10001,0b01110},
  {0b01110,0b10000,0b10000,0b11110,0b10001,0b10001,0b01110},
  {0b11111,0b00001,0b00010,0b00100,0b01000,0b01000,0b01000},
  {0b01110,0b10001,0b10001,0b01110,0b10001,0b10001,0b01110},
  {0b01110,0b10001,0b10001,0b01111,0b00001,0b00010,0b01100}
};

// Prototypes
bool   readSerialData();
void   checkButtons();
String determinePhase();
void   calculateGreenTime(String phase,int &t13,int &t24);
void   runPhase(String phase,int greenTime,int redTime);
void   runPriorityMode();
void   runYellowMode();
void   runTransitionToPriority();
void   runTransitionFromPriority();
void   transitionYellowPhase(int duration);
void   yellowPhase(String phase);
void   updateNextPhase();
void   resetLights();
void   setLights(String phase);
void   setPriorityLights(ControlMode mode);
void   setYellowLights();
void   scanRow(int dataPin,int clockPin,int latchPin,
               uint8_t row,uint8_t redA,uint8_t redB,
               uint8_t greenA,uint8_t greenB);
void   displayBothMatrices(uint8_t t13,uint8_t u13,MatrixColor c13,
                           uint8_t t24,uint8_t u24,MatrixColor c24,
                           uint32_t ms);
void   displayMatrixOff13AndRed24(uint8_t t24,uint8_t u24,uint32_t ms);
void   displayRed13AndMatrixOff24(uint8_t t13,uint8_t u13,uint32_t ms);
void   turnOffMatrices();

void processSerialInput() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();

    // Nếu là lệnh điều khiển từ WebApp
    if (line.startsWith("CMD:")) {
      String cmd = line.substring(4);
      cmd.trim();

      if (cmd == "PRIORITY_13") {
        if (currentMode == NORMAL) {
          pendingMode = PRIORITY_13;
          currentMode = TRANSITION_TO_PRIORITY_13;
          modeStartTime = millis();
          Serial.println("Web command: PRIORITY_13");
        } else {
          Serial.println("⚠️ Reject PRIORITY_13 – Not in NORMAL mode");
        }
      } 
      else if (cmd == "PRIORITY_24") {
        if (currentMode == NORMAL) {
          pendingMode = PRIORITY_24;
          currentMode = TRANSITION_TO_PRIORITY_24;
          modeStartTime = millis();
          Serial.println("Web command: PRIORITY_24");
        } else {
          Serial.println("⚠️ Reject PRIORITY_24 – Not in NORMAL mode");
        }
      }

      else if (cmd == "YELLOW_MODE") {
        if (currentMode == NORMAL) {
          currentMode = YELLOW_MODE;
          modeStartTime = millis();
          Serial.println("✅ Web command: YELLOW_MODE accepted");
          resetLights();
          turnOffMatrices();
          setYellowLights();
        } else {
          Serial.println("⛔ Web command: YELLOW_MODE rejected (Not in NORMAL)");
        }
      }
 
      else if (cmd == "RESET") {
        currentMode = NORMAL;
        Serial.println("Web command: RESET to NORMAL");
        resetLights();
        turnOffMatrices();
      }
    }

    // Nếu là dữ liệu AI dạng "10,5"
    else if (line.indexOf(',') >= 0) {
      int c = line.indexOf(',');
      lane13 = line.substring(0, c).toInt();
      lane24 = line.substring(c + 1).toInt();
      aiDataValid = true;
      Serial.print("Received AI data: ");
      Serial.print(lane13);
      Serial.print(",");
      Serial.println(lane24);
    }
  }
}


void setup(){
  Serial.begin(115200);
  
  // Traffic light pins
  pinMode(RED13, OUTPUT); pinMode(GREEN13, OUTPUT); pinMode(YELLOW13, OUTPUT);
  pinMode(RED24, OUTPUT); pinMode(GREEN24, OUTPUT); pinMode(YELLOW24, OUTPUT);
  
  // Matrix pins
  pinMode(DATA_PIN_13, OUTPUT); pinMode(CLOCK_PIN_13, OUTPUT); pinMode(LATCH_PIN_13, OUTPUT);
  pinMode(DATA_PIN_24, OUTPUT); pinMode(CLOCK_PIN_24, OUTPUT); pinMode(LATCH_PIN_24, OUTPUT);
  
  // Button pins with pull-up resistors
  pinMode(BTN_PRIORITY_13, INPUT_PULLUP);
  pinMode(BTN_PRIORITY_24, INPUT_PULLUP);
  pinMode(BTN_RESET, INPUT_PULLUP);
  pinMode(BTN_YELLOW, INPUT_PULLUP);
  
  resetLights();
  turnOffMatrices();
  
  Serial.println("Traffic Light Controller Started - Improved Version");
  Serial.println("Button Controls:");
  Serial.println("BTN1: Priority Lane 13 (5min) - with 3s yellow transition");
  Serial.println("BTN2: Priority Lane 24 (5min) - with 3s yellow transition");
  Serial.println("BTN3: Reset/Cancel");
  Serial.println("BTN4: Yellow Mode");
}

void loop() {
  processSerialInput();  // ✅ Thay thế cả checkWebCommand + readSerialData
  checkButtons();

  switch(currentMode) {
    case TRANSITION_TO_PRIORITY_13:
    case TRANSITION_TO_PRIORITY_24:
      runTransitionToPriority();
      break;

    case TRANSITION_FROM_PRIORITY:
      runTransitionFromPriority();
      break;

    case PRIORITY_13:
    case PRIORITY_24:
      runPriorityMode();
      break;

    case YELLOW_MODE:
      runYellowMode();
      break;

    case NORMAL:
    default:
      if(!aiDataValid){
        lane13 = lane24 = 1;
        Serial.println("AI data invalid, using default");
      }

      resetLights();
      String phase = determinePhase();
      Serial.print("Phase: "); Serial.println(phase);

      int green13, green24;
      calculateGreenTime(phase, green13, green24);
      int greenTime = (phase=="LANE13") ? green13 : green24;
      int redTime   = greenTime + 3;

      runPhase(phase, greenTime, redTime);

      if(currentMode == NORMAL) {
        yellowPhase(phase);
        updateNextPhase();
      }
      break;
  }
}


void checkButtons() {
  unsigned long currentTime = millis();
  
  // Nút 1: Ưu tiên Lane 13
  if (digitalRead(BTN_PRIORITY_13) == LOW && 
      currentTime - lastButtonPress[0] > DEBOUNCE_DELAY) {
    lastButtonPress[0] = currentTime;
    
    if (currentMode == NORMAL) {
      Serial.println("Button 1 pressed - Starting transition to Priority Lane 13");
      pendingMode = PRIORITY_13;
      currentMode = TRANSITION_TO_PRIORITY_13;
      modeStartTime = currentTime;
    }
  }
  
  // Nút 2: Ưu tiên Lane 24
  if (digitalRead(BTN_PRIORITY_24) == LOW && 
      currentTime - lastButtonPress[1] > DEBOUNCE_DELAY) {
    lastButtonPress[1] = currentTime;
    
    if (currentMode == NORMAL) {
      Serial.println("Button 2 pressed - Starting transition to Priority Lane 24");
      pendingMode = PRIORITY_24;
      currentMode = TRANSITION_TO_PRIORITY_24;
      modeStartTime = currentTime;
    }
  }
  
  // Nút 3: Reset
  if (digitalRead(BTN_RESET) == LOW && 
      currentTime - lastButtonPress[2] > DEBOUNCE_DELAY) {
    lastButtonPress[2] = currentTime;
    
    if (currentMode != NORMAL) {
      if (currentMode == PRIORITY_13 || currentMode == PRIORITY_24) {
        Serial.println("Button 3 pressed - Starting transition from priority to normal");
        currentMode = TRANSITION_FROM_PRIORITY;
        modeStartTime = currentTime;
      } else {
        Serial.println("Button 3 pressed - Reset: Returning to normal AI mode");
        currentMode = NORMAL;
        resetLights();
        turnOffMatrices();
      }
    }
  }
  
  // Nút 4: Chế độ đèn vàng
  if (digitalRead(BTN_YELLOW) == LOW && 
      currentTime - lastButtonPress[3] > DEBOUNCE_DELAY) {
    lastButtonPress[3] = currentTime;
    
    if (currentMode == NORMAL) {
      Serial.println("Button 4 pressed - Yellow Mode activated");
      currentMode = YELLOW_MODE;
      modeStartTime = currentTime;
      resetLights();
      turnOffMatrices();
      setYellowLights();
    }
  }
}

void runTransitionToPriority() {
  unsigned long currentTime = millis();
  
  // Chạy đèn vàng trong 3 giây
  transitionYellowPhase(3000);
  
  // Chuyển sang chế độ ưu tiên
  currentMode = pendingMode;
  modeStartTime = currentTime;
  
  String modeStr = (pendingMode == PRIORITY_13) ? "Lane 13" : "Lane 24";
  Serial.println("Transition complete - Priority " + modeStr + " activated for 5 minutes");
  
  resetLights();
  turnOffMatrices();
  setPriorityLights(pendingMode);
}

void runTransitionFromPriority() {
  // Chạy đèn vàng trong 3 giây
  transitionYellowPhase(3000);
  
  // Chuyển về chế độ bình thường
  currentMode = NORMAL;
  Serial.println("Transition complete - Returning to normal AI mode");
  resetLights();
  turnOffMatrices();
}

void transitionYellowPhase(int duration) {
  unsigned long startTime = millis();
  resetLights();
  setYellowLights();
  turnOffMatrices();  // Tắt ma trận LED trong suốt quá trình chuyển tiếp
  
  while (millis() - startTime < duration) {
    delay(100);  // Chỉ delay đơn giản, không hiển thị countdown
    
    // Vẫn cho phép kiểm tra nút reset trong quá trình chuyển tiếp
    checkButtons();
    if (currentMode == NORMAL) {
      resetLights();
      return;
    }
  }
  
  resetLights();
}

void runPriorityMode() {
  unsigned long currentTime = millis();
  
  // Kiểm tra timeout 5 phút
  if (currentTime - modeStartTime >= PRIORITY_DURATION) {
    Serial.println("Priority mode timeout - starting transition to normal");
    currentMode = TRANSITION_FROM_PRIORITY;
    modeStartTime = currentTime;
    return;
  }
  
  // Duy trì đèn ưu tiên
  setPriorityLights(currentMode);
  delay(50);
}

void runYellowMode() {
  // Kiểm tra nút trước khi bật đèn
  checkButtons();
  if(currentMode != YELLOW_MODE) return;
  
  // Chế độ đèn vàng - chỉ có thể thoát bằng nút reset
  setYellowLights();
  delay(250);
  
  checkButtons();
  if(currentMode != YELLOW_MODE) return;
  
  resetLights();
  delay(250);
}

void setPriorityLights(ControlMode mode) {
  // Reset tất cả đèn trước
  resetLights();
  
  // Bật đúng đèn theo mode
  if (mode == PRIORITY_13) {
    digitalWrite(GREEN13, HIGH);
    digitalWrite(RED24, HIGH);
  } else if (mode == PRIORITY_24) {
    digitalWrite(RED13, HIGH);
    digitalWrite(GREEN24, HIGH);
  }
}

void setYellowLights() {
  // Reset tất cả đèn trước khi bật đèn vàng
  resetLights();
  digitalWrite(YELLOW13, HIGH);
  digitalWrite(YELLOW24, HIGH);
}

void turnOffMatrices() {
  for(int i = 0; i < 10; i++) {
    for(uint8_t r = 0; r < 8; r++) {
      scanRow(DATA_PIN_13, CLOCK_PIN_13, LATCH_PIN_13, r, 0xFF, 0xFF, 0xFF, 0xFF);
      scanRow(DATA_PIN_24, CLOCK_PIN_24, LATCH_PIN_24, r, 0xFF, 0xFF, 0xFF, 0xFF);
    }
  }
}

bool readSerialData(){
  if(Serial.available()){
    String d = Serial.readStringUntil('\n');
    int c = d.indexOf(',');
    if(c>=0){
      lane13 = d.substring(0,c).toInt();
      lane24 = d.substring(c+1).toInt();
      return(true);
    }
  }
  return(false);
}

String determinePhase(){
  return lastGreenWas13?"LANE24":"LANE13";
}

void calculateGreenTime(String phase,int &t13,int &t24){
  const int def=35;
  if(!aiDataValid){ t13=t24=def; }
  else if(lane13==lane24){ t13=t24=def; Serial.println("Equal density:35s each"); }
  else if(lane13>lane24){ t13=45; t24=25; Serial.println("L13 priority"); }
  else{ t13=25; t24=45; Serial.println("L24 priority"); }
  Serial.print("GreenTimes L13:");Serial.print(t13);
  Serial.print(" L24:");Serial.println(t24);
}

void runPhase(String phase, int greenTime, int /*rt*/) {
  resetLights();
  setLights(phase);

  int startCnt = (phase == "LANE13") ? lane13 : lane24;
  int startOppCnt = (phase == "LANE13") ? lane24 : lane13;

  unsigned long phaseStart = millis();
  unsigned long now = millis();

  while (millis() - phaseStart < greenTime * 500UL) {
    now = millis();

    // ✅ Luôn đọc lệnh để không bỏ lỡ Serial command
    processSerialInput();

    // ✅ Nếu lệnh đến, thì thoát ngay
    if (currentMode != NORMAL) {
      Serial.println("Phase interrupted by command (e.g. from Web)");
      resetLights();
      turnOffMatrices();
      return;
    }

    checkButtons();

    int elapsed = (now - phaseStart) / 500 + 1;

    if (elapsed >= 20) {
      int curr = (phase == "LANE13") ? lane13 : lane24;
      int opp  = (phase == "LANE13") ? lane24 : lane13;

      if (startCnt > 0 && curr == 0 && opp > 0) {
        Serial.println("Early interrupt " + phase + " - current lane empty, opposite has traffic");
        break;
      }

      if (startCnt == 0 && startOppCnt == 0 && curr == 0 && opp > 0) {
        Serial.println("Early interrupt " + phase + " - both lanes initially empty, opposite now has traffic");
        break;
      }
    }

    int t = greenTime - (now - phaseStart) / 500;
    int rr = t + 3;

    if (phase == "LANE13")
      displayBothMatrices(t / 10, t % 10, GREEN_M, rr / 10, rr % 10, RED_M, 50);
    else
      displayBothMatrices(rr / 10, rr % 10, RED_M, t / 10, t % 10, GREEN_M, 50);
  }
}


void yellowPhase(String phase){
  if(phase=="LANE13"){ 
    digitalWrite(GREEN13,LOW); 
    digitalWrite(YELLOW13,HIGH);
    for(int t=3;t>0;t--) {
      checkButtons();
      if(currentMode != NORMAL) {
        resetLights();
        turnOffMatrices();
        return;
      }
      displayMatrixOff13AndRed24(t/10,t%10,500);
    }
  } else{ 
    digitalWrite(GREEN24,LOW); 
    digitalWrite(YELLOW24,HIGH);
    for(int t=3;t>0;t--) {
      checkButtons();
      if(currentMode != NORMAL) {
        resetLights();
        turnOffMatrices();
        return;
      }
      displayRed13AndMatrixOff24(t/10,t%10,500);
    }
  }
  digitalWrite(YELLOW13,LOW); 
  digitalWrite(YELLOW24,LOW);
}

void updateNextPhase(){ 
  lastGreenWas13=!lastGreenWas13; 
}

void resetLights(){
  digitalWrite(RED13,LOW); digitalWrite(GREEN13,LOW); digitalWrite(YELLOW13,LOW);
  digitalWrite(RED24,LOW); digitalWrite(GREEN24,LOW); digitalWrite(YELLOW24,LOW);
}

void setLights(String phase){
  if(phase=="LANE13"){ 
    digitalWrite(GREEN13,HIGH); 
    digitalWrite(RED24,HIGH);
  } else{                
    digitalWrite(RED13,HIGH);   
    digitalWrite(GREEN24,HIGH);
  }  
}

void scanRow(int dp,int cp,int lp,uint8_t r,uint8_t ra,uint8_t rb,uint8_t ga,uint8_t gb){
  digitalWrite(lp,LOW);
  shiftOut(dp,cp,LSBFIRST,rb);
  shiftOut(dp,cp,LSBFIRST,ra);
  shiftOut(dp,cp,LSBFIRST,gb);
  shiftOut(dp,cp,LSBFIRST,ga);
  shiftOut(dp,cp,LSBFIRST,1<<r);
  digitalWrite(lp,HIGH);
}

void displayBothMatrices(uint8_t t13,uint8_t u13,MatrixColor c13,uint8_t t24,uint8_t u24,MatrixColor c24,uint32_t ms){
  uint32_t st=millis();
  while(millis()-st<ms) {
    for(uint8_t r=0;r<8;r++){
      uint8_t pA=(r<7?digitFont[t13][r]<<3:0), pB=(r<7?digitFont[u13][r]<<3:0);
      scanRow(DATA_PIN_13,CLOCK_PIN_13,LATCH_PIN_13,r,(c13==RED_M?~pA:0xFF),(c13==RED_M?~pB:0xFF),(c13==GREEN_M?~pA:0xFF),(c13==GREEN_M?~pB:0xFF));
      uint8_t qA=(r<7?digitFont[t24][r]<<3:0), qB=(r<7?digitFont[u24][r]<<3:0);
      scanRow(DATA_PIN_24,CLOCK_PIN_24,LATCH_PIN_24,r,(c24==RED_M?~qA:0xFF),(c24==RED_M?~qB:0xFF),(c24==GREEN_M?~qA:0xFF),(c24==GREEN_M?~qB:0xFF));
      delayMicroseconds(500);
    }
  }
}

void displayMatrixOff13AndRed24(uint8_t t24,uint8_t u24,uint32_t ms){
  uint32_t st=millis();
  while(millis()-st<ms) {
    for(uint8_t r=0;r<8;r++){
      scanRow(DATA_PIN_13,CLOCK_PIN_13,LATCH_PIN_13,r,0xFF,0xFF,0xFF,0xFF);
      uint8_t p=(r<7?digitFont[t24][r]<<3:0), q=(r<7?digitFont[u24][r]<<3:0);
      scanRow(DATA_PIN_24,CLOCK_PIN_24,LATCH_PIN_24,r,~p,~q,0xFF,0xFF);
      delayMicroseconds(500);
    }
  }
}

void displayRed13AndMatrixOff24(uint8_t t13,uint8_t u13,uint32_t ms){
  uint32_t st=millis();
  while(millis()-st<ms) {
    for(uint8_t r=0;r<8;r++){
      uint8_t p=(r<7?digitFont[t13][r]<<3:0), q=(r<7?digitFont[u13][r]<<3:0);
      scanRow(DATA_PIN_13,CLOCK_PIN_13,LATCH_PIN_13,r,~p,~q,0xFF,0xFF);
      scanRow(DATA_PIN_24,CLOCK_PIN_24,LATCH_PIN_24,r,0xFF,0xFF,0xFF,0xFF);
      delayMicroseconds(500);
    }
  }
}