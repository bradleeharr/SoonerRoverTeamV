/*
    THIS TEENSY RECEIVES UDP MESSAGES FROM ROUTER AND SENDS
      CAN MESSAGES TO OTHER MICROCONTROLLERS ON ROVER. IT IS
      ALSO RESPONSIBLE FOR CONTROLLING WHEELS FOR NOW

     Pin wiring diagram

     [],pin  front   [],pin
     0,2     0-|-0    3,11
               |
     1,9     0-|-0   -4,12
               |
    -2,20    0-|-0   -5,13
     "-" indicaes wheel's polarity needs to be reversed

     gimbal pan: D6
     gimbal tilt: D7

     LED RGB: 19,20,21

     Pins used by Ethernet:

*/

#ifndef __IMXRT1062__
  #error "This sketch should be compiled for Teensy 4.x"
#endif

#include <NativeEthernet.h> // NativeEthernet.h and NativeEthernetUDP.h are the exact same as the arduino Ethernet.h and EtherbetUDP.h.
#include <NativeEthernetUdp.h>
#include <ACAN_t4.h>
// Enter a MAC address and IP address for your controller below.
// The IP address will be dependent on your local network:
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
IPAddress ip(10, 0, 0, 101);

unsigned int localPort = 1001;      // local port to listen on

// buffers for receiving and sending data
char packetBuffer[UDP_TX_PACKET_MAX_SIZE];  // buffer to hold incoming packet,
char ReplyBuffer[] = "acknowledged";        // a string to send back

// An EthernetUDP instance to let us send and receive packets over UDP
EthernetUDP Udp;

// The current CAN message to be sent out
CANMessage message;

#define DEBUG 1 // set to 0 to avoid compiling print statements (will save space, don't need to print if running on rover)
int checkSum = 0; // used to check for errors in recieved message.
#define greenPin 8 // LED pins on Teensy
#define redPin 9
#define bluePin 10
#define WHEEL 0x01 // UDP IDs
#define ARM 0x02
#define SCIENCE 0x03
#define LED 0x02
#define LOWER_ARM 0x01 // CAN IDs
#define UPPER_ARM 0x02

int vertPos = 90; // position of gimbal
int horizPos = 90; 

Servo wheel0, wheel1, wheel2, wheel3, wheel4, wheel5, gimbalVert, gimbalHoriz;
Servo wheels[6] = {wheel0, wheel1, wheel2, wheel3, wheel4, wheel5};
// if some connections are reversed, wheel0.write(0) may not be the same direction as wheel1.write(0)
bool wheelReverse[6] = {true, false, false, true, false, true};
// PWM on mega is D2-D13. Using D8-13 for wheels, D7/D6 for gimbal, and D19/D20/D21 for LEDs

unsigned long timeOut = 0; // used to measure time between msgs. If we go a full second without new msgs, stop wheels so rover doesn't run away from us

// super awesome fun proportional wheel control variables
bool proportionalControl = true;
int targetSpeeds[6] = {126, 126, 126, 126, 126, 126}; // neutral
double currentSpeeds[6] = {126, 126, 126, 126, 126, 126};

unsigned long lastLoop = 0; // milli time of last loop
double deltaLoop = 0.0; // seconds since last loop

double Kp = 1.0; // proportional change between target and current

double error[6];

// define time in microseconds of width of pulse
const int PWM_LOW = 1000;
const int PWM_HIGH = 2000;
const int PWM_NEUTRAL = 1500; 

// clips a value so it stays in range [low, high]
double clip(double value, double low, double high) {
  if (value < low) value = low;
  if (value > high) value = high;
  return value;
}

void updateGimbal(int vertical, int horizontal) {
  // inputs are 0 for counter-clockwise, 1 for nothing, 2 for clockwise
   switch(vertical){
      case 2: 
        if(vertPos >= 180){
          vertPos=179;
        }
        vertPos += 1; 
        break;
      case 0:
        if(vertPos <= 0){
          vertPos=1;
        }
        vertPos -= 1; 
        break;
   }
    switch(horizontal) {
      case 2: 
        if(horizPos >= 180){
          horizPos=179;
        }
        horizPos += 1; 
        break;
      case 0:
        if(horizPos <= 0){
          horizPos=1;
        }
        horizPos -= 1; 
        break;
      }
  gimbalVert.write(vertPos);
  gimbalHoriz.write(horizPos);
}

//LED msg: [0x01, 0x02, red, green, blue]
//WHEEL msg: [0x01, 0x01, w1, w2, w3, w4, w5, w6, checkSum]
void updateWheels(char msg[], int msgSize) {
  checkSum = 0;
  #if DEBUG
    //Serial.println("msg recieved:");
    for(int i=0; i<msgSize; ++i) {
      Serial.print(uint8_t(msg[i]));
      Serial.print(", ");
    }
    Serial.println();
  #endif

  /**************LED MSG *****************/
  if (msg[1] == LED) {
    if(msgSize == 5) {
      if(uint8_t(msg[2]) > 0) {
        digitalWrite(redPin, HIGH);
      }
      else {
        digitalWrite(redPin, LOW);
      }
      if(uint8_t(msg[3]) > 0) {
        digitalWrite(greenPin, HIGH);
      }
      else {
        digitalWrite(greenPin, LOW);
      }
      if(uint8_t(msg[4]) > 0) {
        digitalWrite(bluePin, HIGH);
      }
      else {
        digitalWrite(bluePin, LOW);
      }
    } 
    else {
      #if DEBUG
        Serial.println("msg for LEDS was wrong length... ignoring this message.");
      #endif
    }
  }  
  /**************WHEEL MSG *****************/
  else if (msg[1] == WHEEL) {
    if(msgSize == 9) {
      for(int i=2; i<8; i++) { 
        checkSum += msg[i];
      }
      checkSum = checkSum & 0xff;
      #if DEBUG // commented because its hard to read other print statements, keep in case its needed
        // Serial.print("Calcuated checksum: ");
        // Serial.println(checkSum);
        // Serial.print("Received checksum: ");
        // Serial.println((int)msg[8]);
      #endif
      if(checkSum == uint8_t(msg[8])) {
        timeOut = millis(); // save time so we know how long it's been between this and next msg
        // set the appropriate target speeds
        for (int i = 0; i < 6; i++) {
          targetSpeeds[i] = (uint8_t)msg[i+2];
        }          
        //updateGimbal(uint8_t(msg[8]), uint8_t(msg[9])); // NOT IMPLEMENTED RIGHT NOW
      }
      else {
        #if DEBUG
          Serial.println("checkSum for WHEELS was incorrect... ignoring this message.");
        #endif
      }
    }
    else {
      #if DEBUG
        Serial.println("msg for WHEELS was wrong length... ignoring this message.");
      #endif
    }
  }
  else {
    #if DEBUG
      Serial.println("msg was for the wrong device... ignoring this message.");
    #endif
  }
}

void sendArmCAN(char msg[], int msgSize) {
  if(msgSize == 8) {
    for(int i=1; i<7; i++) { 
        checkSum += msg[i];
    }
    checkSum = checkSum & 0xff;
    if(checkSum == uint8_t(msg[7])) {
      message.id = LOWER_ARM; // ID for lower arm
      message.data = {msg[1], msg[2], msg[3]}; // base, bicep, forearm
      const bool ok = ACAN_T4::can1.tryToSend (message) ;
      if(ok) {
        #if DEBUG
          Serial.print("lower sent, ");
        #endif   
      }
      message.id = UPPER_ARM;
      message.data = {msg[4], msg[5], msg[6]}; // pitch, roll, claw
    }
    else {
      #if DEBUG
        Serial.println("checkSum for ARM was incorrect... ignoring this message.");
      #endif
    }
  }
  else {
    #if DEBUG
        Serial.println("checkSum for ARM was incorrect... ignoring this message.");
      #endif
  }
}

void sendScienceCAN(char msg[], int msgSize) {
  if(msgSize == 6) {
    for(int i=1; i<5; i++) { 
        checkSum += msg[i];
    }
    checkSum = checkSum & 0xff;
    if(checkSum == uint8_t(msg[5])) {
      message.id = SCIENCE; // ID for lower arm
      message.data = {msg[1], msg[2], msg[3], msg[4]}; // actuator, carousel, fan, microscope
      const bool ok = ACAN_T4::can1.tryToSend (message) ;
      if(ok) {
        #if DEBUG
          Serial.print("lower sent, ");
        #endif   
      }
      message.id = UPPER_ARM;
      message.data = {msg[4], msg[5], msg[6]}; // pitch, roll, claw
    }
    else {
      #if DEBUG
        Serial.println("checkSum for ARM was incorrect... ignoring this message.");
      #endif
    }
  }
  else {
    #if DEBUG
        Serial.println("checkSum for ARM was incorrect... ignoring this message.");
      #endif
  }
}

void setup() {
  // You can use Ethernet.init(pin) to configure the CS pin
  //Ethernet.init(10);  // Most Arduino shields <---
  //Ethernet.init(5);   // MKR ETH Shield
  //Ethernet.init(0);   // Teensy 2.0
  //Ethernet.init(20);  // Teensy++ 2.0
  //Ethernet.init(15);  // ESP8266 with Adafruit FeatherWing Ethernet
  //Ethernet.init(33);  // ESP32 with Adafruit FeatherWing Ethernet
  wheel0.attach(4);
  wheel1.attach(5);
  wheel2.attach(6);
  wheel3.attach(7);
  wheel4.attach(8);
  wheel5.attach(9);

  gimbalVert.attach(8);
  gimbalHoriz.attach(9);

  pinMode(redPin, OUTPUT); // red
  pinMode(greenPin, OUTPUT); // green
  pinMode(bluePin, OUTPUT); // blue

  // start the Ethernet
  Ethernet.begin(mac, ip);

  // Open serial communications and wait for port to open:
  #if DEBUG
    Serial.begin(9600);
    while (!Serial) {
      ; // wait for serial port to connect. Needed for native USB port only
    }
  #endif

  // Check for Ethernet hardware present
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    #if DEBUG
    Serial.println("Ethernet shield was not found.  Sorry, can't run without hardware. :(");
    #endif
    while (true) {
      delay(1); // do nothing, no point running without Ethernet hardware
    }
  }
  if (Ethernet.linkStatus() == LinkOFF) {
    #if DEBUG
    Serial.println("Ethernet cable is not connected.");
    #endif
  }
  // initialize the CAN settings and start
  ACAN_T4_Settings settings (100 * 1000) ; // 100 kbit/s - must agree on both ends of CAN
  const uint32_t errorCode = ACAN_T4::can3.begin (settings) ;

  if (0 == errorCode) {
    #if DEBUG
      Serial.println ("can3 ok") ;
    #endif
  }else{
    #if DEBUG
      Serial.print ("Error can3: 0x") ;
      Serial.println (errorCode, HEX) ;
    #endif
    while (1) {
      delay (100) ;
      #if DEBUG
        Serial.println ("Invalid setting") ;
      #endif
      digitalWrite (LED_BUILTIN, !digitalRead (LED_BUILTIN)) ;
    }
  }

  // start UDP
  Udp.begin(localPort);
}

void loop() {
  // if there's data available, read a packet
  int packetSize = Udp.parsePacket();
  if (packetSize) {
    #if DEBUG
      Serial.print("Received packet of size ");
      Serial.println(packetSize);
      Serial.print("From ");
      IPAddress remote = Udp.remoteIP();
      for (int i=0; i < 4; i++) {
        Serial.print(remote[i], DEC);
        if (i < 3) {
          Serial.print(".");
        }
      }
      Serial.print(", port ");
      Serial.println(Udp.remotePort());
    #endif
    // read the packet into packetBuffer
    Udp.read(packetBuffer, packetSize);
    if(packetBuffer[0] == WHEEL) { // means the UDP message was for the wheel system
      updateWheels(packetBuffer, packetSize)
    }
    else if(packetBuffer[0] == ARM) {
      sendArmCAN(packetBuffer, packetSize);
    }
    else if(packetBuffer[0] == SCIENCE) {
      sendScienceCAN(packetBuffer, packetSize);
    }
    else {
      #if DEBUG
        Serial.println("ID unrecognized");
      #endif
    }
    // send a reply to the IP address and port that sent us the packet we received (NEVER TRIED THIS)
    //Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
    //Udp.write(ReplyBuffer);
    //Udp.endPacket();
  }
  if ( millis() - timeOut >= 1000) // if the last good msg recieved was longer than 1 sec ago, stop wheels
  {
    timeOut = millis();
    for (int i = 0; i < 6; i++) {
      wheels[i].writeMicroseconds(PWM_NEUTRAL);
      currentSpeeds[i] = 126;
      targetSpeeds[i] = 126;
    }
    // wheel3.writeMicroseconds(1500);
    gimbalVert.write(90);
    gimbalHoriz.write(90);

    #if DEBUG
    Serial.println("Stopped motors");
    #endif
  } else {
    deltaLoop = (millis() - lastLoop)/1000.0;
    for (int i = 0; i < 6; i++) {
      // if using proportional control, look at the difference between the current and desired speed
      if (proportionalControl) {
        error[i] = targetSpeeds[i] - currentSpeeds[i];
        double output = error[i]*Kp*deltaLoop;
        currentSpeeds[i] += output; 
      } else {
        currentSpeeds[i] = targetSpeeds[i];
      }
      // if the target is to stop, set the current to that as well
      if (targetSpeeds[i] == 126) currentSpeeds[i] = 126;
      // also clip the current speed value just to be safe
      currentSpeeds[i] = clip(currentSpeeds[i], 0, 252);
      if (wheelReverse[i]) {
        wheels[i].writeMicroseconds((int)map(currentSpeeds[i], 252, 0, PWM_LOW, PWM_HIGH));        
      } else {
        wheels[i].writeMicroseconds((int)map(currentSpeeds[i], 0, 252, PWM_LOW, PWM_HIGH));
      }

      // just debugging stuff
      // if (i == 3) {
      //   // Serial.println(wheels[0].read());
      // //   Serial.print("ts: ");
      // //   Serial.print(targetSpeeds[i]);
      // //   Serial.print(", cs: ");
      //   Serial.println(currentSpeeds[i]);      
      // }

    }
    // remember milli time of the last loop
    lastLoop = millis();
  }
  delay(10);
}

