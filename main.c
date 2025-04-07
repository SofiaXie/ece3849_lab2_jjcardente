/**
 * main.c
 *
 * ECE 3849 Lab 0 Starter Project
 * Gene Bogdanov    10/18/2017
 *
 * This version is using the new hardware for B2017: the EK-TM4C1294XL LaunchPad with BOOSTXL-EDUMKII BoosterPack.
 *
 */

#include <stdint.h>
#include <stdbool.h>
#include "driverlib/fpu.h"
#include "driverlib/sysctl.h"
#include "driverlib/interrupt.h"
#include "Crystalfontz128x128_ST7735.h"
#include <stdio.h>

#include "buttons.h" //Line added for the sake of the timer


//Definitions and Inclusions for Clock Signal Generation
#include <math.h>
#include "inc/hw_memmap.h"
#include "driverlib/gpio.h"
#include "driverlib/pwm.h"
#include "driverlib/pin_map.h"
#include "random.h"
#define PWM_FREQUENCY 20000 // PWM frequency = 20 kHz
#define LCD_VERTICAL_MAX 128
#define ADC_BUFFER_SIZE 2048
#define ADC_BUFFER_WRAP(i) ((i) & (ADC_BUFFER_SIZE - 1))
#define LCD_DIMENSION 128
#define ADC_OFFSET 2048

uint32_t gSystemClock; // [Hz] system clock frequency
volatile uint32_t gTime = 8345; // time in hundredths of a second

volatile uint16_t Data_Buffer[128];

uint32_t DataFlag = 0;
extern volatile uint16_t gADCBuffer[ADC_BUFFER_SIZE];
extern volatile int32_t gADCBufferIndex;
extern uint32_t OpFlag;
volatile int triggerType = 0;
volatile int voltsPerDiv = 3;
float cpu_load = 0.0;
extern uint32_t count_unloaded;
uint32_t count_loaded = 0;

//Test Test

int main(void)
{
    IntMasterDisable();

    // Enable the Floating Point Unit, and permit ISRs to use it
    FPUEnable();
    FPULazyStackingEnable();

    // Initialize the system clock to 120 MHz
    gSystemClock = SysCtlClockFreqSet(SYSCTL_XTAL_25MHZ | SYSCTL_OSC_MAIN | SYSCTL_USE_PLL | SYSCTL_CFG_VCO_480, 120000000);

    Crystalfontz128x128_Init(); // Initialize the LCD display driver
    Crystalfontz128x128_SetOrientation(LCD_ORIENTATION_UP); // set screen orientation

    tContext sContext;
    GrContextInit(&sContext, &g_sCrystalfontz128x128); // Initialize the grlib graphics context
    GrContextFontSet(&sContext, &g_sFontFixed6x8); // select font

    ButtonInit(); //Initialize Buttons
    signal_init();
    init_CPU_Measure();
    IntMasterEnable(); //Enable Interrupts
    init_ADC1();

   /* uint32_t time;  // local copy of gTime
    uint32_t time_min; //minutes
    uint32_t time_sec; //seconds
    uint32_t time_f; //hundreths of second
    char str[50];   // string buffer*/
    // full-screen rectangle
    tRectangle rectFullScreen = {0, 0, GrContextDpyWidthGet(&sContext)-1, GrContextDpyHeightGet(&sContext)-1};

    init_Grid(&sContext, &rectFullScreen);
    init_Measure(&sContext);

    while (true) {
        /*GrContextForegroundSet(&sContext, ClrBlack);
        GrRectFill(&sContext, &rectFullScreen); // fill screen with black
        time = gTime; // read shared global only once
        time_min = time/6000; //take minutes
        time_sec = (time-(time_min*6000))/100; //take seconds
        time_f = time-(time_min*6000)-(time_sec*100);
        snprintf(str, sizeof(str), "Time = %02u:%02u:%02u", time_min, time_sec, time_f); // convert time to string
        GrContextForegroundSet(&sContext, ClrYellow); // yellow text
        GrStringDraw(&sContext, str, length -1, x 0, y 0, opaque false); */

        int index = Trigger();
        int x;
        for (x = 0; x < LCD_VERTICAL_MAX; x++) {
            Data_Buffer[x] = gADCBuffer[ADC_BUFFER_WRAP(index + x)];
        }
        plot_data(&sContext, Data_Buffer, &rectFullScreen);
        GrFlush(&sContext); // flush the frame buffer to the LCD

        if (OpFlag == 1) {
            OpFlag = 0;
            int op;
            fifo_get(&op);
            if (op == 3) {
                triggerType = triggerType ^ 1;
            } else if (op == 1) {
                if (voltsPerDiv == 4) {
                    voltsPerDiv = 0;
                } else {
                    voltsPerDiv++;
                }
            }
        }
        count_loaded = cpu_load_count();
        cpu_load = 1.0f - (float)count_loaded/count_unloaded; // compute CPU load

    }
}

void signal_init() {

    // configure M0PWM2, at GPIO PF2, BoosterPack 1 header C1 pin 2
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    GPIOPinTypePWM(GPIO_PORTF_BASE, GPIO_PIN_2);
    GPIOPinConfigure(GPIO_PF2_M0PWM2);
    GPIOPadConfigSet(GPIO_PORTF_BASE, GPIO_PIN_2,
    GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
    // configure the PWM0 peripheral, gen 1, outputs 2 and 3
    SysCtlPeripheralEnable(SYSCTL_PERIPH_PWM0);
    // use system clock without division
    PWMClockSet(PWM0_BASE, PWM_SYSCLK_DIV_1);
    PWMGenConfigure(PWM0_BASE, PWM_GEN_1,
    PWM_GEN_MODE_DOWN | PWM_GEN_MODE_NO_SYNC);
    PWMGenPeriodSet(PWM0_BASE, PWM_GEN_1, roundf((float)gSystemClock/PWM_FREQUENCY));
    PWMPulseWidthSet(PWM0_BASE, PWM_OUT_2,roundf((float)gSystemClock/PWM_FREQUENCY*0.4f));
    PWMOutputState(PWM0_BASE, PWM_OUT_2_BIT, true);
    PWMGenEnable(PWM0_BASE, PWM_GEN_1);

}

int Trigger(void) { // search for rising edge trigger

    // Step 1
    int x = gADCBufferIndex - LCD_DIMENSION/2;

    // Step 2
    int x_stop = x - ADC_BUFFER_SIZE/2;
        for (; x > x_stop; x--) {
            if (triggerType == 0) {
                if ( (gADCBuffer[ADC_BUFFER_WRAP(x)] >= ADC_OFFSET) && (gADCBuffer[ADC_BUFFER_WRAP(x - 1)] < ADC_OFFSET)) //Rising Trigger
                    break;
            } else if (triggerType == 1) {
                if ( (gADCBuffer[ADC_BUFFER_WRAP(x)] <= ADC_OFFSET) && (gADCBuffer[ADC_BUFFER_WRAP(x - 1)] > ADC_OFFSET)) //Falling Trigger
                    break;
            }
        }

    // Step 3
    if (x == x_stop) { // for loop ran to the end
        x = gADCBufferIndex - LCD_DIMENSION/2;; // reset x back to how it was initialized
    }
    return x;
}

