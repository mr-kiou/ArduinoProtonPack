#include <QueueArray.h>

// for the sound board
#include <SoftwareSerial.h>
#include "Adafruit_Soundboard.h"

#include <Adafruit_NeoPixel.h>

// for led triggers
#define HIGH 0x1
#define LOW  0x0

#define NEO_POWER 2 // for cyclotron and powercell
Adafruit_NeoPixel powerStick = Adafruit_NeoPixel(44, NEO_POWER, NEO_GRB + NEO_KHZ800);

// neopixel pins / setup
#define NEO_NOSE 3 // for nose of wand
Adafruit_NeoPixel noseJewel = Adafruit_NeoPixel(7, NEO_NOSE, NEO_GRB + NEO_KHZ800);

// neopixel pins / setup
#define NEO_WAND 4 // for nose of wand
Adafruit_NeoPixel wandLights = Adafruit_NeoPixel(4, NEO_WAND, NEO_GRB + NEO_KHZ800);

// LED indexes into the neopixel powerstick chain for the cyclotron
int c1Start = 16;
int c1End = 22;
int c2Start = 23;
int c2End = 29;
int c3Start = 30;
int c3End = 36;
int c4Start = 37;
int c4End = 44;

// inputs for switches and buttons
const int THEME_SWITCH = 5;
const int STARTUP_SWITCH = 6;
const int SAFETY_SWITCH = 7;
const int FIRE_BUTTON = 8;

// soundboard pins and setup
#define SFX_RST 10
#define SFX_RX 11
#define SFX_TX 12
const int ACT = 13;// this allows us to know if the audio is playing

SoftwareSerial ss = SoftwareSerial(SFX_TX, SFX_RX);
Adafruit_Soundboard sfx = Adafruit_Soundboard( & ss, NULL, SFX_RST);

// ##############################
// available options
// ##############################
const bool useGameCyclotronEffect = true; // set this to true to get the fading previous cyclotron light in the idle sequence

// Possible Pack states
bool powerBooted = false; // has the pack booted up
bool isFiring = false;      // keeps track of the firing state
bool shouldWarn = false;    // track the warning state for alert audio
bool shuttingDown = false;  // is the pack in the process of shutting down
bool poweredDown = true;

// current switch states
bool startup = false;
bool theme = false;
bool safety = false;
bool fire = false;
bool warning = false;

// audio track names on soundboard
const String startupTrack =   "T00     WAV";
const String blastTrack =     "T01     WAV";
const String endTrack =       "T02     WAV";
const String idleTrack =      "T03     WAV";
const String shutdownTrack =  "T04     WAV";
const String clickTrack =     "T05     WAV";
const String chargeTrack =    "T06     WAV";
const String warnTrack =      "T07     WAV";
const String ventTrack =      "T08     WAV";
const String texTrack =       "T09     WAV";
const String choreTrack =     "T10     WAV";
const String toolsTrack =     "T11     WAV";
const String listenTrack =    "T12     WAV";
const String goodTrack =      "T13     WAV";
const String thatTrack =      "T14     WAV";
const String neverTrack =     "T15     WAV";
const String hardTrack =      "T16     WAV";
const String neutronizedTrack="T17     WAV";
const String boxTrack =       "T18     WAV";
const String themeTrack =     "T19     OGG";

// this queue holds a shuffled list of dialog tracks we can pull from so we don't
// play the same ones twice
QueueArray <int> dialogQueue; 

// dialog trigger times/states
unsigned long startDialogMillis;
int dialogArray[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}; // array with an index for each dialog
const int dialogWaitTime = 5000; // how long to hold down fire for a dialog to play
const int warnWaitTime = 10000;  // how long to hold down fire before warning sounds

// Arduino setup function
void setup() {
  randomSeed(analogRead(A0)); // try to get a good random seed
  
  // softwareserial at 9600 baud for the audio board
  ss.begin(9600);

  // see if we have the soundboard
  // If we fail to communicate, loop forever for now but it would be nice to warn the user somehow
  if (!sfx.reset()) {
    while (1);
  }

  // set act modes for the fx board
  pinMode(ACT, INPUT);

  // configure nose jewel
  noseJewel.begin();
  powerStick.setBrightness(80);
  noseJewel.show(); // Initialize all pixels to 'off'

  // configure powercell/cyclotron
  powerStick.begin();
  powerStick.setBrightness(20);
  powerStick.show(); // Initialize all pixels to 'off'

  wandLights.begin();
  wandLights.setBrightness(40);
  wandLights.show();

  // set the modes for the switches/buttons
  pinMode(THEME_SWITCH, INPUT);
  digitalWrite(THEME_SWITCH, HIGH);
  pinMode(STARTUP_SWITCH, INPUT);
  digitalWrite(STARTUP_SWITCH, HIGH);
  pinMode(SAFETY_SWITCH, INPUT);
  digitalWrite(SAFETY_SWITCH, HIGH);
  pinMode(FIRE_BUTTON, INPUT);
  digitalWrite(FIRE_BUTTON, HIGH);
}

/* ************* Audio Board Helper Functions ************* */
// helper function to play a track by name on the audio board
void playTrack(String trackname) {
  // stop track if one is going
  int playing = digitalRead(ACT);
  if (playing == 0) {
    sfx.stop();
  }

  char charName[20];
  trackname.toCharArray(charName, 20);
  
  // now go play
  if (sfx.playTrack(charName)) {
    sfx.unpause();
  }
}

int getRandomTrack(){
  if ( !dialogQueue.isEmpty() ){
    // pull the next dialog
    return dialogQueue.dequeue();
  }else{
    // queue is empty so reset it and pull the first
    // random dialog
    int numDialog = sizeof(dialogArray) / sizeof(int);
    shuffleTrackArray( dialogArray, numDialog );
    
    for (int i = 0; i < numDialog; i++){
      dialogQueue.enqueue(dialogArray[i]);
    }

    return dialogQueue.dequeue();
  }
}

// function for sorting arrays found here.
// https://forum.arduino.cc/index.php?topic=345964.0
void shuffleTrackArray(int * array, int size){
  int last = 0;
  int temp = array[last];
  for (int i = 0; i < size; i++){
    int index = random(size);
    array[last] = array[index];
    last = index;
  }
  array[last] = temp;
}

/* ************* Main Loop ************* */
// intervals that can be adjusted in real time to speed up animations 
int pwr_interval = 60;        // interval at which to cycle lights for the powercell. We update this in the loop to speed up the animation so must be declared here (milliseconds)
int cyc_interval = 1000;      // interval at which to cycle lights for the cyclotron.
int cyc_fade_interval = 15;   // fade the inactive cyclotron to light to nothing
int firing_interval = 40;     // interval at which to cycle firing lights on the bargraph. We update this in the loop to speed up the animation so must be declared here (milliseconds).
int wandFlashInterval = 1000; // interval at which we flash the top led on the wand

void loop() {
  // get the current time
  int currentMillis = millis();

  // find out of the audio board is playing audio
  int playing = digitalRead(ACT);

  // get the current switch states
  int theme_switch = digitalRead(THEME_SWITCH);
  int startup_switch = digitalRead(STARTUP_SWITCH);
  int safety_switch = digitalRead(SAFETY_SWITCH);
  int fire_button = digitalRead(FIRE_BUTTON);

  // if the theme switch has recently changed from off to on we 
  // should play the full ghostbusters theme song
  if (theme_switch == 1) {
    if (theme == false) {
      playTrack(themeTrack);
      theme = true;
    }
  } else {
    theme = false;
  }
 
  // while the startup switch is set on
  if (startup_switch == 1) {   
    // in general we always try to play the idle sound if started
    if (playing == 1 && startup == true) {
      playTrack(idleTrack);
    }

    // choose the right powercell animation sequence for booted/on
    if ( powerBooted == true ) {
      poweredDown = false;
      shuttingDown = false;
      powerSequenceOne(currentMillis, pwr_interval, cyc_interval, cyc_fade_interval);
      flashWandLED(0, currentMillis, wandFlashInterval);    // set top light white blinking
      setWandLightState(1, 2);    // set back light orange
      setWandLightState(3, 0);    //set sloblow red
    } else {
      powerSequenceBoot(currentMillis);
    }

    // if we are not started up we should play the startup sound first
    if (startup == false) {
      startup = true;
      playTrack(startupTrack);

      // get the current safety switch state
      if (safety_switch == 1 && safety == false) {
        safety = true;
      }
    }

    if( startup == true && safety_switch == 1 ){
      setWandLightState(2, 3);    //set body white
    }
    
    // if the safety switch is set off then we can fire when the button is pressed
    if ( safety_switch == 1 && fire_button == 0) {
      // if the button is just pressed we clear all led's to start the firing animations
      if ( isFiring == false ) {
        shutdown_leds();
        isFiring = true;
      }

      // show the firing bargraph sequence
      barGraphSequenceTwo(currentMillis);
    } else { // if we were firing and are no longer reset the leds
      if ( isFiring == true ) {
        shutdown_leds();
        isFiring = false;
      }

      // and do the standard bargraph sequence
      barGraphSequenceOne(currentMillis);
    }

    // if we are started up fire loop
    if (startup == true && safety_switch == 1)
    {
      // if the safety was just changed play the click track
      if (safety == false) {
        safety = true;
        playTrack(chargeTrack);
      }

      // if the fire button is pressed
      if (fire_button == 0) {
        fireStrobe(currentMillis); // strobe the nose pixels

        // if this is the first time reset some variables and play the blast track
        if (fire == false) {
          shouldWarn = false;
          fire = true;
          startDialogMillis = millis();
          playTrack(blastTrack);
        } else {
          // find out what our timing is
          long diff = millis() - startDialogMillis;
          // if we are in the warn interval
          if ( diff > warnWaitTime) {
            pwr_interval = 20;    // speed up the powercell animation
            firing_interval = 20; // speed up the bar graph animation
            cyc_interval = 50;    // really speed up cyclotron
            cyc_fade_interval = 5;
            wandFlashInterval = 250;
            if (playing == 1 || shouldWarn == false ) {
              shouldWarn = true;
              playTrack(warnTrack); // play the firing track with the warning
            }
          } else if ( diff > dialogWaitTime) { // if we are in the dialog playing interval
            pwr_interval = 40;    // speed up the powercell animation
            firing_interval = 30; // speed up the bar graph animation
            cyc_interval = 300;   // speed up cyclotron
            wandFlashInterval = 500;
            cyc_fade_interval = 10;
            if (playing == 1) {
              playTrack(blastTrack); // play the normal blast track
            }
          }
        }
      } else { // now the button is no longer pressed
        if (fire == true) { // if we were firing let's reset the animations and play the correct final firing track
          clearFireStrobe();
          pwr_interval = 60;
          firing_interval = 40;
          cyc_interval = 1000;
          cyc_fade_interval = 15;
          wandFlashInterval = 1000;
          fire = false;

          // see if we've been firing long enough to get the dialog or vent sounds
          long diff = millis() - startDialogMillis;

          if ( diff > warnWaitTime) { // if we are past the warning let's vent the pack
            playTrack(ventTrack);
            clearPowerStrip(); // play the boot animation on the powercell
          } else if ( diff > dialogWaitTime) { // if in the dialog time play the dialog in sequence
            switch (getRandomTrack())
            {
              case (1):
                playTrack(choreTrack);
                break;
              case (2):
                playTrack(toolsTrack);
                break;
              case (3):
                playTrack(texTrack);
                break;
              case (4):
                playTrack(listenTrack);
                break;
              case (5):
                playTrack(goodTrack);
                break;
              case (6):
                playTrack(thatTrack);
                break;
              case (7):
                playTrack(neverTrack);
                break;
              case (8):
                playTrack(hardTrack);
                break;
              case (9):
                playTrack(neutronizedTrack);
                break;
              case (10):
                playTrack(boxTrack);
                break;
            }
          } else {
            // otherwise play the standard power down track
            playTrack(endTrack);
          }
        }
      }
    } else {
      // if the safety is switched off play the click track
      if (safety == true) {
        setWandLightState(2, 4);    //set body off
        safety = false;
        playTrack(clickTrack);
      }
    }
  } else { // if we are powering down
    if( poweredDown == false ){
      if( shuttingDown == false ){
        playTrack(shutdownTrack); // play the pack shutdown track
        shuttingDown = true;
      }
      powerSequenceShutdown(currentMillis);
    }else{
      if (startup == true) { // if started reset the variables
        clearPowerStrip(); // clear all led's
        shutdown_leds();
        startup = false;
        safety = false;
        fire = false;
      }
    }
  }
  delay(1);
}

/*************** Powercell/Cyclotron Animations *********************/
// timer helpers and intervals for the animations
unsigned long prevPwrBootMillis = 0;    // the last time we changed a powercell light in the boot sequence
const int pwr_boot_interval = 41;       // interval at which to cycle lights (milliseconds).

unsigned long prevCycBootMillis = 0;    // the last time we changed a cyclotron light in the boot sequence
const int cyc_boot_interval = 400;      // interval at which to cycle lights (milliseconds).

unsigned long prevShtdMillis = 0;       // last time we changed a light in the idle sequence
const int pwr_shutdown_interval = 200;  // interval at which to cycle lights (milliseconds).

unsigned long prevPwrMillis = 0;        // last time we changed a powercell light in the idle sequence
unsigned long prevCycMillis = 0;        // last time we changed a cyclotron light in the idle sequence
unsigned long prevFadeCycMillis = 0;    // last time we changed a cyclotron light in the idle sequence

// LED tracking variables
const int powerSeqTotal = 15;       // total number of led's for powercell 0 based
int powerSeqNum = 0;                // current running powercell sequence led
int powerShutdownSeqNum = 15;       // shutdown sequence counts down from 16

// animation level trackers for the boot and shutdown
int currentBootLevel = -1;          // current powercell boot level sequence led
int currentLightLevel = 15;         // current powercell boot light sequence led

void setWandLightState(int lednum, int state){
  switch ( state ) {
    case 0: // set led red
        wandLights.setPixelColor(lednum, wandLights.Color(255, 0, 0));
      break;
    case 1: // set led white
      wandLights.setPixelColor(lednum, wandLights.Color(255, 255, 255));
      break;
    case 2: // set led orange
      wandLights.setPixelColor(lednum, wandLights.Color(255, 255, 0));
      break;
    case 3: // set led orange
      wandLights.setPixelColor(lednum, wandLights.Color(0, 0, 255));
      break;
    case 4: // set led off
      wandLights.setPixelColor(lednum, 0);
      break;
  }

  wandLights.show();
}

unsigned long prevFlashMillis = 0;        // last time we changed a powercell light in the idle sequence
bool flashState = false;
void flashWandLED(int lednum, int currentMillis, int flashInterval){
  if (currentMillis - prevFlashMillis > flashInterval) {
    prevFlashMillis = currentMillis;
    if( flashState == false ){
      wandLights.setPixelColor(lednum, wandLights.Color(255, 255, 255));
      flashState = true;
    }else{
      wandLights.setPixelColor(lednum, 0);
      flashState = false;
    }
  }
  wandLights.show();
}

// helper function to set light states for the cyclotron
int cyclotronRunningFadeOut = 255;  // we reset this variable every time we change the cyclotron index so the fade effect works
void setCyclotronLightState(int startLed, int endLed, int state ){
  switch ( state ) {
    case 0: // set all leds to red
      for(int i=startLed; i <= endLed; i++) {
        powerStick.setPixelColor(i, powerStick.Color(255, 0, 0));
      }
      break;
    case 1: // set all leds to orange
      for(int i=startLed; i <= endLed; i++) {
        powerStick.setPixelColor(i, powerStick.Color(255, 106, 0));
      }
      break;
    case 2: // set all leds off
      for(int i=startLed; i <= endLed; i++) {
        powerStick.setPixelColor(i, 0);
      }
      break;
    case 3: // fade all leds from red
      for(int i=startLed; i <= endLed; i++) {
        if( cyclotronRunningFadeOut >= 0 ){
          powerStick.setPixelColor(i, 255 * cyclotronRunningFadeOut/255, 0, 0);
          cyclotronRunningFadeOut--;
        }else{
          powerStick.setPixelColor(i, 0);
        }
      }
      break;
  }
}

// shuts off and resets the powercell/cyclotron leds
void clearPowerStrip() {
  // reset vars
  powerBooted = false;
  poweredDown = true;
  powerSeqNum = 0;
  powerShutdownSeqNum = 15;
  currentLightLevel = 15;
  currentBootLevel = -1;
  
  // shutoff the leds
  for ( int i = 0; i <= c4End; i++) {
    powerStick.setPixelColor(i, 0);
  }
  powerStick.show();

  for ( int j=0; j<=3; j++ ){
    wandLights.setPixelColor(j, 0);
  }
  wandLights.show();
}

// boot animation on the powercell/cyclotron
bool reverseBootCyclotron = false;
void powerSequenceBoot(int currentMillis) {
  bool doUpdate = false;
  
  if (currentMillis - prevCycBootMillis > cyc_boot_interval) {
    prevCycBootMillis = currentMillis;
    
    // START CYCLOTRON
    if( reverseBootCyclotron == false ){
      setCyclotronLightState(c1Start, c1End, 1);
      setCyclotronLightState(c2Start, c2End, 2);
      setCyclotronLightState(c3Start, c3End, 1);
      setCyclotronLightState(c4Start, c4End, 2);
      
      doUpdate = true;
      reverseBootCyclotron = true;
    }else{
      setCyclotronLightState(c1Start, c1End, 2);
      setCyclotronLightState(c2Start, c2End, 1);
      setCyclotronLightState(c3Start, c3End, 2);
      setCyclotronLightState(c4Start, c4End, 1);
      
      doUpdate = true;
      reverseBootCyclotron = false;
    }
    // END CYCLOTRON
  }
  
  if (currentMillis - prevPwrBootMillis > pwr_boot_interval) {
    // save the last time you blinked the LED
    prevPwrBootMillis = currentMillis;

    // START POWERCELL
    if( currentBootLevel != powerSeqTotal ){
      if( currentBootLevel == currentLightLevel){
        powerStick.setPixelColor(currentBootLevel, powerStick.Color(0, 0, 150));
        currentLightLevel = powerSeqTotal;
        currentBootLevel++;
      }else{
        if(currentLightLevel+1 <= powerSeqTotal){
          powerStick.setPixelColor(currentLightLevel+1, 0);
        }
        powerStick.setPixelColor(currentLightLevel, powerStick.Color(0, 0, 150));
        currentLightLevel--;
      }
      doUpdate = true;
    }else{
      powerBooted = true;
      currentBootLevel = -1;
      currentLightLevel = 15;
    }
    // END POWERCELL
  }

  // if we have changed an led
  if( doUpdate == true ){
    powerStick.show(); // commit all of the changes
  }
}

// idle/firing animation for the powercell/cyclotron
int cycOrder = 0;     // which cyclotron led will be lit next
int cycFading = -1;   // which cyclotron led is fading out for game style
void powerSequenceOne(int currentMillis, int anispeed, int cycspeed, int cycfadespeed) {
  bool doUpdate = false;  // keep track of if we changed something so we only update on changes

  // START CYCLOTRON 
  if( useGameCyclotronEffect == true ){ // if we are doing the video game style cyclotron
    if (currentMillis - prevCycMillis > cycspeed) {
      prevCycMillis = currentMillis;
      
      switch ( cycOrder ) {
        case 0:
          setCyclotronLightState(c1Start, c1End, 0);
          setCyclotronLightState(c2Start, c2End, 2);
          setCyclotronLightState(c3Start, c3End, 2);
          cycFading = 0;
          cyclotronRunningFadeOut = 255;
          cycOrder = 1;
          break;
        case 1:
          setCyclotronLightState(c2Start, c2End, 0);
          setCyclotronLightState(c3Start, c3End, 2);
          setCyclotronLightState(c4Start, c4End, 2);
          cycFading = 1;
          cyclotronRunningFadeOut = 255;
          cycOrder = 2;
          break;
        case 2:
          setCyclotronLightState(c1Start, c1End, 2);
          setCyclotronLightState(c3Start, c3End, 0);
          setCyclotronLightState(c4Start, c4End, 2);
          cycFading = 2;
          cyclotronRunningFadeOut = 255;
          cycOrder = 3;
          break;
        case 3:
          setCyclotronLightState(c1Start, c1End, 2);
          setCyclotronLightState(c2Start, c2End, 2);
          setCyclotronLightState(c4Start, c4End, 0);
          cycFading = 3;
          cyclotronRunningFadeOut = 255;
          cycOrder = 0;
          break;
      }
  
      doUpdate = true;
    }
  
    // now figure out the fading light
    if( currentMillis - prevFadeCycMillis > cycfadespeed ){
      prevFadeCycMillis = currentMillis;
      if( cycFading != -1 ){
        switch ( cycFading ) {
          case 0:
            setCyclotronLightState(c4Start, c4End, 3);
            break;
          case 1:
            setCyclotronLightState(c1Start, c1End, 3);
            break;
          case 2:
            setCyclotronLightState(c2Start, c2End, 3);
            break;
          case 3:
            setCyclotronLightState(c3Start, c3End, 3);
            break;
        }
        doUpdate = true;
      }
    }
  }else{ // otherwise this is the standard version
    if (currentMillis - prevCycMillis > cycspeed) {
      prevCycMillis = currentMillis;
      
      switch ( cycOrder ) {
        case 0:
          setCyclotronLightState(c4Start, c4End, 2);
          setCyclotronLightState(c1Start, c1End, 0);
          setCyclotronLightState(c2Start, c2End, 2);
          setCyclotronLightState(c3Start, c3End, 2);
          cycFading = 0;
          cyclotronRunningFadeOut = 255;
          cycOrder = 1;
          break;
        case 1:
          setCyclotronLightState(c1Start, c1End, 2);
          setCyclotronLightState(c2Start, c2End, 0);
          setCyclotronLightState(c3Start, c3End, 2);
          setCyclotronLightState(c4Start, c4End, 2);
          cycFading = 1;
          cyclotronRunningFadeOut = 255;
          cycOrder = 2;
          break;
        case 2:
          setCyclotronLightState(c1Start, c1End, 2);
          setCyclotronLightState(c2Start, c2End, 2);
          setCyclotronLightState(c3Start, c3End, 0);
          setCyclotronLightState(c4Start, c4End, 2);
          cycFading = 2;
          cyclotronRunningFadeOut = 255;
          cycOrder = 3;
          break;
        case 3:
          setCyclotronLightState(c1Start, c1End, 2);
          setCyclotronLightState(c2Start, c2End, 2);
          setCyclotronLightState(c3Start, c3End, 2);
          setCyclotronLightState(c4Start, c4End, 0);
          cycFading = 3;
          cyclotronRunningFadeOut = 255;
          cycOrder = 0;
          break;
      }
  
      doUpdate = true;
    }
  }
  // END CYCLOTRON

  // START POWERCELL
  if (currentMillis - prevPwrMillis > anispeed) {
    // save the last time you blinked the LED
    prevPwrMillis = currentMillis;

    for ( int i = 0; i <= powerSeqTotal; i++) {
      if ( i <= powerSeqNum ) {
        powerStick.setPixelColor(i, powerStick.Color(0, 0, 150));
      } else {
        powerStick.setPixelColor(i, 0);
      }
    }
    
    if ( powerSeqNum <= powerSeqTotal) {
      powerSeqNum++;
    } else {
      powerSeqNum = 0;
    }

    doUpdate = true;
  }
  // END POWERCELL

  // if we changed anything update
  if( doUpdate == true ){
    powerStick.show();
  }
}

// shutdown animation for the powercell/cyclotron
int cyclotronFadeOut = 255;
void powerSequenceShutdown(int currentMillis) {
  if (currentMillis - prevShtdMillis > pwr_shutdown_interval) {
    prevShtdMillis = currentMillis;

    // START CYCLOTRON
    for(int i=c1Start; i <= c4End; i++) {
      if( cyclotronFadeOut >= 0 ){
        powerStick.setPixelColor(i, 255 * cyclotronFadeOut/255, 0, 0);
        cyclotronFadeOut--;
      }else{
        powerStick.setPixelColor(i, 0);
      }
    }
    // END CYCLOTRON
    
    // START POWERCELL
    for ( int i = powerSeqTotal; i >= 0; i--) {
      if ( i <= powerShutdownSeqNum ) {
        powerStick.setPixelColor(i, powerStick.Color(0, 0, 150));
      } else {
        powerStick.setPixelColor(i, 0);
      }
    }
    
    powerStick.show();
    
    if ( powerShutdownSeqNum >= 0) {
      powerShutdownSeqNum--;
    } else {
      poweredDown = true;
      powerShutdownSeqNum = 15;
      cyclotronFadeOut = 255;
    }
    // END POWERCELL
  }
}

/*************** Nose Jewel Firing Animations *********************/
unsigned long prevFireMillis = 0;
const int fire_interval = 50;     // interval at which to cycle lights (milliseconds).
int fireSeqNum = 0;
int fireSeqTotal = 5;

void clearFireStrobe() {
  for ( int i = 0; i < 7; i++) {
    noseJewel.setPixelColor(i, 0);
  }
  noseJewel.show();
  fireSeqNum = 0;
}

void fireStrobe(int currentMillis) {
  if (currentMillis - prevFireMillis > fire_interval) {
    prevFireMillis = currentMillis;
    
    switch ( fireSeqNum ) {
      case 0:
        noseJewel.setPixelColor(0, noseJewel.Color(255, 255, 255));
        noseJewel.setPixelColor(1, noseJewel.Color(255, 255, 255));
        noseJewel.setPixelColor(2, 0);
        noseJewel.setPixelColor(3, noseJewel.Color(255, 255, 255));
        noseJewel.setPixelColor(4, 0);
        noseJewel.setPixelColor(5, noseJewel.Color(255, 255, 255));
        noseJewel.setPixelColor(6, 0);
        break;
      case 1:
        noseJewel.setPixelColor(0, noseJewel.Color(0, 0, 255));
        noseJewel.setPixelColor(1, noseJewel.Color(255, 0, 0));
        noseJewel.setPixelColor(2, noseJewel.Color(255, 255, 255));
        noseJewel.setPixelColor(3, noseJewel.Color(255, 0, 0));
        noseJewel.setPixelColor(4, noseJewel.Color(255, 255, 255));
        noseJewel.setPixelColor(5, noseJewel.Color(255, 0, 0));
        noseJewel.setPixelColor(6, noseJewel.Color(255, 255, 255));
        break;
      case 2:
        noseJewel.setPixelColor(0, noseJewel.Color(255, 0, 0));
        noseJewel.setPixelColor(1, 0);
        noseJewel.setPixelColor(2, noseJewel.Color(0, 0, 255));
        noseJewel.setPixelColor(3, 0);
        noseJewel.setPixelColor(4, noseJewel.Color(0, 0, 255));
        noseJewel.setPixelColor(5, 0);
        noseJewel.setPixelColor(6, noseJewel.Color(255, 0, 0));
        break;
      case 3:
        noseJewel.setPixelColor(0, noseJewel.Color(0, 0, 255));
        noseJewel.setPixelColor(1, noseJewel.Color(255, 0, 0));
        noseJewel.setPixelColor(2, noseJewel.Color(255, 255, 255));
        noseJewel.setPixelColor(3, noseJewel.Color(255, 0, 0));
        noseJewel.setPixelColor(4, noseJewel.Color(255, 255, 255));
        noseJewel.setPixelColor(5, noseJewel.Color(255, 0, 0));
        noseJewel.setPixelColor(6, noseJewel.Color(255, 255, 255));
        break;
      case 4:
        noseJewel.setPixelColor(0, noseJewel.Color(255, 0, 0));
        noseJewel.setPixelColor(1, 0);
        noseJewel.setPixelColor(2, noseJewel.Color(255, 255, 255));
        noseJewel.setPixelColor(3, 0);
        noseJewel.setPixelColor(4, noseJewel.Color(255, 0, 0));
        noseJewel.setPixelColor(5, 0);
        noseJewel.setPixelColor(6, noseJewel.Color(255, 255, 255));
        break;
      case 5:
        noseJewel.setPixelColor(0, noseJewel.Color(255, 0, 255));
        noseJewel.setPixelColor(1, noseJewel.Color(0, 255, 0));
        noseJewel.setPixelColor(2, noseJewel.Color(255, 0, 0));
        noseJewel.setPixelColor(3, noseJewel.Color(0, 0, 255));
        noseJewel.setPixelColor(4, noseJewel.Color(255, 0, 255));
        noseJewel.setPixelColor(5, noseJewel.Color(255, 255, 255));
        noseJewel.setPixelColor(6, noseJewel.Color(0, 0, 255));
        break;
    }
  
    noseJewel.show();
  
    fireSeqNum++;
    if ( fireSeqNum > fireSeqTotal ) {
      fireSeqNum = 0;
    }
  }
}

/*************** Bar Graph Animations *********************/
void shutdown_leds() {
  // stub function for when I re-enable to bargraph
}
void barGraphSequenceOne(int currentMillis) {
  // stub function for when I re-enable to bargraph
}
void barGraphSequenceTwo(int currentMillis) {
  // stub function for when I re-enable to bargraph
}

