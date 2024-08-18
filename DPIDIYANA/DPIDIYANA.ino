#include <EEPROM.h>
#include "SSD1306Ascii.h"
#include "SSD1306AsciiWire.h"
#include <Bounce2.h>
#include <Servo.h>

/*
 * I/O
 * 
 * D12 Setting Button
 * D16 Trigger
 * D9/D10 ESC Signal
 * D8  Selectfire SW
 * D5  Solenoid
 * A7  Battery Sensing 47k/10k
 * 
 */

// compiler definitions
#define SETTING_PIN         12
#define TRIGGER_PIN         16
#define ESC_PIN             10
#define SELECTOR_PIN        8
#define NOID_PIN            5
#define VOLT_PIN            A7
#define NOID_ON_TIME        40   //time in MS
#define NOID_OFF_TIME       45   //time in MS
#define SPINUP_DELAY        100  //time in MS to wait from idle to firing aka min delay
#define BATTERY_OFFSET      0.0  //fixing resistor inaccuracy
#define OLED_ADDR           0x3C
#define DWELL               200  //time to keep revving
#define RAMP_DOWN           200  //time to slowly stop revving
#define IDLE_THROTTLE       20
//select fire defitions 
#define MODE_SINGLE 0 
#define MODE_BURST  1
#define MODE_AUTO   2

//menu definitions
#define FIRING 0



//runtime variables
byte menuSelection = 0;
bool isFiring = false;
bool isIdle = false;
byte shotToFire = 0;
long solenoidTimer;
bool isNoidExtended = false;
long flywheelTimer;
bool isRevving = false;
byte minSpinupDelay = 50;
byte maxSpinupDelay = 100;
byte revDownTime = 100;
byte rofDelay = 0;
byte retractionTime = NOID_OFF_TIME + rofDelay;
byte flywheelThrottle = 100;
byte currFlywheelThrottle = 0;
byte minFlywheelThrottle = 0;
byte firingMode = MODE_SINGLE;
byte burstLimit = 3;
float battVoltage;
long displayTimer;
bool enterMenu = false;
bool isBurst = false;



// Declare and Instantiate Bounce objects
Bounce btnTrigger        = Bounce();
Bounce switchSelector    = Bounce();
Bounce settingPin        = Bounce();

SSD1306AsciiWire oled;
Servo ESC;

void solenoidHandle(){
  if (isFiring) {
    //noid has been turned on
    if (isNoidExtended) {
      if ((millis() - solenoidTimer) >= NOID_ON_TIME) {
        digitalWrite(NOID_PIN, LOW); // Retract Solenoid
        shotToFire--;
        isNoidExtended = false;
        solenoidTimer = millis();
        if (shotToFire == 0) { //done firing
          isFiring = false;
          flywheelTimer = millis(); // setup flywheel Timer once for rev down
        } 
      }
    } 
    if (!isNoidExtended) { //noid is turned off
      if ((millis() - solenoidTimer) >= retractionTime) {
        digitalWrite(NOID_PIN, HIGH); // Extend Solenoid
        isNoidExtended = true;
        solenoidTimer = millis();
      }
    }
  }
}

//handles flywheel lag and such
//runs every loop
void flywheelHandle(){
  if(shotToFire != 0){
    //case: flywheel spinning down or idle
    if (isIdle || isRevving){
      isRevving = true;
      if (millis() - flywheelTimer >= minSpinupDelay){ //min delay has occurred
        isFiring = true;
      }
    } else  {
      //case: flywheel not spinning yet -> max delay
      if (millis() - flywheelTimer >= maxSpinupDelay){ //min delay has occurred
        isFiring = true;
      }
    }
  } else { //no shots queued
    //case: flywheel not spinning
    if(millis() - flywheelTimer >= revDownTime){
      isRevving = false;
      currFlywheelThrottle = minFlywheelThrottle;
    } else { 
    //case: start flywheel rev down
      currFlywheelThrottle = map(millis() - flywheelTimer, 0, revDownTime,minFlywheelThrottle,flywheelThrottle);
    }
  }
}


// only executes when the trigger gets pulled
void triggerPressedHandle() {
  
  //save time of FIRST trigger pull
  if (currFlywheelThrottle != flywheelThrottle){
    flywheelTimer = millis();
    //set flywheel variable to firing rpm
    currFlywheelThrottle = flywheelThrottle;
  }


  ESC.write(currFlywheelThrottle); // start flywheel
  switch (firingMode) {
    case MODE_SINGLE: shotToFire++; break;
    case MODE_BURST : shotToFire += burstLimit; break;
    case MODE_AUTO  : shotToFire += 100; break; //generic value
  }
}



// Function: triggerReleasedHandle

void triggerReleasedHandle() {
  //if in auto or burst and trigger released, finish off the shot queue
  if (((firingMode == MODE_AUTO) || (firingMode == MODE_BURST)) && isFiring && (shotToFire > 1)) {
    shotToFire = 1;    // fire off last shot
  }
}


// Function: readVoltage
void readVoltage() {
  // you might have to adjust the formula according to the voltage sensor you use
  battVoltage = ((analogRead(VOLT_PIN) * 0.259)-BATTERY_OFFSET); //converts digital to a voltage

}

void selectFireHandle(){

}

void updateDisplay() {

  readVoltage();
  oled.clear();
  oled.set2X();
  oled.setCursor(0, 0);
  oled.println(battVoltage / 10, 1);
  oled.set1X();
  switch (firingMode) {
    case (MODE_SINGLE):
      oled.println("SEMI");
      break;
    case (MODE_BURST):
      oled.println("BURST");
      break;
    case (MODE_AUTO):
      oled.println("AUTO");
      break;
  }
  oled.print("ROF: ");
  oled.print("Burst Limit: ");
  oled.print("RPM: ");
}

void setup() {
  // put your setup code here, to run once:
  oled.begin(&Adafruit128x64, OLED_ADDR);
  oled.setFont(Adafruit5x7);
  Serial.begin(9600);
  ESC.attach(ESC_PIN,1000,2000);
                      
  pinMode(TRIGGER_PIN, INPUT_PULLUP);    
  btnTrigger.attach(TRIGGER_PIN);
  btnTrigger.interval(5);

  pinMode(SELECTOR_PIN, INPUT_PULLUP);    
  switchSelector.attach(SELECTOR_PIN);
  switchSelector.interval(5);

  pinMode(SETTING_PIN, INPUT_PULLUP);   
  settingPin.attach(SETTING_PIN);
  settingPin.interval(5);

  pinMode(VOLT_PIN, INPUT); 
  pinMode(NOID_PIN, OUTPUT);

   if (digitalRead(SELECTOR_PIN) == LOW) {
    firingMode = MODE_AUTO;
  } else {
    firingMode = MODE_SINGLE;
  }
  oled.clear();
  updateDisplay();
  displayTimer = millis(); 
  ESC.write(180);
  delay(3000);
  ESC.write(0);
  delay(3000);
}

void loop() {
  

  btnTrigger.update();
  switchSelector.update();
  settingPin.update();


  // Listen to Trigger Pull/Release

  if (btnTrigger.fell()) {               // pull
    triggerPressedHandle();
  } else if (btnTrigger.rose()) {        // released
    triggerReleasedHandle();
  }


  // Listen to Firing

  solenoidHandle();
  flywheelHandle();

  // Listen to Firing Mode change: Single Shot, Burst, Full Auto

  if (switchSelector.changed()) {
    if (switchSelector.read()) { //if selector is in full/burst position
      if (isBurst) {
        firingMode = MODE_BURST;
      } else {
        firingMode = MODE_AUTO;
      }
    } else { // single
      firingMode = MODE_SINGLE;
      shotToFire = 0;
    }
    updateDisplay();
    displayTimer = millis();
  }


  if (settingPin.fell()) {
    displayTimer = millis(); 
    enterMenu = true;
  }

  if (settingPin.rose()) {
    enterMenu = false;
  }

  if (enterMenu && ((millis() - displayTimer) > 2000)) { //only enters setup if switchbutton held for 2s
    while (enterMenu) {
    }
    
  }

  if ((millis() - displayTimer > 10000) && !isFiring && !isRevving) { //only waste time updating every 5000 seconds
    updateDisplay();
    displayTimer = millis();
  }


}
