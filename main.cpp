#include "mbed.h"
#include "rtos.h"
#include <string>
#include "ctype.h"
//Photointerrupter input pins
#define I1pin D2
#define I2pin D11
#define I3pin D12

//Incremental encoder input pins
#define CHA   D7
#define CHB   D8  

//Motor Drive output pins   //Mask in output byte
#define L1Lpin D4           //0x01
#define L1Hpin D5           //0x02
#define L2Lpin D3           //0x04
#define L2Hpin D6           //0x08
#define L3Lpin D9           //0x10
#define L3Hpin D10          //0x20
#define check  D1
//-----------------Define all the variables and constant------------------
volatile float duty_cycle = 1.0;
int8_t intState = 0;
int8_t orState = 0; 
volatile float R_now = 0;
volatile int counter=0;

const int8_t driveTable[] = {0x12,0x18,0x09,0x21,0x24,0x06,0x00,0x00};

//Mapping from interrupter inputs to sequential rotor states. 0x00 and 0x07 are not valid
const int8_t stateMap[] = {0x07,0x05,0x03,0x04,0x01,0x00,0x02,0x07};  
//const int8_t stateMap[] = {0x07,0x01,0x03,0x02,0x05,0x00,0x04,0x07}; //Alternative if phase order of input or drive is reversed

//Phase lead to make motor spin
int8_t lead = -2;  //2 for forwards, -2 for backwards

//Status LED
DigitalOut led1(LED1);
DigitalOut checking(check); 
//------------------------Photointerrupter inputs----------------------------
InterruptIn I1(I1pin);
InterruptIn I2(I2pin);
InterruptIn I3(I3pin);

//-----------------------Define the relative position indicator CHA&CHB-------
InterruptIn precise1(CHA);

//-------------------------Motor Transistors Pins----------------------------
//Motor Drive outputs
PwmOut L1L(L1Lpin); //L1L is PWM
DigitalOut L1H(L1Hpin);
PwmOut L2L(L2Lpin); //L2L is PWM
DigitalOut L2H(L2Hpin);
PwmOut L3L(L3Lpin); //L3L is PWM
DigitalOut L3H(L3Hpin);

volatile float mperiod=100;
//---------------------Set a given drive state-----------------------
void motorOut(int8_t driveState){
    
    //Lookup the output byte from the drive state.
    int8_t driveOut = driveTable[driveState & 0x07];
    L1L.period_us(mperiod);
    L2L.period_us(mperiod);
    L3L.period_us(mperiod);
    //Turn off first
    if (~driveOut & 0x01) L1L = 0;//.write(0.00f);//driveoutç¬¬0ä½
    if (~driveOut & 0x02) L1H = 1;         //driveoutç¬¬1ä½
    if (~driveOut & 0x04) L2L = 0;//.write(0.00f);//driveoutç¬¬2ä½
    if (~driveOut & 0x08) L2H = 1;         //driveoutç¬¬3ä½
    if (~driveOut & 0x10) L3L = 0;//.write(0.00f);//driveoutç¬¬4ä½
    if (~driveOut & 0x20) L3H = 1;         //driveoutç¬¬5ä½
    
    //Then turn on
    if (driveOut & 0x01) L1L.write(duty_cycle);
    if (driveOut & 0x02) L1H = 0;
    if (driveOut & 0x04) L2L.write(duty_cycle);
    if (driveOut & 0x08) L2H = 0;
    if (driveOut & 0x10) L3L.write(duty_cycle);
    if (driveOut & 0x20) L3H = 0;
    }
    
//-----------------Convert photointerrupter inputs to a rotor state-----------------
inline int8_t readRotorState(){
    return stateMap[I1 + 2*I2 + 4*I3];
    }

//----------------------Basic synchronisation routine--------------------------------    
int8_t motorHome() {
    //Put the motor in drive state 0 and wait for it to stabilise
    motorOut(0);
    wait(2.0);
    
    //Get the rotor state
    return readRotorState(); 
}

Timer t,T;
volatile float  interval = 0; 
volatile float  velocity = 0;  
void update_motorstate();
volatile float V_last = 0;
volatile float integral = 0;
Thread thread;
float Rs=0x7f800000;//if R command not given, default target revoultion is the largest float data type can accomodate
float Vs=5.0f;//if V command not given, default velocity target is set to 5.0 rev/s
float V_target =0; 
float R_target=0;
Serial pc(SERIAL_TX, SERIAL_RX);//set up serial communication
float k1;
int duration[8];
char Ts[16];
string notes[8] = {"  ","  ","  ","  ","  ","  ","  ","  "};

void melody_command();
void control_command(){
    if(pc.scanf("T%16s", Ts)==1){            
            melody_command();
    }   
    pc.scanf("R%7f", &Rs);
    pc.scanf("V%7f", &Vs);
    if(Rs==0){
        pc.scanf("R%7f", &Rs);
    }
    pc.printf("Entered R: %f\n\r", Rs);
    pc.printf("Entered V: %f\n\r", Vs);
    if(Rs<0){
        //backward rotation
        lead=2;
        Rs=-Rs;
    }
    
    V_target=Vs;
    R_target=Rs*117.0f; 
    k1=-0.35f+0.098f*Vs;
    //control coeficient as a function of the target velocity
}

float find_period(char n1, char n2);
void play_melody(int S, float P);
void melody_command(){
    printf("\nEntered T: %s\n\r", Ts);
        string str = Ts;
        int tc = 0;
        int nc = 0;
        for(int i=0; i<int(str.length()); i++){
            if (isdigit(str[i])){
                duration[tc] = int(str[i])-48; //ASCII minus 48
                tc++;
            }
            else if((int(str[i])>=65)&&(int(str[i])<=71)){ //A-G, ASCII range = [65,71]
                notes[0][nc] = str[i];
                notes[1][nc] = ' ';
                nc++;
            }
            else if((int(str[i])==35)||(int(str[i])==94)){
                if(notes[0][nc-1]!=' '){
                    notes[1][nc-1] = str[i];
                }
            }
            else{
                nc++;
            }
        }
        while(tc<8){
            duration[tc] = 0;
            notes[0][nc] = ' ';
            notes[1][nc] = ' ';
            tc++;
            nc++;
        }
        pc.printf("[");
        for(int i=0; i<8; i++){
            pc.printf("%c%c ", notes[0][i], notes[1][i]);
        }
        pc.printf("]\n[");
        for(int i=0; i<8; i++){
            pc.printf("%d  ", duration[i]);
        }
        pc.printf("]\n\r");
        duty_cycle=0.5;
        pc.printf("playing\n\r");
        float m_period;
        while(1){
            for(int i=0;i<8;i++){
                pc.printf("Note: %c%c\n\r",notes[0][i],notes[1][i]);
                pc.printf("Seconds: %d\n\r",duration[i]);
                m_period=find_period(notes[0][i],notes[1][i]);
                pc.printf("%f\n\r", m_period);
                play_melody(duration[i],m_period);
            }
        }
}
float find_period(char n1, char n2){
    float period;
    pc.printf("notes  %c%c\n\r", n1, n2);
    
    if((n1=='A')&&(n2=='^')) period=2407.89791;
    else if((n1=='A')&&(n2==' ')) period=2272.72727;
    else if(((n1=='A')&&(n2=='#'))||((n1=='B')&&(n2=='^')))period=2145.1862;
    else if(((n1=='B')&&(n2==' '))||((n1=='B')&&(n2=='#')))period=2024.78335;
    else if(((n1=='C')&&(n2=='^'))||((n1=='C')&&(n2==' ')))period=3822.19164;
    else if(((n1=='C')&&(n2=='#'))||((n1=='D')&&(n2=='^')))period=3607.76391;
    else if((n1=='A')&&(n2=='^'))period=3405.29864;
    else if(((n1=='D')&&(n2=='#'))||((n1=='E')&&(n2=='^')))period=3214.09057;
    else if(((n1=='E')&&(n2==' '))||((n1=='E')&&(n2=='#')))period=3033.70446;
    else if(((n1=='F')&&(n2=='^'))||((n1=='F')&&(n2==' ')))period=2863.44243;
    else if(((n1=='F')&&(n2=='#'))||((n1=='G')&&(n2=='^')))period=2702.77575;
    else if((n1=='G')&&(n2==' '))period=2551.02041;
    else if((n1=='G')&&(n2=='#'))period=2407.89791;
    else period=100.0;
    return period;
}
  
   
void play_melody(int S, float P){
    int temp=0;
    while(temp<=S){
        temp++;
        mperiod=P;
        update_motorstate();
        wait(1);
    }
}

void count_and_speed(){
    //checking=1;
    R_now+=117;
    t.stop();
    interval=t.read();
    velocity=(1.0f/interval);//117.0f;
    t.reset();
    t.start();
    thread.signal_set(0x1);
    //checking=0;
}

volatile float V_need = 0;
void control_final(){
    while(true){
        Thread::signal_wait(0x1);
        //checking=1;
        I1.disable_irq();
        I2.disable_irq();
        I3.disable_irq();
        //control both speed and velocity
        V_need = 0.8f + 0.009f*(R_target - R_now)+k1*(-velocity);//position control
        if(V_need > V_target){V_need = V_target;}//reconcile position and speed control
        duty_cycle = 0.5f*(V_need - velocity);
        if(R_now >= R_target){duty_cycle = 0;}//prevent overshoot
        if(duty_cycle<0){duty_cycle=0;}
        update_motorstate();       
        I1.enable_irq();
        I2.enable_irq();
        I3.enable_irq();
        //checking=0;      
        }       
    }
    
volatile float velocity_tmp;

void precise_count_and_speed(){
    //checking=1;
    counter ++;
    t.stop();
    interval = t.read();
    velocity_tmp = (1.0f/interval)/117.0f;
    thread.signal_set(0x2);
    t.reset();
    t.start();
    //checking=0;    
}

void precisecontrol(){
    while(true){
    Thread::signal_wait(0x2);
    //checking=1;
    precise1.disable_irq();
    integral = integral + (V_last-velocity_tmp)*interval;
    duty_cycle  = (50.0f*(V_target-velocity_tmp) + 0.35f*(V_last-velocity_tmp)/interval + 0.5f*integral);
    V_last = velocity_tmp;
    if((R_target-counter)<=200.0f){duty_cycle=0;}
    R_now =(float)counter;
    velocity=V_last;
    update_motorstate();
    precise1.enable_irq();
    //checking=0;
    }   
}

void update_motorstate(){
    intState = readRotorState();
    motorOut((intState-orState+lead+6)%6);    
}   

//Main
int main() {  
    pc.printf("Hello, input command:\n\r");
    //command_read();
    control_command();
    orState = motorHome();
    pc.printf("Rotor origin: %x\n\r",orState);

    if(V_target>=3.8f){
       t.start();
       I1.fall(&count_and_speed);
       I1.rise(&update_motorstate);
       I2.rise(&update_motorstate);
       I3.rise(&update_motorstate);
       I2.fall(&update_motorstate);
       I3.fall(&update_motorstate);
       thread.start(&control_final);
    }
    else{
       t.start();
       precise1.rise(&precise_count_and_speed);
       thread.start(&precisecontrol);
    }

    duty_cycle = 1.0;
    update_motorstate();
    while(1){
        wait(0.5);
        pc.printf(" R: %f\n\r V: %f\n\r D: %f\n\r \n\r",R_now/117.0f, velocity, duty_cycle);
        
    }
 }   
