#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
#include "MS5837.h"
#include "Servo.h"
#include "Sodaq_LSM303AGR.h"

#if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE // copied from mr jrowberg, guess it only imports the wire library if it needs it
#include "Wire.h"
#endif

//#define USE_OLD_PINS //old board
#define USE_NEW_PINS //board revision 1

#define USE_MAGNET_JOYSTICK
//#define ENABLE_AUTO_PUMPS

//#define DEBUG

#define arraylength(x) (sizeof(x) / sizeof((x)[0]))

#ifdef USE_OLD_PINS
#define INTERRUPT_PIN 2
#define UP_BUTTON_PIN 4
#define DOWN_BUTTON_PIN A1
#define MODE_BUTTON_PIN A0
#define EXTRA_BUTTON_1_PIN 13
#define DEPTH_LED_PIN 0
#define ACTUATOR_A_PIN 8
#define ACTUATOR_B_PIN 7
#define PUMP_A_PIN 11
#define PUMP_B_PIN 10
#define JOYSTICK_X_PIN A2
#define JOYSTICK_Y_PIN A3
#define SERVO_1_PIN 9
#define SERVO_2_PIN 6
#define SERVO_3_PIN 5
#define SERVO_4_PIN 3
#endif

#ifdef USE_NEW_PINS
#define UP_BUTTON_PIN A0
#define DOWN_BUTTON_PIN 13
#define MODE_BUTTON_PIN 8
#define EXTRA_BUTTON_1_PIN 7
#define DEPTH_LED_PIN 12
#define ACTUATOR_A_PIN 13
#define ACTUATOR_B_PIN A0
#define PUMP_A_PIN 2
#define PUMP_B_PIN 4
#define JOYSTICK_X_PIN A2
#define JOYSTICK_Y_PIN A3
#define SERVO_1_PIN 9
#define SERVO_2_PIN 6
#define SERVO_3_PIN 5
#define SERVO_4_PIN 3
#endif

Servo servos[4];

#ifdef USE_MAGNET_JOYSTICK
Sodaq_LSM303AGR magnetometer;
#endif

MPU6050 gyroscope;

MS5837 depthSensor;

uint8_t mpuIntStatus;
uint16_t packetSize;
float ypr[3];

volatile bool mpuInterrupt = false;

void dmpDataReady() {
    mpuInterrupt = true;
    //Serial.println("dmp data ready called");
}

void setPinModes() {

    //set the pins to be either an input, or output
    pinMode(UP_BUTTON_PIN, INPUT);
    pinMode(DOWN_BUTTON_PIN, INPUT);
    pinMode(MODE_BUTTON_PIN, INPUT);
    pinMode(EXTRA_BUTTON_1_PIN, INPUT);
    pinMode(DEPTH_LED_PIN, OUTPUT);
    pinMode(PUMP_A_PIN, OUTPUT);
    pinMode(PUMP_B_PIN, OUTPUT);
    pinMode(ACTUATOR_A_PIN, OUTPUT);
    pinMode(ACTUATOR_B_PIN, OUTPUT);
   // pinMode(INTERRUPT_PIN, INPUT); //TODO: It may need to be put after mpu initialization to work
    pinMode(JOYSTICK_X_PIN, INPUT);
    pinMode(JOYSTICK_Y_PIN, INPUT);

    //set the servos to recieve pin input
    servos[0].attach(SERVO_1_PIN);
    servos[1].attach(SERVO_2_PIN);
    servos[2].attach(SERVO_3_PIN);
    servos[3].attach(SERVO_4_PIN);
}

void setupGyro() {

    bool dmpReady = false;
    uint8_t devStatus;

    // join I2C bus (I2Cdev library doesn't do this automatically)
    #if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
        Wire.begin();
        Wire.setClock(400000); // 400kHz I2C clock. Comment this line if having compilation difficulties
    #elif I2CDEV_IMPLEMENTATION == I2CDEV_BUILTIN_FASTWIRE
        Fastwire::setup(400, true);
    #endif

    //TODO: set baud rate

    gyroscope.initialize();

    //TODO: have a timeout and error message if initilization fails
    if (!depthSensor.init()) {
        delay(1000);
    }

    // depthSensor.setModel(MS5837::MS5837_30BA); // set depth sensor model to the MS5837, 30 bar
    depthSensor.setFluidDensity(997); // set fluid density to 997 kilograms per meter cubed (freshwater)

    //load and configure the DMP
    devStatus = gyroscope.dmpInitialize();

    //TODO: see if these offsets need to be changed for maximum accuracy
    gyroscope.setXGyroOffset(220);
    gyroscope.setYGyroOffset(76);
    gyroscope.setZGyroOffset(-85);
    gyroscope.setZAccelOffset(1788);

    //TODO: have error message if initialization fails
    if (devStatus == 0) {

        //TODO: Might need gyro calibration methods if results are off

        gyroscope.setDMPEnabled(true);

        //enable Arduino interrupt detection
        attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), dmpDataReady, RISING);
        mpuIntStatus = gyroscope.getIntStatus();

        dmpReady = true; //allows the main loop method to run

        packetSize = gyroscope.dmpGetFIFOPacketSize();
    }
}

void setServo(Servo servo, float degrees) {

    float microseconds = degrees * 9.9 + 870;

    servo.writeMicroseconds(microseconds);
}

void processGyroData() {

    uint16_t fifoCount;
    uint8_t fifoBuffer[64];

    Quaternion q;
    VectorFloat gravity;
 
    //TODO: create error message if setup fails (dmpReady)

       if(gyroscope.dmpGetCurrentFIFOPacket(fifoBuffer)) {
            
          // display Euler angles in degrees
          gyroscope.dmpGetQuaternion(&q, fifoBuffer);
          gyroscope.dmpGetGravity(&gravity, &q);
          gyroscope.dmpGetYawPitchRoll(ypr, &q, &gravity);
        }


}

#ifdef USE_MAGNET_JOYSTICK
void setupMagnetometer() {
  magnetometer.rebootMagnetometer();
  delay(1000);
  magnetometer.enableMagnetometer(Sodaq_LSM303AGR::MagHighResMode, Sodaq_LSM303AGR::Hz100, Sodaq_LSM303AGR::Continuous);
  uint8_t axes = Sodaq_LSM303AGR::MagX;
  magnetometer.enableMagnetometerInterrupt(axes, -400);
}
#endif

void processSteering() { // read the joystick, then set the servo angles

    float servoAngles[4];


    
#ifdef USE_MAGNET_JOYSTICK
    float x = magnetometer.getMagX() / 3000;
    float y = magnetometer.getMagY() / 3000;
    #ifdef DEBUG
    Serial.print(x);
    Serial.print(" ");
    Serial.println(y);
    #endif

#else
    float x = (analogRead(JOYSTICK_X_PIN) - 512) / 512.0f;
    float y = (analogRead(JOYSTICK_Y_PIN) - 512) / 512.0f;
#endif

    int deadMin = 85;
    int deadMax = 95;
    int minAngle = 45; //minimum angle required for x-pattern
    int maxAngle = 135; //maximum angle required for x-pattern

    servoAngles[0] = (atan(x - y) * -180 / PI) + 90;
    servoAngles[1] = (atan(x + y) * -180 / PI) + 90;
    servoAngles[2] = (atan(y - x) * -180 / PI) + 90;
    servoAngles[3] = (atan(x + y) * 180 / PI) + 90;

    for (int i = 0; i < arraylength(servos); i++ ) {

        if (servoAngles[i] > maxAngle){
            servoAngles[i] = maxAngle;
        } else if (servoAngles[i] < minAngle) {
            servoAngles[i] = minAngle;
        }

        if (servoAngles[i] > deadMin && servoAngles[i] < deadMax) servoAngles[i] = 90;

        servoAngles[i] = round(servoAngles[i]);

#ifdef DEBUG
        Serial.print("Setting servo ");
        Serial.print(i);
        Serial.print(" to ");
        Serial.println(servoAngles[i]);
#endif
        setServo(servos[i], servoAngles[i]);
    }
}

void processDepthInput() {
    depthSensor.read();
//    Serial.print(sensor.pressure());
  //  Serial.print(sensor.depth());
}

void processButtonInput() {


    // TODO: see if we can free a spot for a depth set button (to maintain a depth automatically)

    //automatic tilt bouyancy system

    if (digitalRead(EXTRA_BUTTON_1_PIN) == LOW) { // auto bouyancy is on
#ifdef ENABLE_AUTO_PUMPS
        float roll = (ypr[2] * 180 / M_PI);

        if ((roll) < 2.5 and (roll) > -2.5) {

            digitalWrite(PUMP_A_PIN, LOW);
            digitalWrite(PUMP_B_PIN, LOW);
        }

        if ((roll) > 2.5) {

            digitalWrite(PUMP_A_PIN, HIGH);
            digitalWrite(PUMP_B_PIN, LOW);
        }

        if ((roll) < -2.5) {

            digitalWrite(PUMP_A_PIN, LOW);
            digitalWrite(PUMP_B_PIN, HIGH);
        }
#endif

    } else { // auto bouyancy is off

        if (digitalRead(MODE_BUTTON_PIN) == LOW) { // mode button is switched to manual pump control

            if (UP_BUTTON_PIN == HIGH) {

                digitalWrite(PUMP_A_PIN, HIGH);
                digitalWrite(PUMP_B_PIN, LOW);
            }

            if (DOWN_BUTTON_PIN == HIGH) {

                digitalWrite(PUMP_A_PIN, LOW);
                digitalWrite(PUMP_B_PIN, HIGH);
            }
        }

        if (digitalRead(MODE_BUTTON_PIN) == HIGH) { // mode button is switched to manual actuator control

            if (UP_BUTTON_PIN == HIGH) {

                digitalWrite(ACTUATOR_A_PIN, LOW); 
                digitalWrite(ACTUATOR_B_PIN, HIGH);
            }

            if (DOWN_BUTTON_PIN == HIGH) {

                digitalWrite(ACTUATOR_A_PIN, HIGH;
                digitalWrite(ACTUATOR_B_PIN, LOW);
            }
        }
    }
}

void setup() {
  Serial.begin(9600);

    setPinModes();

   setupGyro();
#ifdef USE_MAGNET_JOYSTICK
   setupMagnetometer();
#endif
   
}

void loop() {

    processGyroData();

    processSteering();

    processDepthInput();

    processButtonInput();
}
