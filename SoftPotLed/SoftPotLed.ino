#include "FastLED.h"
#include "MIDIUSB.h"

#define CARTE_ID 1                           //Id de la carte pour changer les midi et CC automatiquement
//la premiere carte est ID 0, 2eme carte ID 1, etc.

#define NUM_LEDS 30                            //Nombre de leds par strip
#define NUM_STICKS 5                          //Nombre de strips par carte
#define NUM_ZONES 2                           //Nombre de zones sur chaque strip
#define NUM_LEDS_PER_ZONE NUM_LEDS/NUM_ZONES  //auto calcul du nombre de leds par zone
#define LED_TOLERANCE 2                       //Largeur de la zone bleue quand on appuie
#define MIDI_CHANNEL 15                       //Channel midi sur lequel envoyer les infos

#define MIDI_START_NOTE 30                    //Premiere note, puis les 9 suivantes seront incrémentées
//ex. si MIDI_START_NOTE = 30, les notes seront 30,31,32,..,39
#define CC_START_NUMBER  0                   //Premier controlChange                   

//Auto offset depending on CARTE_ID
#define TOTAL_ZONES               NUM_STICKS*NUM_ZONES
#define MIDI_START_NOTE_FINAL     MIDI_START_NOTE+CARTE_ID*TOTAL_ZONES
#define CC_START_NUMBER_FINAL     CC_START_NUMBER+CARTE_ID*TOTAL_ZONES

CRGB baseColor(51, 45, 30);                   //Couleur de base (blanc chaud)
CRGB overColor(10, 20, 150);                  //Couleur à la pression
CRGB liveColor(20, 100, 20);                  //Couleur quand midi note arrive

CRGB leds[NUM_STICKS][NUM_LEDS];
const int potPins[NUM_STICKS] = {A0, A1, A2, A3, A4};
float stickZoneValues[NUM_STICKS][NUM_ZONES];
float liveZoneValues[NUM_STICKS][NUM_ZONES];

long pingTime;
long lastPing = 0;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  delay(1000);
  for (int i = 0; i < NUM_STICKS; i++)
  {
    pinMode(potPins[i], INPUT);

    for (int j = 0; j < NUM_ZONES; j++)
    {
      stickZoneValues[i][j] = 0;
      liveZoneValues[i][j] = 0;
    }
  }

  FastLED.addLeds<WS2812B, 2 , GRB>(leds[0], NUM_LEDS);
  FastLED.addLeds<WS2812B, 3 , GRB>(leds[1], NUM_LEDS);
  FastLED.addLeds<WS2812B, 4 , GRB>(leds[2], NUM_LEDS);
  FastLED.addLeds<WS2812B, 5 , GRB>(leds[3], NUM_LEDS);
  FastLED.addLeds<WS2812B, 6 , GRB>(leds[4], NUM_LEDS);

  pingTime = 1000;
}

void loop() {

  processMIDI();

  for (int i = 0; i < NUM_STICKS; i++)
  {

    int val = analogRead(potPins[i]);

    //Simple 4 division to rectify linearity
    float t = 0;
    const int middle = 330;
    const int middleEnd =  530;

    if (val < middle) t = map(val, 0, middle, 0, 512);
    else if (val < middleEnd) t = map(val, middle, middleEnd, 512, 768);
    else t = map(val, middleEnd, 1024, 768, 1024);
    t /= 1024.;

    //Zones
    int zone = floor(t * NUM_ZONES);

    for (int j = 0; j < NUM_ZONES; j++)
    {
      bool isOver = zone == j;
      float tZone = 0;
      if (isOver) tZone = min(max(fmod(t * NUM_ZONES, 1), 0), 1);

      int midiVal = tZone * 127;
      int midiNote = MIDI_START_NOTE_FINAL + i * NUM_ZONES + j;
      int ccNumber = CC_START_NUMBER_FINAL + i * NUM_ZONES + j;

      if (tZone != stickZoneValues[i][j])
      {
        if (tZone == 0)
        {
          Serial.print(i);
          Serial.print('.');
          Serial.print(j);
          Serial.println(" released");
          sendNoteOff(MIDI_CHANNEL, midiNote, 0); //Force note off to velocity 0

        } else if (stickZoneValues[i][j] == 0)
        {
          Serial.print(i);
          Serial.print('.');
          Serial.print(j);
          Serial.println(" pressed");
          sendNoteOff(MIDI_CHANNEL, midiNote, midiVal);

        }

        stickZoneValues[i][j] = tZone;
        Serial.print(i);
        Serial.print('.');
        Serial.print(j);
        Serial.print(" changed : ");
        Serial.println(tZone);
        sendControlChange(MIDI_CHANNEL, ccNumber, midiVal);
      }

      //LEDS

      int startLed = j * NUM_LEDS_PER_ZONE;
      int targetLed = stickZoneValues[i][j] * NUM_LEDS_PER_ZONE;
      int targetLiveLed = liveZoneValues[i][j] * NUM_LEDS_PER_ZONE;
      
      bool pressActive = tZone > 0 && tZone < 1;
      bool liveActive = liveZoneValues[i][j] > 0 && liveZoneValues[i][j] <= 1;

      for (int k = 0; k < NUM_LEDS_PER_ZONE; k++)
      {
        bool isInTolerance = k >= targetLed - LED_TOLERANCE && k <= targetLed + LED_TOLERANCE;
        bool isInLiveTolerance = k >= targetLiveLed - LED_TOLERANCE && k <= targetLiveLed + LED_TOLERANCE;

        if (liveActive && isInLiveTolerance) leds[i][startLed+k] = liveColor;
        else if (pressActive & isInTolerance) leds[i][startLed+k] = overColor;
        else leds[i][startLed+k] = baseColor;
      }
    }
  }

  MidiUSB.flush();
  FastLED.show();
}


void sendNoteOn(byte channel, byte pitch, byte velocity) {
  midiEventPacket_t noteOn = {0x09, 0x90 | channel, pitch, velocity};
  MidiUSB.sendMIDI(noteOn);
}

void sendNoteOff(byte channel, byte pitch, byte velocity) {
  midiEventPacket_t noteOff = {0x08, 0x80 | channel, pitch, velocity};
  MidiUSB.sendMIDI(noteOff);
}

void sendControlChange(byte channel, byte control, byte value) {
  midiEventPacket_t event = {0x0B, 0xB0 | channel, control, value};
  MidiUSB.sendMIDI(event);
}

void processNoteOn(byte channel, byte pitch, byte velocity) {
  int stick = (pitch - MIDI_START_NOTE_FINAL) / NUM_ZONES;
  int zone = (pitch - MIDI_START_NOTE_FINAL) - (stick * NUM_ZONES);
  
  liveZoneValues[stick][zone] = velocity * 1. / 127.;
  Serial.print("Note on ");
  Serial.print(stick);
  Serial.print(" ");
  Serial.print(zone);
  Serial.print(" ");
  Serial.println(liveZoneValues[stick][zone]);
}

void processNoteOff(byte channel, byte pitch, byte velocity) {
  int stick = (pitch - MIDI_START_NOTE_FINAL) / NUM_ZONES;
  int zone = (pitch - MIDI_START_NOTE_FINAL) - (stick * NUM_ZONES);
  liveZoneValues[stick][zone] = 0;
}

void processControlChange(byte channel, byte pitch, byte velocity) {
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

