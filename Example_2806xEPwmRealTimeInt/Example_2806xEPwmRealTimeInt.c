//
// Included Files
//
#include "DSP28x_Project.h"     // Device Headerfile and Examples Include File
#include <math.h>
#include <string.h>
#include <stdlib.h>

//
// Defines
//

//
// Configure if ePWM timer interrupt is enabled at the PIE level:
// 1 = enabled,  0 = disabled
//
#define PWM1_INT_ENABLE  1
#define PWM1_TIMER_TBPRD   2000       // old value 7500 -> 16.6666 Hz for sine
#define PI 3.14159f

//
// Function Prototypes
//
__interrupt void epwm4_timer_isr(void);
__interrupt void epwm5_timer_isr(void);
__interrupt void epwm6_timer_isr(void);
void InitEPwmTimer(void);
void InitEPwmTimer2(void);
void InitEPwmTimer3(void);
float normalFactor();
void changeFrequency();

void scia_interrupt_init(void);
void initSciaGpio();
void scia_fifo_init();
void scia_rx_init();
void scia_msg(char * msg);
int32 ascii_to_int(char *str);
int32 isCorrectString(char *str);

//
// Globals
//

float angle = 0;
float stepSize = 0;       
float stepSizeIncreasePerPWMHigh = 2 * PI / 2250 / 22500; // 10 Hz for sine / period

char rx_buffer[32];
int32 msg_ready = 0;
volatile int32 rx_index = 0;
volatile int32 targetFreq = 0;
float targetStepSize = 0;
int breakActive = 0;
int breakElapsed = 0;


//
// Main
//
void main(void)
{

    //
    // Step 1. Initialize System Control:
    // PLL, WatchDog, enable Peripheral Clocks
    // This example function is found in the F2806x_SysCtrl.c file.
    //
    InitSysCtrl();

    //
    // Step 2. Initalize GPIO:
    // This example function is found in the F2806x_Gpio.c file and
    // illustrates how to set the GPIO to it's default state.
    //
    // InitGpio();  // Skipped for this example - LEDs set up in main code
    EALLOW;

    EDIS;

    //
    // Step 3. Clear all interrupts and initialize PIE vector table:
    // Disable CPU interrupts
    //
    DINT;

    //
    // Initialize the PIE control registers to their default state.
    // The default state is all PIE interrupts disabled and flags
    // are cleared.
    // This function is found in the F2806x_PieCtrl.c file.
    //
    InitPieCtrl();

    //
    // Disable CPU interrupts and clear all CPU interrupt flags
    //
    IER = 0x0000;
    IFR = 0x0000;

    //
    // Initialize the PIE vector table with pointers to the shell Interrupt
    // Service Routines (ISR).
    // This will populate the entire table, even if the interrupt
    // is not used in this example.  This is useful for debug purposes.
    // The shell ISR routines are found in F2806x_DefaultIsr.c.
    // This function is found in F2806x_PieVect.c.
    //
    InitPieVectTable();

    //
    // Interrupt that is used in this example is re-mapped to
    // ISR function found within this file.
    //
    EALLOW;  // This is needed to write to EALLOW protected registers
    PieVectTable.EPWM4_INT = &epwm4_timer_isr;
    PieVectTable.EPWM5_INT = &epwm5_timer_isr;
    PieVectTable.EPWM6_INT = &epwm6_timer_isr;
    EDIS;    // This is needed to disable write to EALLOW protected registers


    InitEPwmTimer();
    InitEPwmTimer2();
    InitEPwmTimer3();

    EALLOW;
    SysCtrlRegs.PCLKCR0.bit.TBCLKSYNC = 1;  // Start all time-base counters in sync
    EDIS;

    //
    // Enable CPU INT3 which is connected to EPWM1-6 INT
    //
    IER |= M_INT3;

    //
    // Enable EPWM INTn in the PIE: Group 3 interrupt 1-6
    //
    PieCtrlRegs.PIEIER3.bit.INTx4 = PWM1_INT_ENABLE;
    PieCtrlRegs.PIEIER3.bit.INTx5 = PWM1_INT_ENABLE;
    PieCtrlRegs.PIEIER3.bit.INTx6 = PWM1_INT_ENABLE;

    //
    // Initially disable time-critical interrupts
    //
    SetDBGIER(0x0000);          // PIE groups time-critical designation

    initSciaGpio();
	scia_fifo_init();
	scia_rx_init();
	scia_interrupt_init();

    //
    // Enable global Interrupts and higher priority real-time debug events
    //
    EINT;   // Enable Global interrupt INTM
    ERTM;   // Enable Global realtime interrupt DBGM

    scia_msg("\r\nMotor frequency: \0");
    for(;;){
        if (msg_ready) {
            if (isCorrectString((char*)rx_buffer)) 
            {
                if ((char*)rx_buffer[0] == 'b') 
                {
                    breakActive = 1;
                    scia_msg("\r\nBreak Active \0");
                } 
                else
                {
                    if (breakActive == 1)
                    {
                        EPwm4Regs.CMPA.half.CMPA = 0;
                        EPwm5Regs.CMPA.half.CMPA = 0;
                        EPwm6Regs.CMPA.half.CMPA = 0;
                        targetStepSize = 0;
                        stepSize = 0;
                        breakElapsed = 0;
                        breakActive = 0;
                    }
                    breakActive = 0;
                    targetFreq = ascii_to_int((char*)rx_buffer);
                    targetStepSize = targetFreq * 2 * PI / 22500;
                    scia_msg("\r\nFrequency is set to: \0");
                    if (targetFreq >= 100)
                    {
                        scia_msg("+100");
                    } 
                    else if (targetFreq <= -100)
                    {
                        scia_msg("-100");
                    }
                    else 
                    {
                        scia_msg(rx_buffer);
                    }
                }
            } 
            else 
            {
                scia_msg("\r\nIncorrect Input!\0");
            }
            
            msg_ready = 0;
            memset((void *)rx_buffer, 0, 32);
            scia_msg("\r\nEnter motor frequency: \0");
        }
    }

}

//
// InitEPwmTimer - 
//
void
InitEPwmTimer()
{
    EALLOW;
    SysCtrlRegs.PCLKCR0.bit.TBCLKSYNC = 0;          // Stop all the TB clocks
    GpioCtrlRegs.GPAMUX1.bit.GPIO6 = 1;
    GpioCtrlRegs.GPAMUX1.bit.GPIO7 = 1;
    GpioCtrlRegs.GPBMUX2.bit.GPIO52 = 0;

    GpioCtrlRegs.GPBDIR.bit.GPIO52 = 1;
    GpioDataRegs.GPBCLEAR.bit.GPIO52 = 1;
    EDIS;

    EPwm4Regs.TBCTL.bit.FREE_SOFT = 0;

    EPwm4Regs.TBPRD = PWM1_TIMER_TBPRD;			    // set up PWM4 Period
    EPwm4Regs.TBCTL.bit.CTRMODE = TB_COUNT_UP;      // count up mode
    EPwm4Regs.ETSEL.bit.INTSEL = ET_CTR_ZERO;       // select INT on Zero event
    EPwm4Regs.ETSEL.bit.INTEN = PWM1_INT_ENABLE;    // enable INT
    EPwm4Regs.ETPS.bit.INTPRD = ET_1ST;             // generate INT on 1st event
    EPwm4Regs.TBCTL.bit.PHSEN = TB_DISABLE;         // disable phase shift (master)

    EPwm4Regs.TBCTL.bit.SYNCOSEL = TB_CTR_ZERO;     // Generate sync pulse on zero
    EPwm4Regs.TBPHS.half.TBPHS = 0;                 // no phase shift
    EPwm4Regs.TBCTR = 0x0000;					    // clear timer counter
    

    EPwm4Regs.CMPA.half.CMPA = PWM1_TIMER_TBPRD/2;      // only for init

    EPwm4Regs.AQCTLA.bit.CAU = AQ_CLEAR;                // Clear PWM1A on CMPA up-count
    EPwm4Regs.AQCTLA.bit.ZRO = AQ_SET;

    // Dead-band configuration
    EPwm4Regs.DBCTL.bit.OUT_MODE = DB_FULL_ENABLE;      // Enable dead-band on A and B
    EPwm4Regs.DBCTL.bit.POLSEL = DB_ACTV_HIC;           // A -> original, B -> inverted
    EPwm4Regs.DBCTL.bit.IN_MODE = DBA_ALL;              // Use A as input for both rising/falling

    EPwm4Regs.DBRED = 2;        // Rising edge delay (dead time)
    EPwm4Regs.DBFED = 2;        // Falling edge delay (dead time)
}


void
InitEPwmTimer2()
{
    EALLOW;
    SysCtrlRegs.PCLKCR0.bit.TBCLKSYNC = 0;          // Stop all the TB clocks
    GpioCtrlRegs.GPAMUX1.bit.GPIO8 = 1;
    GpioCtrlRegs.GPAMUX1.bit.GPIO9 = 1;
    EDIS;

    EPwm5Regs.TBCTL.bit.FREE_SOFT = 0;

    EPwm5Regs.TBPRD = PWM1_TIMER_TBPRD;             // set up PWM5 Period
    EPwm5Regs.TBCTL.bit.CTRMODE = TB_COUNT_UP;      // count up mode
    EPwm5Regs.ETSEL.bit.INTSEL = ET_CTR_ZERO;       // select INT on Zero event
    EPwm5Regs.ETSEL.bit.INTEN = PWM1_INT_ENABLE;    // enable INT
    EPwm5Regs.ETPS.bit.INTPRD = ET_1ST;             // generate INT on 1st event
    EPwm5Regs.TBCTR = 0x0000;					    // clear timer counter

    EPwm5Regs.TBCTL.bit.SYNCOSEL = TB_SYNC_IN;      // Sync from upstream


    EPwm5Regs.CMPA.half.CMPA = PWM1_TIMER_TBPRD/2;

    EPwm5Regs.AQCTLA.bit.CAU = AQ_CLEAR;                // Clear PWM1A on CMPA up-count
    EPwm5Regs.AQCTLA.bit.ZRO = AQ_SET;

    // Dead-band configuration
    EPwm5Regs.DBCTL.bit.OUT_MODE = DB_FULL_ENABLE;      // Enable dead-band on A and B
    EPwm5Regs.DBCTL.bit.POLSEL = DB_ACTV_HIC;           // A -> original, B -> inverted
    EPwm5Regs.DBCTL.bit.IN_MODE = DBA_ALL;              // Use A as input for both rising/falling

    EPwm5Regs.DBRED = 2;        // Rising edge delay (dead time)
    EPwm5Regs.DBFED = 2;        // Falling edge delay (dead time)
}


void
InitEPwmTimer3()
{
    EALLOW;
    SysCtrlRegs.PCLKCR0.bit.TBCLKSYNC = 0;      // Stop all the TB clocks
    GpioCtrlRegs.GPAMUX1.bit.GPIO10 = 1;
    GpioCtrlRegs.GPAMUX1.bit.GPIO11 = 1;
    EDIS;

    EPwm6Regs.TBCTL.bit.FREE_SOFT = 0;

    EPwm6Regs.TBPRD = PWM1_TIMER_TBPRD;			    // set up PWM6 Period
    EPwm6Regs.TBCTL.bit.CTRMODE = TB_COUNT_UP;      // count up mode
    EPwm6Regs.ETSEL.bit.INTSEL = ET_CTR_ZERO;       // select INT on Zero event
    EPwm6Regs.ETSEL.bit.INTEN = PWM1_INT_ENABLE;    // enable INT
    EPwm6Regs.ETPS.bit.INTPRD = ET_1ST;             // generate INT on 1st event
    EPwm6Regs.TBCTR = 0x0000;					    // clear timer counter

    EPwm6Regs.TBCTL.bit.SYNCOSEL = TB_SYNC_IN;


    EPwm6Regs.CMPA.half.CMPA = PWM1_TIMER_TBPRD/2;

    EPwm6Regs.AQCTLA.bit.CAU = AQ_CLEAR;            // Clear PWM1A on CMPA up-count
    EPwm6Regs.AQCTLA.bit.ZRO = AQ_SET;


    // Dead-band configuration
    EPwm6Regs.DBCTL.bit.OUT_MODE = DB_FULL_ENABLE;  // Enable dead-band on both A and B
    EPwm6Regs.DBCTL.bit.POLSEL = DB_ACTV_HIC;       // A -> original, B -> inverted
    EPwm6Regs.DBCTL.bit.IN_MODE = DBA_ALL;          // Use A as input for both rising/falling

    EPwm6Regs.DBRED = 2;        // Rising edge delay (dead time)
    EPwm6Regs.DBFED = 2;        // Falling edge delay (dead time)
}

// Interrupts
__interrupt void epwm4_timer_isr(void)
{   
    if (breakActive) 
    {
        EPwm4Regs.CMPA.half.CMPA = PWM1_TIMER_TBPRD / 2;
        EPwm5Regs.CMPA.half.CMPA = PWM1_TIMER_TBPRD / 2;
        EPwm6Regs.CMPA.half.CMPA = (PWM1_TIMER_TBPRD / 2) + 400;
    }
    else 
    {
        EPwm4Regs.CMPA.half.CMPA = PWM1_TIMER_TBPRD * ((sinf(angle) + sinf(angle * 3) / 6) + 1) / 2 * normalFactor();

        angle += stepSize;
        if (angle > 2 * PI) 
        {
            angle -= 2 * PI;
        } 
        else if (angle < 0) 
        {
            angle += 2 * PI;
        }

        changeFrequency();
    }

    EPwm4Regs.ETCLR.bit.INT = 1;
    PieCtrlRegs.PIEACK.all = PIEACK_GROUP3;
}

__interrupt void epwm5_timer_isr(void)
{
    if (!breakActive) 
    {
        EPwm5Regs.CMPA.half.CMPA = PWM1_TIMER_TBPRD * ((sinf(angle + 2 * PI / 3) + sinf(3 * (angle + 2 * PI / 3)) / 6) + 1) / 2 * normalFactor();
    }
    EPwm5Regs.ETCLR.bit.INT = 1;
    PieCtrlRegs.PIEACK.all = PIEACK_GROUP3;
}

__interrupt void epwm6_timer_isr(void)
{
    if (!breakActive)
    {
        EPwm6Regs.CMPA.half.CMPA = PWM1_TIMER_TBPRD * ((sinf(angle + 4 * PI / 3) + sinf(3 * (angle + 4 * PI / 3)) / 6) + 1) / 2 * normalFactor();
    }
    EPwm6Regs.ETCLR.bit.INT = 1;
    PieCtrlRegs.PIEACK.all = PIEACK_GROUP3;
}

float normalFactor() 
{
    if (stepSize < 2 * PI / 450 && stepSize > -2 * PI / 450)            // until 50 Hz
    {
        if (stepSize >= 0) 
        {
            return stepSize / ( 2 * PI / 450);
        }
        else 
        {
            return -stepSize / ( 2 * PI / 450);
        }
    }
    else 
    {
        return 1;
    }
}

void increaseStepSize()
{
    if (stepSize < 2 * PI / 225 && stepSize < targetStepSize) 
    {
        stepSize += stepSizeIncreasePerPWMHigh;
    }
    else 
    {
        if (2 * PI / 225 >= targetStepSize )
        {
            stepSize = targetStepSize;
        } 
        else  
        {
            stepSize =  2 * PI / 225;
        }
    }
}

void decreaseStepSize()
{
    if (stepSize > -2 * PI / 225 && stepSize > targetStepSize) 
    {
        stepSize -= stepSizeIncreasePerPWMHigh;
    }
    else 
    {
        if (-2 * PI / 225 <= targetStepSize )
        {
            stepSize = targetStepSize;
        } 
        else  
        {
            stepSize =  -2 * PI / 225;
        }
    }
}

void changeFrequency()
{
    if (stepSize < targetStepSize) 
    {
        increaseStepSize();
    } 
    else 
    {
        decreaseStepSize();
    } 
}

int32 isCorrectString(char *str)            // Checks if received message is valid
{
    int i = 1;
    if (str[0] == 'b' && str[1] == '\0')
    {
        return 1;
    }
    else 
    {
        if (str[0] != '-' && str[0] != '+')
        {
            return 0;
        }
        while (str[i] != '\0') {
            if (str[i] < '0' || str[i] > '9') 
            {
                return 0;
            }
            i++;
        }
        return 1;
    }
}

int32 ascii_to_int(char *str)
{
    int32 result = 0;

    int i = 1;
    while (str[i] >= '0' && str[i] <= '9') {
        result = result * 10 + (str[i] - '0');  // Convert char to digit
        i++;
    }

    if (str[0] == '-') 
    {
        result *= -1;
    }

    return result;
}


void
initSciaGpio()
{
    EALLOW;

    GpioCtrlRegs.GPAPUD.bit.GPIO28 = 0;     // Enable pull-up for GPIO28

    GpioCtrlRegs.GPAPUD.bit.GPIO29 = 0;	    // Enable pull-up for GPIO29
        
    GpioCtrlRegs.GPAQSEL2.bit.GPIO28 = 3;   // Asynch input GPIO28 
  
    GpioCtrlRegs.GPAMUX2.bit.GPIO28 = 1;
    
    GpioCtrlRegs.GPAMUX2.bit.GPIO29 = 1;
  

    EDIS;
}

void scia_rx_init()
{
    SciaRegs.SCICCR.all =0x0007;
    SciaRegs.SCICTL1.all =0x0003;

    // 9600 baudrate
    SciaRegs.SCIHBAUD    =0x0001;
    SciaRegs.SCILBAUD    =0x0024;

    SciaRegs.SCICTL1.all =0x0023;
}

void
scia_xmit(int a)
{
    while (SciaRegs.SCIFFTX.bit.TXFFST != 0)
    {
        
    }
    SciaRegs.SCITXBUF=a;
}


void
scia_msg(char * msg)
{
    int i;
    i = 0;
    while(msg[i] != '\0')
    {
        scia_xmit(msg[i]);
        i++;
    }
}
void
scia_fifo_init()
{
	SciaRegs.SCIFFTX.bit.SCIFFENA = 1;          // Enable FIFO enhancement
	SciaRegs.SCIFFRX.bit.RXFFIENA = 1;          // Enable RX FIFO interrupt
	SciaRegs.SCIFFRX.bit.RXFIFORESET = 1;       // Reset/enable RX FIFO
	SciaRegs.SCIFFTX.bit.TXFIFOXRESET = 1;      // Reset/enable TX FIFO
	SciaRegs.SCIFFRX.bit.RXFFIL = 1;            // FIFO Interrupt triggerred by 1 Byte received
}
__interrupt void scia_rx_isr(void)
{
    char ch = SciaRegs.SCIRXBUF.all;
	// read the RXBuffer
    if (ch == '\r' || ch == '\n') {
        rx_buffer[rx_index] = '\0';             // Null-terminate string
        msg_ready = 1;
        rx_index = 0;
    } else {
        if (rx_index < 31) {
            rx_buffer[rx_index++] = ch;
        }
    }


	SciaRegs.SCIFFRX.bit.RXFFINTCLR = 1;        // Clear RX interrupt flag
    PieCtrlRegs.PIEACK.all = PIEACK_GROUP9;
}


void scia_interrupt_init(void)
{
    EALLOW;
    PieVectTable.SCIRXINTA = &scia_rx_isr;
    EDIS;

    PieCtrlRegs.PIECTRL.bit.ENPIE = 1;      // Enable PIE block
    PieCtrlRegs.PIEIER9.bit.INTx1 = 1;      // Enable PIE Group 9, INT1 (SCIRXINTA)
    IER |= M_INT9;                          // Enable CPU int 9 (Group 9)
    EINT;                                   // Enable global interrupts
    ERTM;                                   // Enable real-time debug
}



//
// End of File
//


// We used Example_2806xEPwmRealTimeInt from C2000Ware as our template for this project.

