#include "CNC_Lathe.h"

//global vars

//Cosinus LookUp-Table for Quarter Circle in Q15 (max. 32767 !!!) (saved in Flash-Memory)
const int lookup_cosinus[91] PROGMEM = {32767, 32762, 32747, 32722, 32687, 32642, 32587, 32523, 32448, 32364, 32269, 32165, 32051, 31927, 31794, 31650, 31498, 31335, 31163, 30982, 30791, 30591, 30381, 30162, 29934, 29697, 29451, 29196, 28932, 28659, 28377, 28087, 27788, 27481, 27165, 26841, 26509, 26169, 25821, 25465, 25101, 24730, 24351, 23964, 23571, 23170, 22762, 22347, 21925, 21497, 21062, 20621, 20173, 19720, 19260, 18794, 18323, 17846, 17364, 16876, 16384, 15886, 15383, 14876, 14364, 13848, 13328, 12803, 12275, 11743, 11207, 10668, 10126, 9580, 9032, 8481, 7927, 7371, 6813, 6252, 5690, 5126, 4560, 3993, 3425, 2856, 2286, 1715, 1144, 572, 0};

//ERROR-Numbers
volatile byte ERROR_NO = 0; //actual ERROR-Numbers Bit-coded (bit2_SPINDLE|bit1_CNC_CODE|bit0_SPI)

//Machine State
volatile byte STATE1=0; //bit7_stepper|bit6_spindle_direction|bit5_spindle|bit4_inch|bit3_pause|bit2_manual|bit1_init|bit0_control_active
volatile byte STATE2=0; //STATE2_CNC_CODE_NEEDED_BIT | STATE2_TOOLCHANGER_RUNNING_BIT | STATE2_ZSTEPPER_RUNNING_BIT | STATE2_XSTEPPER_RUNNING_BIT | STATE2_COMMAND_TIME_BIT | STATE2_COMMAND_RUNNING_BIT
volatile int STATE_RPM=0;
volatile int STATE_X=0;
volatile int STATE_Z=0;
volatile int STATE_F=0;
volatile int STATE_H=0;
volatile byte STATE_T=0; //0 = uninitialized
volatile int STATE_N=0;

void watchdogSetup(void) {
  cli(); // disable all interrupts
  wdt_reset(); // reset the WDT timer
  /*
     WDTCSR conﬁguration:
     WDIE  = 1: Interrupt Enable
     WDE   = 1 :Reset Enable
     WDP3 = 0 :For 2000ms Time-out
     WDP2 = 1 :For 2000ms Time-out
     WDP1 = 1 :For 2000ms Time-out
     WDP0 = 1 :For 2000ms Time-out
  */
  // Enter Watchdog Conﬁguration mode:
  WDTCSR |= (1<<WDCE) | (1<<WDE); 
  // Set Watchdog settings:
  WDTCSR = (1<<WDIE) | (1<<WDE) | (0<<WDP3)  | (1<<WDP2) | (1<<WDP1)  | (1<<WDP0);
  sei();
}

void setup() {
  // put your setup code here, to run once:
    
  //PINs
  pinMode(PIN_CONTROL_ACTIVE, INPUT);
  pinMode(PIN_REVOLUTIONS_SYNC, INPUT_PULLUP);
  pinMode(PIN_REVOLUTIONS_COUNT, INPUT_PULLUP);
  pinMode(PIN_SPINDLE_ON_DETECT, INPUT);
  pinMode(PIN_OLD_CONTROL_STEPPER_X_OFF, INPUT);
  pinMode(PIN_OLD_CONTROL_STEPPER_X_A, INPUT);
  pinMode(PIN_OLD_CONTROL_STEPPER_X_B, INPUT);
  pinMode(PIN_OLD_CONTROL_STEPPER_Z_A, INPUT);
  pinMode(PIN_OLD_CONTROL_STEPPER_Z_B, INPUT);
  pinMode(PIN_SERVO_ENGINE, OUTPUT); //needed for Fast PWM
  #ifdef SPINDLEDRIVER_NEW
    pinMode(PIN_SPINDLEPWM_NIKO, OUTPUT); //needed for Fast PWM
    pinMode(PIN_SPINDLE_NEW, OUTPUT);
    pinMode(PIN_SPINDLE_CHARGERESISTOR_OFF, OUTPUT);
  #endif
  pinMode(PIN_DEBUG_INPUT_1, INPUT_PULLUP);
  pinMode(PIN_DEBUG_INPUT_2, INPUT_PULLUP);
  pinMode(PIN_DEBUG_INPUT_3, INPUT_PULLUP);
  pinMode(PIN_STEPPER_X_A, OUTPUT);
  pinMode(PIN_STEPPER_X_B, OUTPUT);
  pinMode(PIN_STEPPER_X_C, OUTPUT);
  pinMode(PIN_STEPPER_X_D, OUTPUT);
  pinMode(PIN_STEPPER_Z_A, OUTPUT);
  pinMode(PIN_STEPPER_Z_B, OUTPUT);
  pinMode(PIN_STEPPER_Z_C, OUTPUT);
  pinMode(PIN_STEPPER_Z_D, OUTPUT);
  #ifdef BOARDVERSION_1_25
    pinMode(PIN_TOOL_CHANGER_HOLD, OUTPUT);
  #else
    pinMode(PIN_TOOL_CHANGER_FIXING, OUTPUT);
  #endif
  pinMode(PIN_SPINDLE_ON, OUTPUT);
  pinMode(PIN_SPINDLE_DIRECTION, OUTPUT);
  #if !defined DEBUG_SERIAL_CODE_OFF && defined SPINDLEDRIVER_EXTRA_BOARD
    pinMode(PIN_USART1_RX, INPUT);
    pinMode(PIN_USART1_TX, OUTPUT);
  #endif
  pinMode(PIN_SPI_MISO, OUTPUT); 		//Arduino is SPI-Slave
  pinMode(PIN_SPI_MOSI, INPUT); 	//Arduino is SPI-Slave
  pinMode(PIN_SPI_SCK, INPUT); 	//Arduino is SPI-Slave
  pinMode(PIN_SPI_SS, INPUT); 	//Arduino is SPI-Slave
  #ifdef SERVO_LIB
    potiservo.attach(PIN_SERVO_ENGINE);   //Attach Servo-Pin
  #endif

  //set initial State
  STATE1 |= _BV(STATE1_MANUAL_BIT) | _BV(STATE1_PAUSE_BIT); //set = 1
  if(!get_control_active()) get_stepper_on_off(); //get initial state
  
  //Serial Communication
  #ifndef DEBUG_SERIAL_CODE_OFF
    //#error Serial compilation activated!
    //Serial.begin(115200); //for Debugging with Serial Monitor (115200 baud * 4 bit/baud = 460800 bit/s)
    Serial.begin(74880); //for Debugging with Serial Monitor (74880 baud * 4 bit/baud = 299520 bit/s)
  #endif

  #if !defined DEBUG_SERIAL_CODE_OFF && defined SPINDLEDRIVER_EXTRA_BOARD
    Serial1.begin(9600); //Nikos Platine
  #endif
  
  //SPI
  SPCR |= _BV(SPE);  // turn on SPI in slave mode
  init_msg_praeambel();
  create_machine_state_msg(); //initialize machine_state_msg before turning on interrupt
  #ifndef DEBUG_SPI_CODE_OFF
    //#error SPI compilation activated!
    SPI.attachInterrupt();  //turn on interrupt
  #endif

  //Measurement of Revolutions
  #ifndef DEBUG_RPM_CODE_OFF
    //#error RPM compilation activated!
    attachInterrupt(digitalPinToInterrupt(PIN_REVOLUTIONS_SYNC),get_revolutions_ISR,RISING);
  #endif

  //TIMER
  
  //Timer0
  //millis() and micros() functions use Timer0
  //millis() returns (unsigned long)
  //micros() returned value at 16 MHz is always a multiple of four microseconds in (unsigned long)
  //get_revolutions and get_feed with micros() in PIN triggerd ISR !!! micros reads out Timer0 even in an ISR, but overflow could be missed
  //Dwell?
  //Stepper-Timeout

  //Timer1
  //Toolchanger + set x_command_completed
  //+X-Stepper
  //command_running isr (conflict with stepper!!! => must be changed to another timer)
  
  //Timer2 
  //tone() function uses Timer2
    
  //Timer3
  //Z-Stepper output + set z_command_completed while in active mode and maybe observing Stepper in passive mode

  #ifdef SPINDLEDRIVER_NEW
    //Timer4
    //spindle PWM
    //set and start Timer4 (Clk = 16MHz/(Prescaler*(TOP+1)) = 16MHz/(1023+1) = 15,625 kHz)
    TCCR4B = 0b00001000; //connect no Input-Compare-PINs, WGM43=0, WGM42=1 for Fast PWM, 10-bit and Disbale Timer with Prescaler=0 while setting it up
    TCCR4A = 0b00001011; //connect OC4C-PIN (PIN 8) to Output Compare and WGM41=1, WGM40=1 for Fast PWM
    TCCR4C = 0; //no Force of Output Compare
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      OCR4C = 0; //OCR4C max. = 1023 *0,55338792 = 566 !!! Engine is only for 180V DC
      TCNT4 = 0; //set Start Value
    }
    //Prescaler 1 and Start Timer
    TCCR4B |= _BV(CS40); //set 1
  #endif
  
  //Timer5 Servo and spindle regulator
  set_Timer5();

  //Toolchanger
#ifdef BOARDVERSION_1_25
  digitalWrite(PIN_TOOL_CHANGER_HOLD, HIGH);
#endif

  //set interrupt enable
  sei();

  //read Last Steps
  //read_last_x_step();
  //read_last_z_step();

  /*
  //debug Watchdog
  #ifndef DEBUG_SERIAL_CODE_OFF
    Serial.print(F("Watchdog WDTCSR: "));
    Serial.println(WDTCSR, BIN);
    Serial.print(F("Watchdog MCUCSR: "));
    Serial.println(MCUSR, BIN);
  #endif
  */
  //watchdogSetup();
}

void loop() {
  // put your main code here, to run repeatedly:

  //SPI-Communication
  #ifndef DEBUG_SPI_CODE_OFF
    //#error SPI compilation activated!
    if (!byte_received) create_machine_state_msg(); //update machine_state_msg if no transfer is in progress
    spi_buffer_handling();
  #endif

  //CNC-Lathe State-Machine
  if (!command_time && !i_command_time && !i_tool && x_command_completed && z_command_completed) {
    STATE_F = 0; //maybe not needed
    if (wait_for_spindle_stop) {
      wait_for_spindle_stop=0;
    }
    else if (callback_spindle_direction_change) {
      callback_spindle_direction_change=0;
      spindle_direction(target_spindle_direction);
    }
    else if (wait_for_spindle_spindle_direction_relais) {
      wait_for_spindle_spindle_direction_relais=0;
    }
    else if (callback_spindle_start) {
      callback_spindle_start=0;
      spindle_on();
    }
    else {
      command_completed=1;
      #ifdef RPM_ERROR_TEST
        if ((STATE1>>STATE1_SPINDLE_BIT)&1) if(!test_for_spindle_rpm(target_revolutions, 100)) ERROR_NO |= _BV(ERROR_SPINDLE_BIT); //test for wrong rpm (not finished)
      #endif
      if (!pause && !ERROR_NO && !((STATE2>>STATE2_CNC_CODE_NEEDED_BIT)&1)) {
        STATE_N++;
        if (STATE_N<0 || STATE_N>CNC_CODE_NMAX) { //should be done before process_cnc_listing()
          //N_Offset = N_Offset + STATE_N; //should be done by Uploader
          STATE2 |= _BV(STATE2_CNC_CODE_NEEDED_BIT);
          //wait for new code-messages and reset of STATE2_CNC_CODE_NEEDED_BIT
        }
      }
    }
  }
  
  if (get_control_active()) { //with board V1.25 turn Spindle-Switch of Emco Control off, before avtivate or deactivate new control!!! Hotfix for Direction-Bug
    if (initialized) {
      if (pause) stepper_timeout();
    }
    if (command_completed) {
      if (!((STATE1>>STATE1_MANUAL_BIT)&1)) { //manual maybe not needed, instead use pause
        if (!pause && !ERROR_NO && !((STATE2>>STATE2_CNC_CODE_NEEDED_BIT)&1)) {
          if (process_cnc_listing()) { //error
            STATE1 |= _BV(STATE1_MANUAL_BIT) | _BV(STATE1_PAUSE_BIT);
            ERROR_NO |= _BV(ERROR_CNC_CODE_BIT);
          }
          else {
            #ifdef DEBUG_CNC_ON
              //programm_pause();
            #endif
          }
        }
      }
    }
    else reset_stepper_timeout=true;
  }
  //else intitialize(); //without sensors useless, Tool-Changer- and Origin-Init by SPI command (Origin not needed at the moment)
  else {
    observe_machine();
    if(!(millis()%100)) set_revolutions(get_SERVO_CONTROL_POTI()); //problematic with spi-communication, analogRead should be replaced by an adc-isr, because it waits 100us for adc-conversion
    //if(!(millis()%100)) intr_analogRead(APIN_SERVO_CONTROL_POTI); //also problematic with spi-communication, but why?
    //Debug
    #if !defined DEBUG_SERIAL_CODE_OFF && defined DEBUG_MSG_RPM_ON
      if(!(millis()%2000)) {
        //#error RPM debug-msg compilation activated!
        Serial.print(F("RPM-set-Value: "));
        Serial.println (target_revolutions);
      }
    #endif
    //set spindle-direction
  }

  #ifdef DEBUG_PROGRAM_FLOW_ON
    Serial.println("0");
  #endif
  
  //wdt_reset(); // reset the WDT timer
}

/*
ISR(WDT_vect) //Watchdog-ISR
{
  #ifndef DEBUG_SERIAL_CODE_OFF
    Serial.println("Watchdog Interrupt - Restarting if WDE in WDTCSR enabled");
  #endif
  // you can include any code here. With the reset disabled you could perform an action here every time
  // the watchdog times out...
}
*/
