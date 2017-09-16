/*
 * Copyright (c) 2017, Luc Yriarte
 * License: BSD <http://www.opensource.org/licenses/bsd-license.php>
 */

#include <Servo.h> 

/* **** **** **** **** **** ****
 * Constants
 * **** **** **** **** **** ****/

#define BPS_HOST 9600
#define COMMS_BUFFER_SIZE 1024
#define SERIAL_HOST_DELAY_MS 10000
#define SCAN_STEP_DELAY_MS 600
/* 
 * ultra sonic telemeter mesure
 */
#define INECHO 10
#define TRIGGER 11
#define ECHO_TIMEOUT 100000
#define ECHO2CM(x) (x/60)
#define MAX_CM 1000

/* 
 * servo
 */
#define SRV 12

/* 
 * stepper
 */
#define STEP1 9
#define STEP2 8
#define STEP3 7
#define STEP4 6
#define STEPPERMS 5

/* 
 * automaton states
 */
enum {
	START,
	ERROR,
	IN_CMD,
	IN_SERVO,
	IN_STEPPER,
	IN_SCAN_PARAMETERS,
	SENSOR_MESURE
};


/* **** **** **** **** **** ****
 * Global variables
 * **** **** **** **** **** ****/

/* 
 * servo
 */
Servo servo;

/* 
 * stepper
 */
byte steps8[] = {
  HIGH,  LOW,  LOW,  LOW,
  HIGH, HIGH,  LOW,  LOW,
   LOW, HIGH,  LOW,  LOW,
   LOW, HIGH, HIGH,  LOW,
   LOW,  LOW, HIGH,  LOW, 
   LOW,  LOW, HIGH, HIGH,
   LOW,  LOW,  LOW, HIGH,
  HIGH,  LOW,  LOW, HIGH,
};

/* 
 * automaton status
 */
int currentState;

/* 
 * serial comms buffer
 */
char commsBuffer[COMMS_BUFFER_SIZE];

/* **** **** **** **** **** ****
 * Functions
 * **** **** **** **** **** ****/

void setup() {
	pinMode(INECHO, INPUT);
	pinMode(TRIGGER, OUTPUT);
	pinMode(STEP1, OUTPUT);
	pinMode(STEP2, OUTPUT);
	pinMode(STEP3, OUTPUT);
	pinMode(STEP4, OUTPUT);
	servo.attach(SRV);
	Serial.begin(BPS_HOST);
	digitalWrite(TRIGGER, LOW);
	currentState = START;
}

/* 
 * servo command
 */
void servoCommand(int angle) {
	servo.write(angle);
}

/* 
 * stepper command
 */
void step8(int pin1, int pin2, int pin3, int pin4) {
	int i=0;
	while (i<32) {
		digitalWrite(pin1, steps8[i++]);
		digitalWrite(pin2, steps8[i++]);
		digitalWrite(pin3, steps8[i++]);
		digitalWrite(pin4, steps8[i++]);
		delay(STEPPERMS);
	}
} 

void stepperCommand(int steps) {
	while (steps) {
		if (steps > 0) {
			step8(STEP1,STEP2,STEP3,STEP4);
			steps--;
		}
		else {
			step8(STEP4,STEP3,STEP2,STEP1);
			steps++;
		}
	}
}

/* 
 * telemeter mesure
 */
int telemeterMesure() {
	unsigned long echoDuration;
	unsigned int mesureCm;
	digitalWrite(TRIGGER, HIGH);
	delayMicroseconds(10);
	digitalWrite(TRIGGER, LOW);
	echoDuration = pulseIn(INECHO, HIGH, ECHO_TIMEOUT);
	mesureCm = echoDuration ? ECHO2CM(echoDuration) : MAX_CM;
	return mesureCm;
}

/* 
 * telemeter scan
 */
int telemeterscan(int stepperStep, int stepperN, int servoStep, int servoN, int servoFrom) {
	int stepperInc, servoInc;
	Serial.println();
	for (stepperInc=0; stepperInc<stepperN; stepperInc++) {
		Serial.print(stepperInc * stepperStep);
		Serial.print(" / ");
		for (servoInc=0; servoInc<servoN; servoInc++) {
			servoCommand(servoFrom + (servoInc * servoStep));
			delay(SCAN_STEP_DELAY_MS);
			Serial.print(servoFrom + (servoInc * servoStep));
			Serial.print(":");
			Serial.print(telemeterMesure());
			Serial.print(" ");
		}
		Serial.println();
		stepperCommand(stepperStep);
	}
}

/* 
 * userIO
 */
char * userInput(char * message) {
	unsigned long timeLoopStart,timeLoop;
	int nread = 0;
	if (message)
		Serial.print(message);
	timeLoopStart = timeLoop = millis();
	// Serial timeout triggers on full commsBuffer, just wait for first command
	while (nread == 0 && timeLoop - timeLoopStart < SERIAL_HOST_DELAY_MS) {
		// drop older contents on buffer overflow
		while ((nread = Serial.readBytes(commsBuffer, COMMS_BUFFER_SIZE)) == COMMS_BUFFER_SIZE);
		timeLoop = millis();
	}		
	commsBuffer[nread] = 0;
	return nread == 0 ? NULL : commsBuffer;
}

/* 
 * automaton
 */
int stateTransition(int currentState) {
	int newState = ERROR;
	char * input = NULL;
	int value = 0;
	switch (currentState) {
		case START:
			newState = IN_CMD;
			break;

		case ERROR:
			Serial.println("ERROR");
			newState = START;
			break;

		case IN_CMD:
			if (!(input = userInput("CMD: "))) {
				newState = IN_CMD;
				break;
			}
			if (!strcmp(input,"SERVO"))
				newState = IN_SERVO;
			else if (!strcmp(input,"STEPPER"))
				newState = IN_STEPPER;
			else if (!strcmp(input,"MESURE"))
				newState = SENSOR_MESURE;
			else if (!strcmp(input,"SCAN"))
				newState = IN_SCAN_PARAMETERS;
			break;

		case IN_SERVO:
			if (!(input = userInput("ANGLE: ")))
				while (!(input = userInput(NULL)));
			value = atoi(input);
			servoCommand(value);
			newState = IN_CMD;
			break;

		case IN_STEPPER:
			if (!(input = userInput("STEPS: ")))
				while (!(input = userInput(NULL)));
			value = atoi(input);
			stepperCommand(value);
			newState = IN_CMD;
			break;

		case IN_SCAN_PARAMETERS:
			int stSt, stNb, srSt, srNb, srFr;
			if (!(input = userInput("STEPPER STEP: ")))
				while (!(input = userInput(NULL)));
			stSt = atoi(input);
			if (!(input = userInput("STEPPER NBR: ")))
				while (!(input = userInput(NULL)));
			stNb = atoi(input);
			if (!(input = userInput("SERVO STEP: ")))
				while (!(input = userInput(NULL)));
			srSt = atoi(input);
			if (!(input = userInput("SERVO NBR: ")))
				while (!(input = userInput(NULL)));
			srNb = atoi(input);
			if (!(input = userInput("SERVO FROM: ")))
				while (!(input = userInput(NULL)));
			srFr = atoi(input);
			telemeterscan(stSt, stNb, srSt, srNb, srFr);
			newState = IN_CMD;
			break;

		case SENSOR_MESURE:
			value = telemeterMesure();
			Serial.println(value);
			newState = IN_CMD;
			break;

		default:
			newState = ERROR;
			break;
	}
	return newState;
}

void loop() {
//	telemeterscan(64, 8, 30, 4, 30);
	currentState = stateTransition(currentState);
}
