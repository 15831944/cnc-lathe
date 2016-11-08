#include "Motion_Control.h"

boolean absolute=0, feed_modus=0;
volatile byte interpolationmode=0, i_command_time=0;
volatile int command_time=0;


void set_xz_coordinates(int x_origin, int z_origin) {
  STATE_X -= x_origin;
  STATE_Z -= z_origin;
}

int get_inc_X(int abs_X) { //get incremental x-Coordinate
  return abs_X-STATE_X;
}

int get_inc_Z(int abs_Z) { //get incremental z-Coordinate
  return abs_Z-STATE_Z;
}

int get_Tool_X(int TOOL_X) { //get Tool x-Coordinate
  return TOOL_X-STATE_X;
}

int get_Tool_Z(int Tool_Z) { //get Tool z-Coordinate
  return Tool_Z-STATE_Z;
}

void set_xz_move(int X, int Z, int feed, byte local_interpolationmode) {
  //int x_steps=0; //has to be global for ISR
  //int z_steps=0; //has to be global for ISR
  //int x_feed=0; //has to be global for ISR
  //int z_feed=0; //has to be global for ISR
  //int command_time=0;
  command_completed=0;
  
  if (feed_modus==FEED_IN_MM_PER_REVOLUTION) {
    STATE_F =  get_xz_feed_related_to_revolutions(feed);
  } else STATE_F = feed;
  
  interpolationmode=local_interpolationmode;

  //turn stepper on with last step
  if (!((STATE>>STATE_STEPPER_BIT)&1)) stepper_on();
  
  //get incremental coordinates
  if (absolute){
    X=get_inc_X(X);
    Z=get_inc_Z(Z);
  }

  //calculate needed steps
  x_steps = X*STEPS_PER_MM; //not finished, maybe overflow
  z_steps = Z*STEPS_PER_MM; //not finished, maybe overflow

  clk_feed = (long)STATE_F * 60 * STEPS_PER_MM /1024; //clk_feed in 1/1024s (Overflow possible?)

  //Prepare Timer 1 for X-Stepper 
  TCCR1B = 0b00011000; //connect no Input-Compare-PINs, WGM13=1, WGM12=1 for Fast PWM and Disbale Timer with Prescaler=0 while setting it up
  TCCR1A = 0b00000011; //connect no Output-Compare-PINs and WGM11=1, WGM10=1 for Fast PWM
  TCCR1C = 0; //no Force of Output Compare
  
  //Prepare Timer 3 for Z-Stepper
  TCCR3B = 0b00011000; //connect no Input-Compare-PINs, WGM33=1, WGM32=1 for Fast PWM and Disbale Timer with Prescaler=0 while setting it up
  TCCR3A = 0b00000011; //connect no Output-Compare-PINs and WGM31=1, WGM30=1 for Fast PWM
  TCCR3C = 0; //no Force of Output Compare

  if (interpolationmode==INTERPOLATION_LINEAR) {
    if (Z==0) {
      x_feed=STATE_F;
      z_feed=0;
    } else {
      x_feed=(long)X*STATE_F/((long)Z+(long)X);
      if (x_feed==0) x_feed=1; //Minimum needed
    }
    if (X==0) {
      x_feed=0;
      z_feed=STATE_F;
    } else {
      z_feed=(long)Z*STATE_F/((long)Z+(long)X);
      if (z_feed==0) z_feed=1; //Minimum needed
    }

    clk_xfeed = (long)x_feed * 60 * STEPS_PER_MM / 1024; //clk_xfeed in 1/1024s (Overflow possible?)
    clk_zfeed = (long)z_feed * 60 * STEPS_PER_MM / 1024; //clk_xfeed in 1/1024s (Overflow possible?)

    //set Timer-Compare-Values
    if (X) {
      OCR1A = (15625L/clk_xfeed)-1; //OCR1A = (16MHz/(Prescaler*F_OCF1A))-1 = (16MHz/(1024*clk_xfeed))-1 = (15625Hz/clk_xfeed)-1
    }
    if (Z) { 
      OCR3A = (15625L/clk_zfeed)-1; //OCR3A = (16MHz/(Prescaler*F_OCF3A))-1 = (16MHz/(1024*clk_zfeed))-1 = (15625Hz/clk_zfeed)-1
    }
  }

  else if (interpolationmode==RAPID_LINEAR_MOVEMENT) {
    //set Timer-Compare-Values
    if (X) {
      OCR1A = 7514; //OCR1A = (16MHz/(Prescaler*F_OCF1A))-1 = (16MHz/(1024*clk_xfeed))-1 = (15625Hz*60/499s)-1
    }
    if (Z) { 
      OCR3A = 7514; //OCR3A = (16MHz/(Prescaler*F_OCF3A))-1 = (16MHz/(1024*clk_zfeed))-1 = (15625Hz*60/499s)-1
    }
  }
  
  else { //Circular Interpolation with different speed settings for x- and z-stepper
    //Steps have to be seperated in max. 90 sections of same moving average feed.
    //For each of x_steps and z_steps an average phi of the section has to be calculated.
    //Maybe an calculation of the next phi with a modified Bresenham-Algorithm could improve it.
    
    //next X- and Z-Step moving average feed
    phi_x = (((long)(x_step))*90+45)/x_steps;
    phi_z = (((long)(z_step))*90+45)/z_steps;
    
    if (interpolationmode==INTERPOLATION_CIRCULAR_CLOCKWISE) {
      //calculation of next x- and z-clk (Direction)
      if (z_steps < 0) {
        if (x_steps < 0) {
          clk_xfeed = (clk_feed * lookup_cosinus[90-phi_x])>>15;
          clk_zfeed = (clk_feed * lookup_cosinus[phi_z])>>15;
        }
        else {
          clk_xfeed = (clk_feed * lookup_cosinus[phi_x])>>15;
          clk_zfeed = (clk_feed * lookup_cosinus[90-phi_z])>>15;
        }
      }
      else {
        if (x_steps < 0) {
          clk_xfeed = (clk_feed * lookup_cosinus[phi_x])>>15;
          clk_zfeed = (clk_feed * lookup_cosinus[90-phi_z])>>15;
        }
        else {
          clk_xfeed = (clk_feed * lookup_cosinus[90-phi_x])>>15;
          clk_zfeed = (clk_feed * lookup_cosinus[phi_z])>>15;
        }
      }
    }
    else if (interpolationmode==INTERPOLATION_CIRCULAR_COUNTERCLOCKWISE) {
      //calculation of next x- and z-clk (Direction)
      if (z_steps < 0) {
        if (x_steps < 0) {
          clk_xfeed = (clk_feed * lookup_cosinus[phi_x])>>15;
          clk_zfeed = (clk_feed * lookup_cosinus[90-phi_z])>>15;
        }
        else {
          clk_xfeed = (clk_feed * lookup_cosinus[90-phi_x])>>15;
          clk_zfeed = (clk_feed * lookup_cosinus[phi_z])>>15;
        }
      }
      else {
        if (x_steps < 0) {
          clk_xfeed = (clk_feed * lookup_cosinus[90-phi_x])>>15;
          clk_zfeed = (clk_feed * lookup_cosinus[phi_z])>>15;
        }
        else {
          clk_xfeed = (clk_feed * lookup_cosinus[phi_x])>>15;
          clk_zfeed = (clk_feed * lookup_cosinus[90-phi_z])>>15;
        }
      }
    }
    
    //set Timer-Compare-Values
    //every step has to be executed, feed can't be zero
    if (clk_xfeed) { //clock not zero
      OCR1A = (15625L/clk_xfeed)-1; //OCR1A = (16MHz/(Prescaler*F_OCF1A))-1 = (16MHz/(1024*clk_xfeed))-1 = (15625Hz/clk_xfeed)-1
    } else OCR1A = 15624L;
    if (clk_zfeed) { //clock not zero
      OCR3A = (15625L/clk_zfeed)-1; //OCR3A = (16MHz/(Prescaler*F_OCF3A))-1 = (16MHz/(1024*clk_zfeed))-1 = (15625Hz/clk_zfeed)-1
    } else OCR3A = 15624L;   
  }

  //start Timer
  if (X) {
    x_command_completed=0;      
    TCNT1 = 0; //set Start Value
    //Output Compare A Match Interrupt Enable
    TIMSK1 |= _BV(OCIE1A); //set 1
    //Prescaler 1024 and Start Timer
    TCCR1B |= _BV(CS12) | _BV(CS10); //set 1
  }
  if (Z) {
    z_command_completed=0;
    TCNT3 = 0; //set Start Value
    //Output Compare A Match Interrupt Enable
    TIMSK3 |= _BV(OCIE3A); //set 1
    //Prescaler 1024 and Start Timer
    TCCR3B |= _BV(CS32) | _BV(CS30); //set 1
  }
}

void get_xz_coordinates() { //calculate Coordinates
  
}

int get_xz_feed() {
	int feed=0; //Stub
	return feed;
}

int get_xz_feed_related_to_revolutions(int feed_per_revolution) { // for G95 - Feed in mm/rev.
  int feed = feed_per_revolution*STATE_RPM;
  return feed;
}


//should be replaced by command_completed=1; in Stepper and Toolchanger ISR
//still needed for G04

void command_running(int local_command_time) { //command_time in 1/100s
  command_completed=0;
  
  //handling durations over 4s
  i_command_time = local_command_time/400;
  command_time = local_command_time%400;

  //set and start Timer1 for command_time
  TCCR1B = 0b00011000; //connect no Input-Compare-PINs, WGM13=1, WGM12=1 for Fast PWM and Disbale Timer with Prescaler=0 while setting it up
  TCCR1A = 0b00000011; //connect no Output-Compare-PINs and WGM11=1, WGM10=1 for Fast PWM
  TCCR1C = 0; //no Force of Output Compare
    
  if (i_command_time) {
    OCR1A = 62499; //OCR1A = (16MHz/(Prescaler*F_OCF1A))-1 = (16MHz*command_time/(1024*100))-1 = (15625Hz*400s/100)-1
  }
  else {
    OCR1A = (15625L*command_time/100)-1; //OCR1A = (16MHz/(Prescaler*F_OCF1A))-1 = (16MHz*command_time/(1024*100))-1 = (15625Hz*command_time/100)-1
  }
    TCNT1 = 0; //set Start Value
    //Output Compare A Match Interrupt Enable
    TIMSK1 |= _BV(OCIE1A); //set 1
    //Prescaler 1024 and Start Timer
    TCCR1B |= _BV(CS10) | _BV(CS12); //set 1
}

