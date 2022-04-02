#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);   //init oled
//button inputs are normally open active LOW
//arduino pins
const int encoderBtnPin = 2;
const int encoderClkPin = 3;
const int pizeoPin = 4;
const int m1StepPin = 5;
const int m2StepPin = 6;
const int m1DirPin = 7;
const int m2DirPin = 8;
const int encoderDtPin = 9;
const int startBtnPin = 10;
const int stopBtnPin = 11;
const int limitSwitch1Pin = 12;
const int limitSwitch2Pin = 13;
//menu screens
const int lengthSelectMenu = 0;
const int quantitySelectMenu = 1;
const int wrapLoadMenu = 2;
const int wrapUnloadMenu = 3;
const int speedSelectMenu = 4;
const int cycleStartMenu = 5;

const int fwdDir = 0;     //direction of stepper
const int revDir = 1;     //valid values are 1/0/HIGH/LOW
const int cutterCycleSteps = 2500;   //calculate the steps needed to complete cutting cycle!
int stepperDirection;
word *stepperSpeed;     //used in delay() to change the speed of stepper writes
word loadingSpeed = 10000;    //also used for unloading speed
volatile float cycleSpeed = 1000;
word m1Steps;
word m2Steps;
volatile float length = 65.00;    //length of cut, used to calculate steps for feed stepper
volatile int interruptState1 = 0;     //used to count if the encoder rotation has triggered interrupt
volatile int interruptState2 = 0;     //used to count if the button interrupt has occured
volatile float quantity = 1;      //number of cycles to preform in run mode. convert to int!
volatile int focusValue = 0;      //used to hold value of which menu screen is selected
volatile float *menuValue;        //pointer to variable, slected by focusValue
const float stepFeed = 0.049063;   //distance traveled in each step (mm)
volatile float incrementValue;    //variable that holds the incremental steps for encoder rotation
char *menuText;                   //used to pass strings to updateOled() dependant on focusValue
char lengthString[] = "length";     //*these should be const but code breaks when they are*
char quantityString[] = "quantity";
char speedString[] = "speed";
char loadString[] = "press\nstart to\nload";
char unloadString[] = "press\nstart to\nunload";
char cycleStartString[] = "press\nstart to\nrun cycle";
char endstopTriggered[] = "endstop\ntriggered\nstart->ok"; //propts stepperHome()


void setup(){
  Serial.begin(9600);
  pinMode(pizeoPin, OUTPUT);
  pinMode(encoderDtPin, INPUT);
  pinMode(m1StepPin, OUTPUT);
  pinMode(m1DirPin, OUTPUT);
  pinMode(m2StepPin, OUTPUT);
  pinMode(m2DirPin, OUTPUT);
  pinMode(startBtnPin, INPUT_PULLUP);
  pinMode(stopBtnPin, INPUT_PULLUP);
  pinMode(limitSwitch1Pin, INPUT_PULLUP);
  pinMode(limitSwitch2Pin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(3), encoderState, FALLING);
  attachInterrupt(digitalPinToInterrupt(2), focusState, FALLING);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  delay(1000);
  //display.clearDisplay();  //this causes display not to work idk hwhy
  display.display();
  menuScreenSelect();
}


/*
  stepperHome is used to drive the axis of the cutter to its mechanical zero. Since there is no
  absolute positioning, all movments are based off of zero, which is set by this function on startup.
*/
void stepperHome(){   //need to zero cutting stepper with limit switch \\ active low
  stepperSpeed = &loadingSpeed;     //called at startup and anytime a limit switch is triggered during cycle
  while(digitalRead(limitSwitch1Pin) == HIGH){
    driveStepper(m2StepPin, m2DirPin, 1, stepperSpeed, revDir);
  }
}


void updateOled(char *Text, volatile float *value){
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0,0);
  display.println("Wrap cutter v1.0");
  display.setTextSize(2);
  display.setCursor(0,16);
  display.print(Text);
  Serial.println(focusValue);
  if(focusValue != wrapLoadMenu || wrapUnloadMenu || cycleStartMenu){
    Serial.println("if loop true");
    display.setCursor(0,38);
    display.print(*value);    //why can value be accessed this way but not Text?
  }
  display.display();
}


/*
  function: encoderState()
  inputs: none
  returns: void
  param: 
    function is of type ISR, to only be called by the CPU transparently!
    called after the encoder has triggered the interrupt on FALLING_EDGE.
    local int (dtState) stores the value of pin4 (1 or 0) that determines
      which direction the encoder has been rotated.
    changes the value of global float (length).
    all global variables are volatile!
    if a global variable used inside this function is changed outside of 
      this function, the operation must be atomic!
    nested in this function is providing audiable feedback through 
      a pizeo buzzer while also debouncing the encoders input.
    global int (interruptState) is used to indicate that the function
      has been executed and is used in the loop to update the display.
*/
void encoderState(){
  digitalWrite(pizeoPin, HIGH);
  delayMicroseconds(43);
  digitalWrite(pizeoPin, LOW);
  interruptState1 = 1;
  int dtState = digitalRead(9);
  if(digitalRead(3) == LOW){
    if(dtState == HIGH){
      *menuValue = *menuValue + incrementValue;
    }
    else if(dtState == LOW){
      *menuValue = *menuValue - incrementValue;
    }
  }
}


void focusState(){
  digitalWrite(pizeoPin, HIGH);
  delayMicroseconds(43);
  digitalWrite(pizeoPin, LOW);
  interruptState2 = 1;
  if(digitalRead(2) == LOW){
    if(focusValue < 5){
      focusValue++;
    }
    else{
      focusValue = 0;
    }
  }
}


void menuScreenSelect(){
  if(focusValue == lengthSelectMenu){
    menuText = lengthString;
    menuValue = &length;
    incrementValue = 0.05;
    updateOled(menuText, menuValue);
  }
  else if(focusValue == quantitySelectMenu){
    menuText = quantityString;
    menuValue = &quantity;
    incrementValue = 1.0;
    updateOled(menuText, menuValue);
  }
  else if(focusValue == wrapLoadMenu){
    loadDisplay();
  }
  else if(focusValue == wrapUnloadMenu){
    unloadDisplay();
  }
  else if(focusValue == speedSelectMenu){
    menuText = speedString;
    menuValue = &cycleSpeed;
    incrementValue = 50;
    updateOled(menuText, menuValue);
  }
  else if(focusValue == cycleStartMenu){
    cycleStartDsiaply();
  }
}


void driveStepper(const int motorStp, const int motorDir, word steps, word *speed, int direction){
  digitalWrite(motorDir, direction);
  for(word i = 0; i < steps; i++){
    digitalWrite(motorStp, HIGH);
    delayMicroseconds(*speed);
    digitalWrite(motorStp, LOW);
  }
}


void loadDisplay(){           //still need to disable external interrupt for pin 3 while 
  /*
  display.clearDisplay();     //this function executes
  display.setTextColor(WHITE);  //having individual functions for these menu screen is just sad
  display.setTextSize(1);
  display.setCursor(0,0);
  display.println("Wrap cutter v1.0");
  display.setTextSize(2);
  display.setCursor(0,16);
  display.print("press\nstart to\nload");
  display.display();
  */
  menuText = loadString;
  updateOled(menuText, menuValue);
  while(focusValue == wrapLoadMenu){
    if(digitalRead(startBtnPin) == 0){
      m1Steps = word(20.0 / stepFeed);
      stepperSpeed = &loadingSpeed;
      driveStepper(m1StepPin, m1DirPin, m1Steps, stepperSpeed, fwdDir);
      digitalWrite(pizeoPin,HIGH);    //give some sort of indication that the stepper is finished
      delay(100);
      digitalWrite(pizeoPin, LOW);
    }
  }
}


void unloadDisplay(){
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0,0);
  display.print("Wrap cutter v1.0");
  display.setTextSize(2);
  display.setCursor(0,16);
  display.print("press\nstart to\nunload");
  display.display();
  while(focusValue == wrapUnloadMenu){
    if(digitalRead(startBtnPin) == 0){
      m1Steps = word(20 / stepFeed);
      stepperSpeed = &loadingSpeed;
      driveStepper(m1StepPin, m1DirPin, m1Steps, stepperSpeed, revDir);
      digitalWrite(pizeoPin, HIGH);
      delay(100);
      digitalWrite(pizeoPin, LOW);
    }
  }
}


void cycleRun(){
  for(float i = 0; i < quantity; i++){
    m1Steps = word(length / stepFeed);
    word speed = word(cycleSpeed);
    stepperSpeed = &speed;
    driveStepper(m1StepPin, m1DirPin, m1Steps, stepperSpeed, fwdDir);
    m2Steps = cutterCycleSteps;
    driveStepper(m2StepPin, m2DirPin, m2Steps, stepperSpeed, fwdDir);
    delay(1);
    driveStepper(m2StepPin, m2DirPin, m2Steps, stepperSpeed, revDir);
  }
}


void cycleStartDsiaply(){
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0,0);
  display.print("Wrap cutter v1.0");
  display.setTextSize(2);
  display.setCursor(0,16);
  display.print("press\nstart to\nrun cycle");
  display.display();
  while(focusValue == cycleStartMenu){
    if(digitalRead(startBtnPin) == 0){
      cycleRun();
    }
  }
}


void loop(){
  if(interruptState1 == 1){
    interruptState1 = 0;
    updateOled(menuText, menuValue);
  }
  if(interruptState2 == 1){
    interruptState2 = 0;
    menuScreenSelect();
  }
}
