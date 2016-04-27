#include <stdint.h>
#include "TouchScreen.h" //touch screen library, with NUM_SAMPLES 3 (!!)
#include <Adafruit_GFX.h>    // Core graphics library
#include "SWTFT.h" // Hardware-specific library

// These are the pins for the shield!
// on some shields they are swapped x<->y
#define YP A2  // must be an analog pin, use "An" notation!
#define XM A1  // must be an analog pin, use "An" notation!
#define YM 6   // can be a digital pin
#define XP 7   // can be a digital pin

#define MINPRESSURE 10
#define MAXPRESSURE 1000

//detection hysteresis
#define PRESSURE_THRESHOLD_LOW 160
#define PRESSURE_THRESHOLD_HIGH 320 

// Assign human-readable names to some common 16-bit color values:
#define BLACK   0x0000
#define BLUE    0x001F
#define RED     0xF800
#define GREEN   0x07E0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define WHITE   0xFFFF

//calibration constants for used touchscreen
#define TS_MINX 190
#define TS_MINY 90
#define TS_MAXX 855
#define TS_MAXY 900


#define FSMSTATE_RESET 1
#define FSMSTATE_ACTIVE 2
#define FSMSTATE_DETECT 3
#define FSMSTATE_ALERT 4
#define FSMSTATE_INACTIVE 5
#define FSMSTATE_WAIT 6

#define ALERT_PIN 13 //PB5 //led, buzzer

#define PIR_INPUT 11 //PB3 //PIR sensor 

#define BUTTON_INPUT 12 //

#define GERKON_INPUT A5

#define LOW_ADC_RANGE 32 

#define HIGH_ADC_RANGE 992

#define UNLOCK_CODE 123456

//3 minutes to leave protected area
//#define ACTIVATE_TIMEOUT 180000

#define ACTIVATE_TIMEOUT 30000

#define START_TIMEOUT 10000

//1 minute to enter deactivation code
#define DEACTIVATE_TIMEOUT 30000

//30 seconds to hide UI and show alert?
#define HIDE_BUTTONS_TIMEOUT 30000

#define EVENT_IMITATION_TIMEOUT 5000

//1 second to dump state
#define TIMER_INTERVAL 1000

// For better pressure precision, we need to know the resistance
// between X+ and X- Use any multimeter to read it
// For the one we're using, its 300 ohms across the X plate
TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);

//screen object
SWTFT tft;

#define CODE_BUTTON_COUNT 12

#define ACTIVATE_BUTTON_COUNT 2

//numeric buttons
Adafruit_GFX_Button gCodeButtons[CODE_BUTTON_COUNT];


Adafruit_GFX_Button gActivateButtons[ACTIVATE_BUTTON_COUNT];

static const unsigned char buttonLabels[]  = "123456789<0>";

static const char Activate[]  = "Activate";
static const char Cancel[]  = "Cancel";

//debug cycle counter
unsigned long gCounter;

//touch screen pressure filter, exponential moving average, hysteresis threshold 
bool gLastPressed;
int gLastPressure;

//FSM state
int gFSMState;

//sensors detected something
bool gHasDetect;

//code input
bool gButtonsVisible;

unsigned long gCode;

//
int gSensors;

//time
long gCurrentMillis;

long gTimeoutStart;

long gLastPressTime;

long gTimerLast;

void setup(void) {
  Serial.begin(9600);

  Serial.println(F("TFT LCD test"));

  tft.reset();

  uint16_t identifier = tft.readID();


  Serial.print(F("LCD driver chip: "));
  Serial.println(identifier, HEX);


  tft.begin(identifier);
  
  //tft.fillScreen(BLACK);
    
  gLastPressed = false;
  gLastPressure = 0;
  gCounter = 0;
  gLastPressTime = 0;
  gButtonsVisible = false;  
  gCode=0;
  
  gFSMState = FSMSTATE_ACTIVE;
  gHasDetect = false;


  gSensors = 0;
 
  for(int i=0;i<CODE_BUTTON_COUNT;i++){
    char label[9];
    label[0] = buttonLabels[i];
    label[1] = 0;
    gCodeButtons[i].initButton(&tft, 45+(i % 3)*75, 45+(i / 3)*75, 60, 60, WHITE, RED, BLACK, label, 3);
    //buttons[i].drawButton();
  }

  gActivateButtons[0].initButton(&tft, 120, 45, 210, 60, WHITE, RED, BLACK, Activate, 3);
  gActivateButtons[1].initButton(&tft, 120, 135, 210, 60, WHITE, RED, BLACK, Cancel, 3);

  digitalWrite(ALERT_PIN, LOW);
  pinMode(ALERT_PIN, OUTPUT);  

  pinMode(PIR_INPUT, INPUT);
  pinMode(BUTTON_INPUT, INPUT);
  digitalWrite(BUTTON_INPUT, HIGH);
  readSensors(true);
  readSensors(true);

  gCurrentMillis = millis();
  gTimeoutStart = gCurrentMillis;
  gTimerLast = gCurrentMillis;

  displayState(true, 0);  


}


void dumpPointSerial(TSPoint *p){
  Serial.print("T = "); Serial.print(gCurrentMillis);
  Serial.print("\tN = "); Serial.print(gCounter);
  Serial.print("\tX = "); Serial.print(p->x);
  Serial.print("\tY = "); Serial.print(p->y);
  Serial.print("\tPressure = "); Serial.println(p->z);
}

void dumpPointScreen(TSPoint *p, bool clear){
    tft.setTextColor(GREEN);
    tft.setTextSize(1);
    if(clear){
      tft.setCursor(0, 0);
      tft.fillRect(0, 0, tft.width(), 10, BLACK);
    }
    tft.print(p->x);
    tft.print(" ");
    tft.print(p->y);
    tft.print(" ");
    tft.print(p->z);
    tft.print(" ");
}

void dumpPoint(TSPoint *p, bool clear) {
  dumpPointSerial(p);
  //dumpPointScreen(p, clear);
}


int readSensors(bool dumpAnalog){
  /*if(gCurrentMillis>EVENT_IMITATION_TIMEOUT){
    return 1;//debug event
  }*/
  int result = 0;
  if(digitalRead(PIR_INPUT)==HIGH){
    result |=1;
  }  
  if(digitalRead(BUTTON_INPUT)==HIGH){
    result |=2;
  }  
  int sensorValue = analogRead(GERKON_INPUT);
  if(dumpAnalog){
    Serial.println(sensorValue);
  }
  if(sensorValue<LOW_ADC_RANGE){
    result |=4;
  }
  if(sensorValue>HIGH_ADC_RANGE){
    result |=8;
  }
  
  return result;
}

void clearScreen(){
  tft.fillScreen(BLACK);
}

void dumpStateSerial(){
  Serial.print("gFSMState ");
  Serial.println(gFSMState);
  Serial.print("gCounter ");
  Serial.println(gCounter);
  Serial.print("gCurrentMillis ");
  Serial.println(gCurrentMillis);
  Serial.print("gTimeoutStart ");
  Serial.println(gTimeoutStart);
  Serial.print("gLastPressTime ");
  Serial.println(gLastPressTime);
  Serial.println();
}

void printTime(long time){
  int seconds = time/1000;
  int minutes = seconds / 60;  
  seconds = seconds % 60;
  
  if(minutes<10){  
    tft.print("0");
  }
  tft.print(minutes);
  tft.print(":");
  if(seconds<10){  
    tft.print("0");
  }  
  tft.print(seconds);
}

void displayState(bool needClear, int sensors){  
  if(gButtonsVisible){
    return;  
  }
  tft.setTextSize(3);    
  tft.setCursor(0, 0); 
  if(needClear){
    gButtonsVisible = false;
    clearScreen();
  }else{
    tft.fillRect(0, 0, tft.width(), 60, BLACK);  
  }  
  switch(gFSMState){
    case FSMSTATE_RESET:
      tft.setTextColor(RED);
      tft.println("RESET");
      printTime(START_TIMEOUT-(gCurrentMillis-gTimeoutStart));
      tft.println();     
      break;
    case FSMSTATE_ACTIVE:
      tft.setTextColor(RED);     
      tft.println("ACTIVE");
      break;
    case FSMSTATE_DETECT:
      tft.setTextColor(RED);      
      tft.print("DETECT ");
      tft.println(sensors);
      //tft.print(" ");
      printTime(DEACTIVATE_TIMEOUT-(gCurrentMillis-gTimeoutStart));
      tft.println();     
      break;
    case FSMSTATE_ALERT: 
      tft.print("ALERT ");
      tft.println(sensors);
      tft.println();     
      break;      
    case FSMSTATE_WAIT:
      tft.setTextColor(RED);      
      tft.println("WAIT");      
      printTime(ACTIVATE_TIMEOUT-(gCurrentMillis-gTimeoutStart));
      tft.println();     
      break;
    case FSMSTATE_INACTIVE:
      tft.setTextColor(GREEN);      
      tft.print("INACTIVE");      
      tft.println();     
      break;
  }
}

/*
void displaySensors(int sensors, bool needClear){
  Serial.print("displaySensors ");
  Serial.println(sensors);
  if(needClear){
    clearScreen();  
  }
  tft.setTextSize(3);    
  tft.setCursor(0, 0);
  tft.fillRect(0, 0, tft.width(), 30, BLACK);  
  tft.println(sensors);
}

void displayTimeout(long time, bool needClear){
  Serial.print("displayTimeout ");
  Serial.println(time);
  if(needClear){
    clearScreen();  
  }
  tft.setTextSize(3);    
  tft.setCursor(0, 0);
  tft.setTextColor(RED);    
  tft.fillRect(0, 0, tft.width(), 30, BLACK);  
  printTime(time);
  tft.println();  
}*/


void activateAlert(){
  digitalWrite(ALERT_PIN, HIGH);
  Serial.println("ALERT"); 
}

void deactivateAlert(){
  digitalWrite(ALERT_PIN, LOW);
}

/*void drawActivateButtons(){
  for(int i=0;i<ACTIVATE_BUTTON_COUNT;i++){
    gActivateButtons[i].drawButton();
    gActivateButtons[i].press(false);
  }
}*/

void deactivateAlarm(){
  Serial.println("deactivateAlarm");  
  deactivateAlert();
  gFSMState = FSMSTATE_INACTIVE;
}


void drawCodeButtons(){
  for(int i=0;i<CODE_BUTTON_COUNT;i++){
    gCodeButtons[i].drawButton();
  }
  gButtonsVisible=true;
}


void resetCode(){
  Serial.println("resetCode");    
  gCode = 0;
}

bool processCodeButton(int i){
  if(i>=0 && i<=8){  
    gCode = gCode*10+i+1;
  }else
  if(i==9){  
    gCode = gCode/10;
  }else
  if(i==10){ 
    gCode = gCode*10;
  }else
  if(i==10){
    //TODO: process #
  }    
  if(gCode==UNLOCK_CODE){
    gButtonsVisible = false;
    deactivateAlarm();
    return true;
  }
  return false;
}

void displayCode(){
    Serial.print("displayCode ");  
    Serial.println(gCode);
    tft.setTextColor(GREEN);
    tft.setTextSize(1);
    tft.setCursor(0, 0);
    tft.fillRect(0, 0, tft.width(), 10, BLACK);    
    tft.print(gCode);
    tft.println();
    /*tft.print(" ");
    tft.print(gCurrentMillis-gTimeoutStart);    
    tft.print(" ");
    tft.print(gCurrentMillis);    
    tft.print(" ");
    tft.print(gFSMState);*/
}


bool processActivateButton(int i){
  Serial.print("processActivateButton ");  
  Serial.println(i);
  if(i==0){    
    gTimeoutStart = gCurrentMillis;
    gFSMState = FSMSTATE_WAIT;
    return true;
  }

  if(i==1){    
    deactivateAlarm();        
    return true;
  }
  
}

void drawUnpressedButtons(Adafruit_GFX_Button buttons[], int length){
  for(int i=0;i<length;i++){
    Adafruit_GFX_Button *b = &buttons[i];
    b->press(false);        
    b->drawButton(false);                
  }
}

bool processTouchPress(TSPoint *p){
  // we have some minimum pressure we consider 'valid'
  // pressure of 0 means no pressing!  

  if(gFSMState==FSMSTATE_RESET){
    return false;  
  }
  bool result = false;
  
  if (p->z > MINPRESSURE && p->z < MAXPRESSURE) {  //
    
    dumpPoint(p, true);
    //tft.println("");

    //map to screen coords
    p->x = tft.width()-(map(p->x, TS_MINX, TS_MAXX, tft.width(), 0));
    p->y = tft.height()-(map(p->y, TS_MINY, TS_MAXY, tft.height(), 0));

    //hysteresis
    bool pressed;
    if(gLastPressed) {
      pressed = p->z > PRESSURE_THRESHOLD_LOW;
    }else{
      pressed = p->z > PRESSURE_THRESHOLD_HIGH;
    }
    /*
    //led/buzzer debug?
    if(pressed){
      digitalWrite(13, LOW);
    }else{
      digitalWrite(13, HIGH);   
    }*/
    if(gLastPressed^pressed){
      
      //digitalWrite(13, HIGH); 
      //delay(10);//todo:replace with FSM
      //digitalWrite(13, LOW);
      
      Serial.print("pressed=");Serial.print(pressed);
      Serial.print(" lastPressed=");Serial.print(gLastPressed);
      Serial.print(" pressure=");Serial.print(p->z);      
      Serial.println();

      
      if(gFSMState==FSMSTATE_ACTIVE || gFSMState==FSMSTATE_DETECT || gFSMState==FSMSTATE_ALERT){
        if(!gButtonsVisible){
          if(!pressed) {
            clearScreen();
            drawUnpressedButtons(gCodeButtons, CODE_BUTTON_COUNT);          
            resetCode();           
            gButtonsVisible = true;
          }          
        }else{
        
          //entering code
          for(int i=0;i<CODE_BUTTON_COUNT;i++){
            Adafruit_GFX_Button *b = &gCodeButtons[i];
            bool isPressed = b->contains(p->x, p->y) && pressed;
            bool isReleased = b->contains(p->x, p->y) && !pressed;
            if(isPressed){
              b->press(true);              
            }else{
              b->press(false);        
            }
            if(b->justPressed() || b->justReleased()){
              //Serial.print("draw code button ");
              //Serial.print(i);
              //Serial.print(" ");
              //Serial.println(b->isPressed());
              b->drawButton(b->isPressed());      
            }       
            if(b->justReleased()){
              result = processCodeButton(i);            
              if(result){
                break;             
              }
            }
          }
        }
        displayCode();
      }else{//activation
        if(!gButtonsVisible){
          if(!pressed){
            clearScreen();
            drawUnpressedButtons(gActivateButtons, ACTIVATE_BUTTON_COUNT);
            gButtonsVisible = true;
          }
        }else{
          for(int i=0;i<ACTIVATE_BUTTON_COUNT;i++){
            Adafruit_GFX_Button *b = &gActivateButtons[i];
            bool isPressed = b->contains(p->x, p->y) && pressed;
            bool isReleased = b->contains(p->x, p->y) && !pressed;
            if(isPressed){
              b->press(true);              
            }else{
              b->press(false);        
            }
            if(b->justPressed() || b->justReleased()){
              //Serial.print("draw activate button ");
              //Serial.print(i);
              //Serial.print(" ");
              //Serial.println(b->isPressed());
              b->drawButton(b->isPressed());      
            }       
            if(b->justReleased()){
              result = processActivateButton(i);
              if(result){
                break;              
              }
            } 
          }        
        }
      }
      gLastPressTime = gCurrentMillis;
    }
    gLastPressed = pressed;
  }
  if(result){
    gButtonsVisible = false;
    clearScreen();  
  }
  return result;
}




void loop(void) {
  // a point object holds x y and z coordinates  
  gCurrentMillis = millis();

  TSPoint p = ts.getPoint();  

  //exponential moving average filter
  gLastPressure = (gLastPressure+p.z)/2;
  p.z = gLastPressure;
  
  // if sharing pins, you'll need to fix the directions of the touchscreen pins
  //pinMode(XP, OUTPUT);
  pinMode(XM, OUTPUT);
  pinMode(YP, OUTPUT);
  //pinMode(YM, OUTPUT);
  
  //dumpPointScreen(&p, true);

  bool wasChange = processTouchPress(&p);

  bool timer = false;
  if(gCurrentMillis-gTimerLast>TIMER_INTERVAL){    
    timer = true;
    gTimerLast = gCurrentMillis;
  }

  bool showState = wasChange;
  bool buttonsTimeout = gCurrentMillis-gLastPressTime>HIDE_BUTTONS_TIMEOUT;
  bool buttonsWereVisible = gButtonsVisible;
  if(buttonsTimeout){    
    if(gButtonsVisible){
      Serial.println("clear buttons");
      clearScreen();
    }
    showState |= gButtonsVisible;
    gButtonsVisible = false;    
  }  
  
  long passed;
  int sensors = 0;
  
  switch(gFSMState){
    case FSMSTATE_RESET: 
      passed = gCurrentMillis-gTimeoutStart;
      if(passed>START_TIMEOUT){
        Serial.println("Starting");  
        gFSMState = FSMSTATE_ACTIVE;
        showState = true;
      }else{
        showState = timer;
      }
      break;
    case FSMSTATE_ACTIVE: //protection on
      sensors = readSensors(timer);
      if(sensors!=0){
        gSensors = sensors;
        gTimeoutStart = gCurrentMillis;
        gFSMState = FSMSTATE_DETECT;        
        showState = true;
      }
      break;
    case FSMSTATE_DETECT:     
      sensors = readSensors(timer); 
      passed = gCurrentMillis-gTimeoutStart;
      if(passed>DEACTIVATE_TIMEOUT){
        activateAlert();
        gFSMState = FSMSTATE_ALERT;
        showState = true;
      }else{        
        showState |= timer;
      }
      break;
    case FSMSTATE_ALERT:       
      sensors = readSensors(timer);   
      showState |= timer;
      break;
    case FSMSTATE_INACTIVE:            
      break;
    case FSMSTATE_WAIT:
      passed = gCurrentMillis-gTimeoutStart;
      if(passed>ACTIVATE_TIMEOUT){
        Serial.println("activateAlarm");  
        gFSMState = FSMSTATE_ACTIVE;
        showState = true;
      }else{
        showState |= timer;
      }
      break;     
    default:
      gFSMState = FSMSTATE_ACTIVE;
      showState = true;
      break;     
  }
  if(showState){
    dumpStateSerial();
    displayState(buttonsWereVisible, sensors);  
  }
  
  gCounter++; 
}
