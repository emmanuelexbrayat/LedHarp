#include "FastLED.h"
#include "MIDIUSB.h"

#define CARTE_ID 1                              //Id de la carte pour changer les midi et CC automatiquement
//la premiere carte est ID 1, 2eme carte ID 2, etc.

#if CARTE_ID == 1
#define MIDI_START_NOTE 60                    //60 = C3 / Premiere note, puis les x suivantes seront incrémentées
#define CC_START_NUMBER  15                   //Premier controlChange   
#define NUM_STICKS 4                         //Nombre de strips par carte  
#define UPSIDE_DOWN 0                        //Pas inversé (partie basse)

#else if CARTE_ID == 2
#define MIDI_START_NOTE 72                  //1ere note de cette carte (72 = C4)
#define CC_START_NUMBER 23                    //1er CC de cette carte
#define NUM_STICKS 6                         //Nombre de strips par carte
#define UPSIDE_DOWN 1                       //Inversé (partie haute)
#endif

#define NUM_LEDS 30                            //Nombre de leds par strip
#define NUM_ZONES 2                           //Nombre de zones sur chaque strip
#define LED_TOLERANCE 2                       //Largeur de la zone bleue quand on appuie
#define MIDI_CHANNEL 15                       //Channel midi sur lequel envoyer les notes
#define CC_CHANNEL 16                         //Channel sur lequel envoyer les CC
#define RECEIVE_CHANNEL 14                    //Channel de reception des notes et CC

CRGB baseColor(51, 45, 30);                   //Couleur de base (blanc chaud)
CRGB overColor(10, 20, 255);                  //Couleur à la pression (bleu)
CRGB liveZoneColor(20, 100, 0);               //Couleur quand midi note arrive (vert)
CRGB liveCCColor(255, 60, 0);                //Couleur quand CC arrive (orange) 

#if CARTE_ID == 1
const int potPins[NUM_STICKS] = {A0, A1, A2, A3};
#elif CARTE_ID == 2
const int potPins[NUM_STICKS] = {A0, A1, A2, A3, A4, A5};
#endif


//Ne pas trop toucher, filtrage du bruit de masse
const int noiseFilter = 30; //min value for mass noise default 30, values between 0-1024
const float noiseSmooth = .3f; //0 = no smooth, 1 = too much smooth (never use 1)
const float minChangeVal = .02f;


//Ne pas toucher
#define NUM_LEDS_PER_ZONE NUM_LEDS/NUM_ZONES  //auto calcul du nombre de leds par zone
#define TOTAL_ZONES NUM_STICKS*NUM_ZONES      //Automatique, ne pas changer

CRGB leds[NUM_STICKS][NUM_LEDS];


float stickZoneValues[NUM_STICKS][NUM_ZONES];
float liveCCValues[NUM_STICKS][NUM_ZONES];
bool liveZoneValues[NUM_STICKS][NUM_ZONES];

long pingTime;
long lastPing = 0;

//Control Change release mode
bool ccReleaseEnabled = false;


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  delay(1000);
  for (int i = 0; i < NUM_STICKS; i++)
  {
    pinMode(potPins[i], INPUT);

    for (int j = 0; j < NUM_ZONES; j++)
    {
      stickZoneValues[i][j] = 0;
      liveZoneValues[i][j] = false;
      liveCCValues[i][j] = 0;
    }
  }

  FastLED.addLeds<WS2812B, 2 , GRB>(leds[0], NUM_LEDS);
  FastLED.addLeds<WS2812B, 3 , GRB>(leds[1], NUM_LEDS);
  FastLED.addLeds<WS2812B, 4 , GRB>(leds[2], NUM_LEDS);
  FastLED.addLeds<WS2812B, 5 , GRB>(leds[3], NUM_LEDS);
#if CARTE_ID == 2
  FastLED.addLeds<WS2812B, 6 , GRB>(leds[4], NUM_LEDS);
  FastLED.addLeds<WS2812B, 7 , GRB>(leds[5], NUM_LEDS);
#endif

  pingTime = 1000;
}

void loop() {

  processMIDI();

  for (int i = 0; i < NUM_STICKS; i++)
  {

    int val = analogRead(potPins[i]);

    if (val < noiseFilter) val = 0;
    val = map(val, noiseFilter, 1024, 0, 1024);

    //Simple 4 division to rectify linearity
    float t = 0;
    const int middle = 330;
    const int middleEnd =  530;

    if (val < middle) t = map(val, 0, middle, 0, 512);
    else if (val < middleEnd) t = map(val, middle, middleEnd, 512, 768);
    else t = map(val, middleEnd, 1024, 768, 1024);
    t /= 1024.;

#if UPSIDE_DOWN
    if (t > 0) t = 1 - t;
#endif

    //Zones
    int zone = floor(t * NUM_ZONES);


    for (int j = 0; j < NUM_ZONES; j++)
    {
      bool isOver = zone == j;
      float tZone = 0;
      if (isOver) tZone = min(max(fmod(t * NUM_ZONES, 1), 0), 1);

      int midiVal = tZone * 127;
      int midiNote = MIDI_START_NOTE + i * NUM_ZONES + j;
      int ccNumber = CC_START_NUMBER + i * NUM_ZONES + j;

      if (tZone != stickZoneValues[i][j])
      {
        bool forceSendChanged = false;

        if (tZone == 0)
        {
          stickZoneValues[i][j] = tZone;
          if (ccReleaseEnabled) forceSendChanged = true;

          Serial.print(i);
          Serial.print('.');
          Serial.print(j);
          Serial.println(" released");
          sendNoteOff(MIDI_CHANNEL, midiNote, 0); //Force note off to velocity 0

        } else if (stickZoneValues[i][j] == 0)
        {
          stickZoneValues[i][j] = tZone;
          forceSendChanged = true;
          Serial.print(i);
          Serial.print('.');
          Serial.print(j);
          Serial.println(" pressed");
          sendNoteOn(MIDI_CHANNEL, midiNote, 127);
        }

        int oldMidiVal = stickZoneValues[i][j] * 127;
        float tZoneSmoothed = stickZoneValues[i][j] + (tZone - stickZoneValues[i][j]) * (1 - noiseSmooth);
        int midiValSmoothed = tZoneSmoothed * 127;

        if (forceSendChanged ||
            (midiValSmoothed != oldMidiVal && abs(tZoneSmoothed - stickZoneValues[i][j]) > minChangeVal)
           )
        {
          stickZoneValues[i][j] = tZoneSmoothed;
          Serial.print(i);
          Serial.print('.');
          Serial.print(j);
          Serial.print(" changed : ");
          Serial.print(tZoneSmoothed);
          Serial.print(" ( CC value : ");
          Serial.print(midiValSmoothed);
          Serial.print(" )");
          Serial.println("");
          sendControlChange(CC_CHANNEL, ccNumber, midiValSmoothed);
        }

      }

      //LEDS

      int startLed = j * NUM_LEDS_PER_ZONE;
      int targetLed = stickZoneValues[i][j] * NUM_LEDS_PER_ZONE;
      int targetLiveLed = liveCCValues[i][j] * NUM_LEDS_PER_ZONE;

#if UPSIDE_DOWN
      startLed = NUM_LEDS_PER_ZONE - startLed - 1;
      targetLed = NUM_LEDS_PER_ZONE - targetLed - 1;
      targetLiveLed = NUM_LEDS_PER_ZONE - targetLiveLed - 1;
#endif

      bool pressActive = tZone > 0 && tZone < 1;
      bool liveCCActive = liveCCValues[i][j] > 0 && liveCCValues[i][j] <= 1;

      CRGB bgColor = liveZoneValues[i][j] ? liveZoneColor : baseColor;
      int ledTolerance = LED_TOLERANCE;
      if (liveCCActive) ledTolerance = 1;
      for (int k = 0; k < NUM_LEDS_PER_ZONE; k++)
      {

        bool isInTolerance = k >= targetLed - ledTolerance && k <= targetLed + ledTolerance;
        bool isInLiveTolerance = k >= targetLiveLed - ledTolerance && k <= targetLiveLed + ledTolerance;

        if (liveCCActive && isInLiveTolerance) leds[i][startLed + k] = liveCCColor;
        else if (pressActive & isInTolerance) leds[i][startLed + k] = overColor;
        else leds[i][startLed + k] = bgColor;
      }
    }
  }

  MidiUSB.flush();
  FastLED.show();
}


void sendNoteOn(byte channel, byte pitch, byte velocity) {
  channel--; //0-15 > 1-16
  midiEventPacket_t noteOn = {0x09, 0x90 | channel, pitch, velocity};
  MidiUSB.sendMIDI(noteOn);
}

void sendNoteOff(byte channel, byte pitch, byte velocity) {
  channel--;//0-15 > 1-16
  midiEventPacket_t noteOff = {0x08, 0x80 | channel, pitch, velocity};
  MidiUSB.sendMIDI(noteOff);
}

void sendControlChange(byte channel, byte control, byte value) {
  channel--; //0-15 > 1-16
  midiEventPacket_t event = {0x0B, 0xB0 | channel, control, value};
  MidiUSB.sendMIDI(event);
}

void processNoteOn(byte channel, byte pitch, byte velocity) {
  if (channel == RECEIVE_CHANNEL - 1)
  {
    int stick = (pitch - MIDI_START_NOTE) / NUM_ZONES;
    int zone = (pitch - MIDI_START_NOTE) - (stick * NUM_ZONES);
    liveZoneValues[stick][zone] = true;
  }
}

void processNoteOff(byte channel, byte pitch, byte velocity) {
  if (channel == RECEIVE_CHANNEL - 1)
  {
    int stick = (pitch - MIDI_START_NOTE) / NUM_ZONES;
    int zone = (pitch - MIDI_START_NOTE) - (stick * NUM_ZONES);
    liveZoneValues[stick][zone] = false;
  }
}


void processControlChange(byte channel, byte number, byte value) {
  if (channel == RECEIVE_CHANNEL - 1) //13 (0-15) = 14 (1-16)
  {
    if (number == 127)
    {
      if (value > 64) ccReleaseEnabled = true;
      else ccReleaseEnabled = false;

      Serial.print("CC Release ");
      Serial.println(ccReleaseEnabled ? "enabled" : "disabled");
    } else
    {
      int stick = (number - CC_START_NUMBER) / NUM_ZONES;
      int zone = (number - CC_START_NUMBER) - (stick * NUM_ZONES);

      Serial.print("Process CC ");
      Serial.print(stick);
      Serial.print(" ");
      Serial.print(zone);
      Serial.print(" ");

      if (stick < NUM_STICKS && zone < NUM_ZONES)
      {
        liveCCValues[stick][zone] = value * 1. / 127.;
        Serial.println(liveZoneValues[stick][zone]);
      } else
      {
        Serial.println("OUT !");
      }

    }
  }
}

void processMIDI()
{
  midiEventPacket_t rx = MidiUSB.read();
  switch (rx.header) {
    case 0:
      break; //No pending events

    case 0x9:
      processNoteOn(
        rx.byte1 & 0xF,  //channel
        rx.byte2,        //pitch
        rx.byte3         //velocity
      );
      break;

    case 0x8:
      processNoteOff(
        rx.byte1 & 0xF,  //channel
        rx.byte2,        //pitch
        rx.byte3         //velocity
      );
      break;

    case 0xB:
      processControlChange(
        rx.byte1 & 0xF,  //channel
        rx.byte2,        //control
        rx.byte3         //value
      );
      break;

    default:
      Serial.print("Unhandled MIDI message: ");
      Serial.print(rx.header, HEX);
      Serial.print("-");
      Serial.print(rx.byte1, HEX);
      Serial.print("-");
      Serial.print(rx.byte2, HEX);
      Serial.print("-");
      Serial.println(rx.byte3, HEX);
  }
}

