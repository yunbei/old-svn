/*
 * OWSlave
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 *  any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http: * www.gnu.org/licenses/>.
 *
 * based on owdevice - A small 1-Wire emulator for AVR Microcontroller
 *
 * Copyright (C) 2012  Tobias Mueller mail (at) tobynet.de
 *
 * VERSION 1.3pre2 for ATmega48
 *
 * Created: 15.05.2013 13:36:59
 *  Author: Michael Markstaller
 *
 * use included Makefile: just change target MCU and avrdude params
 *
 * Notes:
 * - Default: with internal or external pullup to Vcc on counters: counting on FALLING edge
 * - OWRXLED is connected with the ANODE to PAx
 * - OUT A/B could be used both ways - better is active-high as it's not inverted and not flapping on flashing/re-init
 *
 *
 * Changelog:
 * v1.52(->1.06) 2013-12-06 - !!!PENDING!!!
 * - Add PA0 / PA1 as 2bit output OUT.A / OUT.B -> Active high (though just inverted, no matter, but active-high is stable during reset)
 *
 * v1.51 2013-11-10:
 * - change OWRXLED from PA7 to PA2 on Attiny84 - keep PA7(ICP) free for possible Ehz/sml serial input
 * v1.5:
 *  - changed timing OWT_READLINE 3->2 in RM-code, ported forward
 *  - configurable pullups
 * v1.4pre - switched back from RM-code 1.3 - attiny84 counter etc.
 * v1.3pre2
 *  - combined prototype for Counter / RM with full owfs-support as family E1
 * v1.3pre1:
 *  - Major cleanup code: define serial & debug-macros
 *  - adjust ISR to exact current defines in avr-libc, add uartlib (tiny24|44|84/25|45|85 missing yet! fits if UART is removed )
 *  - add EEPROM-functions
 *  - add basic ow function-commands instead of page/memory access used in DS2423
 *  - Family E1 introduced
 *  - much testcode
 * v1.2: basic code
 */


#include <stdlib.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/sleep.h>
#include <string.h>
// #include <avr/pgmspace.h>
#include "common.h"
//#include "uart.h"

//#define DEBUG 0
#include "debug.h"
#ifndef F_CPU
//# warning "F_CPU was not defined!! defining it now in debug.h but you should take care before!"
#define F_CPU 8000000UL //very important! define before delay.h as delay.h fucks up the serial-timing otherwise somehow
#endif
//#include <util/delay.h>


#if defined(__AVR_ATmega48__) || (__AVR_ATmega88__) || (__AVR_ATmega328P__)
/* ATMega is currently untested! */
# error "ATMega defs are very outdated/broken in this code - fix first"
// OW_PORT Pin 4  - PD2(INT0)

//OW Pin
#define OW_PORT PORTD //1 Wire Port
#define OW_PIN PIND //1 Wire Pin as number
#define OW_PORTN (1<<PIND2)  //Pin as bit in registers
#define OW_PINN (1<<PIND2)
#define OW_DDR DDRD  //pin direction register
#define SET_LOW OW_DDR|=OW_PINN;OW_PORT&=~OW_PORTN;  //set 1-Wire line to low
#define RESET_LOW {OW_DDR&=~OW_PINN;}  //set 1-Wire pin as input
//Pin interrupt
#define EN_OWINT {EIMSK|=(1<<INT0);EIFR=(1<<INTF0);}  //enable interrupt 0 and *clear* it
#define DIS_OWINT  EIMSK&=~(1<<INT0);  //disable interrupt 0
#define SET_OWINT_RISING EICRA|=(1<<ISC01)|(1<<ISC00);  //set interrupt at rising edge
#define SET_OWINT_FALLING {EICRA|=(1<<ISC01);EICRA&=~(1<<ISC00);} //set interrupt at falling edge
#define CHK_INT_EN (EIMSK&(1<<INT0))==(1<<INT0) //test if interrupt enabled
#define PIN_INT ISR(INT0_vect)  // the interrupt service routine
//Timer Interrupt
#define EN_TIMER {TIMSK0 |= (1<<TOIE0); TIFR0|=(1<<TOV0);} //enable timer0 interrupt
#define DIS_TIMER TIMSK0  &= ~(1<<TOIE0); // disable timer interrupt
#define TCNT_REG TCNT0  //register of timer-counter
#define TIMER_INT ISR(TIMER0_OVF_vect) //the timer interrupt service routine

/* TODO/FIXME: define ports for status-leds, etc
 * AVOID:
 * PB2-5: SPI
 * PC4-5: TWI
 * PC0-3: ADC0-3
 * PD5-6: PWM0 (?TC in use!)
 * PB1-2: PWM1
 * PB3/PD3: PWM2
 * PD2-3: INT0-1
 * PD0-1: USART
 * Good:
 * PD4
 * PB6-7 (XTAL)
 * PD7
 * PB0
 * PB1-5 (used by counter, collides PWM0,SPI)
 */

#define OWRXLED_PORT PORTD
#define OWRXLED_DDR DDRD
#define OWRXLED_PIN PIND7

/* PWRDOWN: use PORTB / same PORT as counters ! */
#define PWRDOWN_PORT PORTB
#define PWRDOWN_DDR DDRB
#define PWRDOWN_PIN PINB0

#define INIT_LED_PINS RMRXLED_DDR |= (1<<RMRXLED_PIN); \
                     OWRXLED_DDR |= (1<<OWRXLED_PIN); /* pins as output */
/* Toogle:  PORTB ^= ( 1 << PB0 );
 * OWRXLED_PORT ^= (1<<OWRXLED_PIN);
 */

//FIXME / TODO: double-check & compare timings!
#define OWT_MIN_RESET 51 // tRSTL 512uS in DS, tRSTH = 584; 51 are 408uS
#define OWT_RESET_PRESENCE 4 //tPDT 64uS in DS
#define OWT_PRESENCE 20 // unclear, mabe between 512 and 584 uS; 20 are 160uS? real bus 9/170uS -> default settings 31/200
#define OWT_READLINE 2 //for fast master, maybe 4 for long lines; flexible is 10 to 24 uS
#define OWT_LOWTIME 3 //3=24uS for DS9490,

//Initializations of AVR
#define INIT_AVR CLKPR=(1<<CLKPCE); /* FIXME! this is crap, next line disables it! */ \
                  CLKPR=0; /* 8Mhz */ \
                  /* CLKPR=(1<<CLKPCE)|(0<<CLKPS3)|(0<<CLKPS2)|(0<<CLKPS1)|(1<<CLKPS0);  FIXME/CHECK: this gives 4MHz */ \
                  TIMSK0=0; \
                  EIMSK=(1<<INT0);  /*set direct GICR INT0 enable*/ \
                  TCCR0B=(1<<CS00)|(1<<CS01); /*8mhz /64 cause 8 bit Timer interrupt every 8us */ \
                  TIMSK1 |= (1<<TOIE1); TIFR1 |= (1<<TOV1); /* enable overflow int for timer1 */ \
                  TCCR1A |= (1<<COM1A1)|(1<<COM1B1)|(1<<WGM10); /* 8bit Fast-PWM 0<<COM1A0 0<<COM1B0 0<<WGM11 */ \
                  TCCR1B |= (1<<WGM12)|(1<<CS11)|(1<<CS10); /* 0<<WGM13 FastPWM - Clock for TIM1 /1024 ; /256 ohne |(1<<CS10) */ \
                  /* FIXME: disable ADC etc to save power here? */

                  /* TCCR0B=(1<<CS00)|(1<<CS01);  FIXME/CHECK: 4mhz /8 cause 8 bit Timer interrupt every 2us? so we have only 8 ticks! */
                  /* MAYBE use 16bit TC1 for this @4MHz?? */

                  /* FIXME: init TC1 for PWM/internal time-clock */
                  /* FIXED: test wether TC0 can be still used for PWM->YES 490Hz- though it "flickers" for 2x20ms when 1-Wire is active: */
                  /* DDRD |= ((1<<PIND5)|(1<<PIND6)); PD5& as output */
                  /* TCCR0A |= ((2<<COM0A0) | (2<<COM0B0) | (3<<WGM00)); enable output-compare-match */
                  /* OCR0A = 128; OCR0B = 192; set dummy-values 50/75% */
                  /* TCNT1 - Test */
                  /* DDRB |= ((1<<PINB1)|(1<<PINB2)); PB1&2 as output */
                  /* OCR1A = 128; OCR1B = 192; set dummy-values 50/75% */
                  /* NO internal 1V1 AREF - we have 1M/470k = 20.1758 * AREAD / 1000 = Volt */
                  /* /124 gives PWM cycle of 32.8ms = 30Hz ; 4885-5190 = 305 * ++ in OVF per 10s */
                  /* /256 gives 125Hz? 8ms = 1222 * ++ in OVF per 10s every 32us ?*/

                  /* !! FIXME: must set DDRB PB0/PB1 to output and remove counter below! for PWM to work FIXME !! */
                  /* Atmega48: PD5/PD6; Attiny2313: PB2/PD5 */

#define PWRSAVE_AVR PRR = (1<<PRTWI)|(1<<PRTIM2)|(1<<PRSPI); /* power down TWI, TIMCNT2, leave USART |(1<<PRUSART0) on for debug! */ \
                    DIDR0 = (1<<ADC5D)|(1<<ADC4D)|(1<<ADC3D)|(1<<ADC2D)|(1<<ADC1D)|(1<<ADC0D); /* diable Digital-in on ADC5..0 = PORTC5..0 */
                    /* |(1<<PRTIM1) and ADC |(1<<PRADC) enabled for now */


#define PC_INT_ISR ISR(PCINT0_vect) { /* ATmega48 is PCINT0_vect - counter PCINT */ \
          if (((PINB&(1<<PINB1))==0)&&((istat&(1<<PINB1))==(1<<PINB1))) { Counter1++; } \
          if (((PINB&(1<<PINB2))==0)&&((istat&(1<<PINB2))==(1<<PINB2))) { Counter2++; } \
          if (((PINB&(1<<PINB3))==0)&&((istat&(1<<PINB3))==(1<<PINB3))) { Counter3++; } \
          if (((PINB&(1<<PINB4))==0)&&((istat&(1<<PINB4))==(1<<PINB4))) { Counter4++; } \
          if (((PINB&(1<<PINB0))==0)&&((istat&(1<<PINB0))==(1<<PINB0))) {               \
                /* FIXME: ints off for eeprom-write ?? */ \
                eeprom_update_dword((uint32_t *) (EE_COUNTER_OFFSET+0),Counter1);\
                eeprom_update_dword((uint32_t *) (EE_COUNTER_OFFSET+4),Counter2);\
                eeprom_update_dword((uint32_t *) (EE_COUNTER_OFFSET+8),Counter3);\
                eeprom_update_dword((uint32_t *) (EE_COUNTER_OFFSET+12),Counter4); } \
                eeprom_update_byte((uint8_t *) (EE_OUTPUT_OFFSET),outstate); } \
            istat=PINB; }
          /* FIXME: !!! debounce !!! */

#define INIT_COUNTER_PINS /* Init counter pins */ \
          PCICR=(1<<PCIE0); /* enable PCINT0..7 global */ \
          PORTB |= ( (1<<PB1) | (1<<PB2) ); \
          PORTB |= ( (1<<PB3) | (1<<PB4) ); /* activate internal Pull-Up PB1-4 */ \
          PCMSK0|= ((1<<PCINT1)|(1<<PCINT2)); \
          PCMSK0|= ((1<<PCINT3)|(1<<PCINT4)); /* enable PCINT1-4 PB1-4 */ \
          PCMSK0|= (1<<PCINT0); /* enable PCINT0 PB0 */ \
          DDRB &=~((1<<PINB1)|(1<<PINB2)); \
          DDRB &=~((1<<PINB3)|(1<<PINB4)); /* PB1-4 as input */ \
          DDRB &=~((1<<PINB0)); /* PB0 as input */ \
              istat=PINB; \

/* FIXME: !! nonsense ! just to emulate DS18B20 !!
#define INIT_TEMP   DDRC&=~(1<<PINC0); \
                    ADMUX=(1<<REFS0); \
                    ADCSRA= (1<<ADEN) | (1<<ADPS2) | (1<<ADPS1) | (1<<ADPS0);

#define CONV_TEMP   { uint8_t tc; int16_t sum=0; \
                        for(tc=0;tc<0x10;tc++) { \
                            ADCSRA|=(1<<ADSC)|(1<<ADIF);\
                            while(ADCSRA&(1<<ADSC));\
                            sum=sum+ADC; \
                        } \
                        sum=(sum<<4);  \
                        scratchpad.u16_22 = sum;\
                        scratchpad.u8_24 = ADCL;\
                        scratchpad.u8_25 = ADCH; } \
*/

#endif // __AVR_ATmega48__

#if defined (__AVR_ATtiny24__) || (__AVR_ATtiny44__) || (__AVR_ATtiny84__)
// OW_PORT Pin 5 - PB2

//OW Pin
#define OW_PORT PORTB //1 Wire Port
#define OW_PIN PINB //1 Wire Pin as number
#define OW_PORTN (1<<PINB2)  //Pin as bit in registers
#define OW_PINN (1<<PINB2)
#define OW_DDR DDRB  //pin direction register
#define SET_LOW OW_DDR|=OW_PINN;OW_PORT&=~OW_PORTN;  //set 1-Wire line to low
#define RESET_LOW {OW_DDR&=~OW_PINN;}  //set 1-Wire pin as input
//Pin interrupt
#define EN_OWINT { GIMSK|=(1<<INT0); GIFR=(1<<INTF0); }  //enable interrupt and clear it! GIFR=(1<<INTF0);
#define DIS_OWINT  GIMSK&=~(1<<INT0); sleepmode=SLEEP_MODE_IDLE; //disable interrupt
#define SET_OWINT_RISING MCUCR=(1<<ISC01)|(1<<ISC00);  //set interrupt at rising edge
#define SET_OWINT_FALLING MCUCR=(1<<ISC01);MCUCR&=~(1<<ISC00); //set interrupt at falling edge FIXME/TODO: Check - MM added clearing ISC00 here!
#define SET_OWINT_BOTH MCUCR=(1<<ISC00);MCUCR&=~(1<<ISC01); //set interrupt at both edges
#define SET_OWINT_LOWLEVEL MCUCR&=~((1<<ISC01)|(1<<ISC00)); //set interrupt at low level

#define CHK_INT_EN (GIMSK&(1<<INT0))==(1<<INT0) //test if interrupt enabled
#define PIN_INT ISR(INT0_vect)  // the interrupt service routine
//Timer Interrupt
#define EN_TIMER {TIMSK0 |= (1<<TOIE0); TIFR0|=(1<<TOV0); } //enable timer interrupt
#define DIS_TIMER TIMSK0  &= ~(1<<TOIE0); // disable timer interrupt + IDLE-Sleep?
#define TCNT_REG TCNT0  //register of timer-counter
#define TIMER_INT ISR(TIM0_OVF_vect) //the timer interrupt service routine

#define OWRXLED_PORT PORTA
#define OWRXLED_DDR DDRA
#define OWRXLED_PIN PINA2

#define OUTA_PORT PORTA
#define OUTA_DDR DDRA
#define OUTA_PIN PINA0
#define OUTB_PORT PORTA
#define OUTB_DDR DDRA
#define OUTB_PIN PINA1

/* PWRDOWN: use PORTB / same PORT as counters ! */
#define PWRDOWN_PORT PORTB
#define PWRDOWN_DDR DDRB
#define PWRDOWN_PIN PINB0

#define INIT_LED_PINS OWRXLED_DDR |= (1<<OWRXLED_PIN); /* pins as output */

#define INIT_OUT_PINS OUTA_DDR |= (1<<OUTA_PIN); \
                      OUTB_DDR |= (1<<OUTB_PIN); /* pins as output */ \
                      OUTA_PORT &= ~((outstate&0x01) <<OUTA_PIN); \
                      OUTB_PORT &= ~((outstate&0x02) <<OUTB_PIN);

/*                      OUTA_PIN xxx Init out-Pins AFTER EEPROM-read!
*/

//FIXME / TODO: Double-check timings and move these to EEPROM
#define OWT_MIN_RESET 51
#define OWT_RESET_PRESENCE 4
#define OWT_PRESENCE 20
#define OWT_READLINE 2 //3 for fast master, 4 for slow master and long lines
#define OWT_LOWTIME 3 //3 for fast master, 4 for slow master and long lines

//Initializations of AVR
#define INIT_AVR CLKPR=(1<<CLKPCE); \
                   CLKPR=0; /*8Mhz*/  \
                   TIMSK0=0; \
                   GIMSK=(1<<INT0);  /*set direct GIMSK register*/ \
                   TCCR0B=(1<<CS00)|(1<<CS01); /*8mhz /64 couse 8 bit Timer interrupt every 8us*/ \
                   WDTCSR |= ((1<<WDP2)|(1<<WDP1)|(1<<WDP0)); /* ((1<<WDP2)|(1<<WDP1)|(1<<WDP0)) WDT_ISR every 2s - (1<<WDP3) every 4s */ \
                   WDTCSR |= (1<<WDIE); /* only enable int, no real watchdog */

#define PWRSAVE_AVR ADCSRA &= ~(1<<ADEN); PRR |= (1<<PRTIM1)|(1<<PRUSI)|(1<<PRADC);

#define PC_INT_ISRA ISR(PCINT0_vect) { /*Attiny84 - PAx is PCINT0 */ \
                    if (((PINA&(1<<PINA3))==0)&&((pinstatA&(1<<PINA3))==(1<<PINA3))) { Counter2++; }       \
                    if (((PINA&(1<<PINA4))==0)&&((pinstatA&(1<<PINA4))==(1<<PINA4))) { Counter3++; }       \
                    if (((PINA&(1<<PINA5))==0)&&((pinstatA&(1<<PINA5))==(1<<PINA5))) { Counter4++; }       \
                    pinstatA=PINA;}    \

#define PC_INT_ISRB ISR(PCINT1_vect) { /*Attiny84 - PBx is PCINT1 */ \
                    if (((PINB&(1<<PINB1))==0)&&((pinstatB&(1<<PINB1))==(1<<PINB1))) { Counter1++; }       \
                    if (((PINB&(1<<PINB0))==0)&&((pinstatB&(1<<PINB0))==(1<<PINB0))) {               \
                          /* FIXME: ints off for eeprom-write ?? eeprom own routines to save much space? */ \
                          eeprom_update_dword((uint32_t *) (EE_COUNTER_OFFSET+0),Counter1);\
                          eeprom_update_dword((uint32_t *) (EE_COUNTER_OFFSET+4),Counter2);\
                          eeprom_update_dword((uint32_t *) (EE_COUNTER_OFFSET+8),Counter3);\
                          eeprom_update_dword((uint32_t *) (EE_COUNTER_OFFSET+12),Counter4); \
                          eeprom_update_byte((uint8_t *) (EE_OUTPUT_OFFSET),outstate); } \
                    pinstatB=PINB;}    \

#define INIT_COUNTER_PINS /* Counter Interrupt */\
                        GIMSK|=(1<<PCIE0);\
                        GIMSK|=(1<<PCIE1);\
                        PCMSK1|=(1<<PCINT9); /* PB1 PCINT */ \
                        PCMSK1|= (1<<PCINT8); /* enable PCINT8 PB0 */ \
                        DDRB &=~((1<<PINB1));  /* Counter-pins as input */ \
                        DDRB &=~((1<<PINB0)); /* PB0 as input */ \
                        pinstatB=PINB; \
                        PCMSK0|=((1<<PCINT3)|(1<<PCINT4)|(1<<PCINT5)); /* PA3-5 PCINT */ \
                        DDRA &=~((1<<PINA3)|(1<<PINA4)|(1<<PINA5));  /* Counter-pins PA3-5 as input */ \
                        pinstatA=PINA; \
                        if (pullupcfg) { \
                          PORTA |= ( (1<<PA3)|(1<<PA4)|(1<<PA5) ); /* activate internal Pull-Up PA3-5 */ \
                          PORTB |= ( (1<<PB1) ); /* activate internal Pull-Up PB1 */ \
                        } \


#endif // __AVR_ATtiny84__

//States / Modes (defines from original owslave.c - new are all called OWC_ !)
#define OWM_SLEEP 0  //Waiting for next reset pulse
#define OWM_RESET 1  //Reset pulse received
#define OWM_PRESENCE 2  //sending presence pulse
#define OWM_READ_COMMAND 3 //read 8 bit of command
#define OWM_SEARCH_ROM 4  //SEARCH_ROM algorithms
#define OWM_MATCH_ROM 5  //test number
#define OWM_CHK_RESET 8  //waiting of rising edge from reset pulse
#define OWM_GET_ADRESS 6
#define OWM_READ_MEMORY_COUNTER 7
#define OWM_WRITE_SCRATCHPAD 9
#define OWM_READ_SCRATCHPAD 10

#define OWM_WRITE_PAGE_TO_MASTER 11
#define OWM_WRITE_FUNC 12

#define OWC_READ_SCRATCHPAD 0xBE
#define OWC_WRITE_SCRATCHPAD 0x4E
#define OWC_WRITE_FUNC 0x4F
/* READ_SCRATCHPAD 0xBE + 0xYY is adaptec from DS2438! BE + Address
 *
 */

//Write a bit after next falling edge from master
//its for sending a zero as soon as possible
#define OWW_NO_WRITE 2
#define OWW_WRITE_1 1
#define OWW_WRITE_0 0


volatile uint32_t uptime = 0; /* holds uptime in 1/2 seconds - overflows after 6.8 years */

typedef union {
    volatile uint8_t bytes[11];
    struct {
      uint8_t   page1;
      uint8_t   u8_11;
      uint8_t   u8_12;
      uint16_t  u16_13;
      uint32_t  u32_14;
      uint8_t   crc1;
    };
    struct {
      uint8_t   page2;
      uint16_t  u16_21;
      uint16_t  u16_22;
      uint16_t  u16_23;
      uint8_t   u16_24;
      uint8_t   crc2;
    };
    struct {
      uint8_t   page3;
      uint32_t  u32_31;
      uint32_t  u32_32;
      uint8_t   crc3;
    };
    struct {
      uint8_t   page4;
      uint8_t   u8_41;
      uint8_t   u8_42;
      uint8_t   u8_43;
      uint8_t   u8_44;
      uint32_t   u32_4XX;
      uint8_t   crc4;
    };
} scratchpad_t;
scratchpad_t scratchpad;

/* recv-states */
enum {
      S_NULL = 0,
      S_STX = 2,
      S_ETX = 3,
      S_ACK = 6,
      S_NACK = 15,
      S_DATA = 20,
};
uint8_t recv_state = S_NULL;
uint8_t nackmsg=0;

volatile uint16_t scrc; //CRC calculation
volatile uint8_t page; /* address of memory-page to read/write */

volatile uint8_t lastcps;
volatile uint32_t Counter1;
volatile uint32_t Counter2;
volatile uint32_t Counter3;
volatile uint32_t Counter4;
volatile uint8_t pinstatA;
volatile uint8_t pinstatB;
volatile uint8_t sleepmode;

volatile uint8_t cbuf; //Input buffer for a command
uint8_t owid[8] = {0xE1, 0xE2, 0x00, 0x00, 0x00, 0x00, 0x84, 0x28 };

volatile uint8_t bitp;  //pointer to current Byte
volatile uint8_t bytep; //pointer to current Bit

volatile uint8_t mode; //state
volatile uint8_t wmode; //if 0 next bit that send the device is  0
volatile uint8_t actbit; //current
volatile uint8_t srcount; //counter for search rom

/* temp vars to avoid eeprom-reading - in case of low-mem: FIXME */
uint16_t version = 0x0106;
uint8_t stype = 1;
uint8_t pullupcfg = 1;
uint8_t outstate = 0;
uint16_t rcnt = 1;
uint8_t eflag; //internal error/status-flag
volatile uint8_t crcerrcnt=0;
volatile uint8_t ewrite_flag;//ewrite_flag is already volatile and used as semaphore..
typedef union {
    uint8_t bytes[8];
    struct {
      uint32_t u32_1;
      uint32_t u32_2;
    };
} eewrite_t;
eewrite_t eewrite_buf;

PIN_INT {
    uint8_t lwmode=wmode;  //let this variables in registers
    uint8_t lmode=mode;
    if (lwmode==OWW_WRITE_0) { //if necessary set 0-Bit
        SET_LOW;
        lwmode=OWW_NO_WRITE;
    }
    DIS_OWINT; //disable interrupt, only in OWM_SLEEP mode it is active
    sleepmode=SLEEP_MODE_IDLE; //powerdown is set in TIMER_INT on OWM_SLEEP only!
    switch (lmode) {
        case OWM_SLEEP:
            TCNT_REG=~(OWT_MIN_RESET);
            //RESET_LOW;  //??? Set pin as input again ???
            EN_OWINT; SET_OWINT_RISING; //other edges ?
            OWRXLED_PORT &= ~(1<<OWRXLED_PIN); /* led off */
            break;
        //start of reading a byte with falling edge from master, reading closed in timer isr
        case OWM_MATCH_ROM:  //falling edge wait for receive
        case OWM_WRITE_SCRATCHPAD:
        case OWM_GET_ADRESS:
        case OWM_READ_COMMAND:
        case OWM_WRITE_FUNC:
            TCNT_REG=~(OWT_READLINE); //wait a time for reading
            break;
        case OWM_SEARCH_ROM:   //Search algorithm waiting for receive or send
            if (srcount<2) { //this means bit or complement is writing,
                TCNT_REG=~(OWT_LOWTIME);
            } else
                TCNT_REG=~(OWT_READLINE);  //init for read answer of master
            break;
        case OWM_READ_SCRATCHPAD:
        case OWM_WRITE_PAGE_TO_MASTER:
            TCNT_REG=~(OWT_LOWTIME);
            break;
        case OWM_CHK_RESET:  //rising edge of reset pulse
            SET_OWINT_FALLING;
            TCNT_REG=~(OWT_RESET_PRESENCE);  //waiting for sending presence pulse
            lmode=OWM_RESET;
            break;
    }
    EN_TIMER;
    mode=lmode;
    wmode=lwmode;
}

TIMER_INT {
    uint8_t lwmode=wmode; //let this variables in registers
    uint8_t lmode=mode;
    uint8_t lbytep=bytep;
    uint8_t lbitp=bitp;
    uint8_t lsrcount=srcount;
    uint8_t lactbit=actbit;
    uint16_t lscrc=scrc;
    //Ask input line sate
    uint8_t p=((OW_PIN&OW_PINN)==OW_PINN);

    OWRXLED_PORT ^= (1<<OWRXLED_PIN); /* toolge RX-led */

    //Interrupt still active ?
    if (CHK_INT_EN) {
        //maybe reset pulse
        if (p==0) {
            lmode=OWM_CHK_RESET;  //wait for rising edge
            SET_OWINT_RISING;
        }
        DIS_TIMER;
    } else
    switch (lmode) {
        case OWM_RESET:  //Reset pulse and time after is finished, now go in presence state
            lmode=OWM_PRESENCE;
            SET_LOW;
            TCNT_REG=~(OWT_PRESENCE);
            DIS_OWINT;  //No Pin interrupt necessary only wait for presence is done
            break;
        case OWM_PRESENCE:
            RESET_LOW;  //Presence is done now wait for a command
            lmode=OWM_READ_COMMAND;
            cbuf=0;lbitp=1;  //Command buffer have to set zero, only set bits will write in
            break;
        case OWM_READ_COMMAND:
            if (p) {  //Set bit if line high
                cbuf|=lbitp;
            }
            lbitp=(lbitp<<1);
            if (!lbitp) { //8-Bits read - weird syntax?
                lbitp=1;
                switch (cbuf) {
                    case 0x55://Match ROM
                        lbytep=0;
                        lmode=OWM_MATCH_ROM;
                        break;
                    case 0xF0:  //initialize search rom
                        lmode=OWM_SEARCH_ROM;
                        lsrcount=0;
                        lbytep=0;
                        lactbit=(owid[lbytep]&lbitp)==lbitp; //set actual bit
                        lwmode=lactbit;  //prepare for writing when next falling edge
                        break;
                    //FIXME: case 0xEC:  //alarm search rom - TODO
                    case 0xBE: //read scratchpad
                        lmode=OWM_GET_ADRESS; //first the master sends an address
                        lbytep=0;
                        page=0;
                        //EN_OWINT; SET_OWINT_LOWLEVEL; sleepmode=SLEEP_MODE_PWR_DOWN; //Testing if we can charge-up?
                        break;
                    case OWC_WRITE_FUNC:
                        lmode=OWM_WRITE_FUNC; //first the master sends an address(page/func), then 8bytes + crc
                        lbytep=0;
                        lscrc=0;
                        scratchpad.page1=0;
                        break;
                    default:
                        LSL("\r\nDC:")
                        //LVH(cbuf)
                        lmode=OWM_SLEEP;  //all other commands do nothing
                }
            }
            break;
        case OWM_SEARCH_ROM:
            RESET_LOW;  //Set low also if nothing send (branch takes time and memory)
            lsrcount++;  //next search rom mode
            switch (lsrcount) {
                case 1:lwmode=!lactbit;  //preparation sending complement
                    break;
                case 3:
                    if (p!=(lactbit==1)) {  //check master bit
                        lmode=OWM_SLEEP;  //not the same go sleep
                    } else {
                        lbitp=(lbitp<<1);  //prepare next bit
                        if (lbitp==0) {
                            lbitp=1;
                            lbytep++;
                            if (lbytep>=8) {
                                lmode=OWM_SLEEP;  //all bits processed
                                break;
                            }
                        }
                        lsrcount=0;
                        lactbit=(owid[lbytep]&lbitp)==lbitp;
                        lwmode=lactbit;
                    }
                    break;
            }
            break;
        case OWM_MATCH_ROM:
            if (p==((owid[lbytep]&lbitp)==lbitp)) {  //Compare with ID Buffer
                lbitp=(lbitp<<1);
                if (!lbitp) {
                    lbytep++;
                    lbitp=1;
                    if (lbytep>=8) {
                        lmode=OWM_READ_COMMAND;  //same? get next command
                        cbuf=0;
                        break;
                    }
                }
            } else {
                lmode=OWM_SLEEP;
            }
            break;
        case OWM_GET_ADRESS:
            if (p) { //Get the Address for reading
                page|=lbitp;
            }
            lbitp=(lbitp<<1);
            if (!lbitp) {
                lbytep++;
                lbitp=1;
                if (lbytep==1) {
                    lmode=OWM_WRITE_PAGE_TO_MASTER;
                    lbytep=0;lscrc=0; //from first position
                    memset( &scratchpad.bytes, 0, 10 );
                    switch (page) {
                      case 0:
                        scratchpad.page1 = page;
                        scratchpad.u8_11 = stype;
                        scratchpad.u8_12 = eflag;
                        scratchpad.u16_13 = version;
                        scratchpad.u32_14 = uptime;
                        break;
                      case 1:
                        scratchpad.page1 = page;
                        scratchpad.u16_21 = rcnt;
                        //scratchpad.u16_22 = ADC1
                        //scratchpad.u16_23 = ADC2
                        scratchpad.u16_24 = crcerrcnt; //temp/debug
                        //scratchpad.u16_24 = freeRam;
                        break;
                      case 2:
                        scratchpad.page1 = page;
                        scratchpad.u32_31 = Counter1;
                        scratchpad.u32_32 = Counter2;
                        break;
                      case 3:
                        scratchpad.page1 = page;
                        scratchpad.u32_31 = Counter3;
                        scratchpad.u32_32 = Counter4;
                        break;
                      case 4:
                        scratchpad.page1 = page;
                        //pin-input-state
                        //FIXME: make 1 byte with relevant pins only
                        scratchpad.u8_41 = pinstatA;
                        scratchpad.u8_42 = pinstatB;
                        scratchpad.u8_43 = pullupcfg;
                        scratchpad.u8_44 = outstate;
                        break;
                      case 5:
                        scratchpad.page1 = page;
                        //FIXME: untested, might be broken!
                        //FIXME: make 1 byte with relevant pins only
                        scratchpad.u8_41 = outstate;
                        break;
                      case 9:
                        scratchpad.page1 = page;
                        scratchpad.u32_31 = eeprom_read_dword((const uint32_t *) (EE_LABEL_OFFSET+0));
                        scratchpad.u32_32 = eeprom_read_dword((const uint32_t *) (EE_LABEL_OFFSET+4));
                        break;
                      case 10:
                        scratchpad.page1 = page;
                        scratchpad.u32_31 = eeprom_read_dword((const uint32_t *) (EE_LABEL_OFFSET+8));
                        scratchpad.u32_32 = eeprom_read_dword((const uint32_t *) (EE_LABEL_OFFSET+12));
                        break;
                      default:
                        scratchpad.page1 = page;
                        scratchpad.u8_11 = 0xff;
                        scratchpad.u8_12 = page; //this is actually an error! page unknown
                        break;
                    }
                    lactbit=(lbitp&scratchpad.bytes[0])==lbitp;
                    lwmode=lactbit; //prepare for send firs bit
                    break;
                } else page=0; // never happens, should be page[lbytepos]
            }
            break;
        case OWM_WRITE_FUNC:
            if (p) {
                scratchpad.bytes[lbytep]|=lbitp;
            }
            /* Page(function) is part of CRC! */
            if ((lscrc&1)!=p) lscrc=(lscrc>>1)^0x8c; else lscrc >>=1;
            lbitp=(lbitp<<1);
            if (!lbitp) {
                lbytep++;
                lbitp=1;
                if (lbytep==10) {
                    /* now process received Write-function(s) if crc matches */
                    if (scratchpad.bytes[9] != scratchpad.bytes[10])
                      crcerrcnt++;
                    else {
                      switch (scratchpad.bytes[0]) { /* "page" or function-id */
                        case 1: /* reset-functions */
                          if (scratchpad.bytes[1] == 1) /* reset counterX */ {
                            Counter1=0;Counter2=0;Counter3=0;Counter4=0;
                          }
                          break;
                        case 16:
                          Counter1 = scratchpad.u32_31;
                          break;
                        case 20:
                          Counter2 = scratchpad.u32_31;
                          break;
                        case 24:
                          Counter3 = scratchpad.u32_31;
                          break;
                        case 28:
                          Counter4 = scratchpad.u32_31;
                          break;
                        case 34:
                          //Pullups XXX = scratchpad.u8_11;
                          //FIXME: Bitwise! each separate
                          if (scratchpad.bytes[1] == 0) {
                            PORTB &= ~( (1<<PB1) ); /* disable internal Pull-Up PB1 */ \
                            PORTA &= ~( (1<<PA3)|(1<<PA4)|(1<<PA5) ); /* disable internal Pull-Up PA3-5 */ \
                          } else if (scratchpad.bytes[1] == 1) {
                            PORTB |= ( (1<<PB1) ); /* activate internal Pull-Up PB1 */ \
                            PORTA |= ( (1<<PA3)|(1<<PA4)|(1<<PA5) ); /* activate internal Pull-Up PA3-5 */ \
                          }
                          pullupcfg = scratchpad.bytes[1];
                          eeprom_write_byte((uint8_t *) (EE_PULLUP_OFFSET+0), scratchpad.bytes[1]);
                          break;
                        case 35:
                          /* TODO / FIXME: Set output ports/Check! */
                          outstate = scratchpad.bytes[1]; /* EEprom-save is done on pwrdown only! */
                          /* Quirky FIXME */
                          if (outstate & 0x01)
                            OUTA_PORT |= (1 <<OUTA_PIN);
                          else
                            OUTA_PORT &= ~(1 <<OUTA_PIN);
                          if (outstate & 0x02)
                            OUTB_PORT |= (1 <<OUTB_PIN);
                          else
                            OUTB_PORT &= ~(1 <<OUTB_PIN);
                          break;
                        case 72:
                        case 76:
                        case 80:
                        case 84:
                          if (ewrite_flag > 0) //last write still pending!
                            break;
                          eewrite_buf.u32_1 = scratchpad.u32_31;
                          ewrite_flag = (scratchpad.page1) - 72 + EE_LABEL_OFFSET;
                          break;
                        default:
                          if (scratchpad.page1 > 71 && scratchpad.page1 < 72+EE_LABEL_MAXLEN)
                            ; //alternatively: just write it?
                          break;
                      }
                    }
                    lmode=OWM_SLEEP;
                    break;
                } else scratchpad.bytes[lbytep]=0;
                if (lbytep==9) {
                    //copy calculated CRC to last scratchpad-byte as we receive it with next byte!
                    scratchpad.bytes[10] = lscrc;
                }
            }
            break;
        case OWM_WRITE_PAGE_TO_MASTER:
            RESET_LOW;
            if ((lscrc&1)!=lactbit) lscrc=(lscrc>>1)^0x8c; else lscrc >>=1;
            lbitp=(lbitp<<1);
            if (!lbitp) {
                lbytep++;
                lbitp=1;
                if (lbytep>=10) {
                    lmode=OWM_SLEEP;
                    break;
                } else if (lbytep==9) scratchpad.bytes[9]=lscrc;
            }
            lactbit=(lbitp&scratchpad.bytes[lbytep])==lbitp;
            lwmode=lactbit;
            break;
        }
        if (lmode==OWM_SLEEP) {
          //RESET_LOW;  //??? Set pin as input again ???
          DIS_TIMER;
          EN_OWINT; SET_OWINT_LOWLEVEL;
          sleepmode=SLEEP_MODE_PWR_DOWN; //sleep deep
          OWRXLED_PORT &= ~(1<<OWRXLED_PIN); /* led off */
        } else {
          //sleepmode=SLEEP_MODE_IDLE; //no sleep
        }

        if (lmode!=OWM_PRESENCE)  {
            TCNT_REG=~(OWT_MIN_RESET-OWT_READLINE);  //OWT_READLINE around OWT_LOWTIME
            EN_OWINT;
        }
        mode=lmode;
        wmode=lwmode;
        bytep=lbytep;
        bitp=lbitp;
        srcount=lsrcount;
        actbit=lactbit;
        scrc=lscrc;
}

#ifdef PC_INT_ISR
PC_INT_ISR  //for counting  defined for specific device
#elif defined PC_INT_ISRA
PC_INT_ISRA
PC_INT_ISRB
#endif

void init_eeprom(void) {
    /* check magic, read slave address and counter values, resetcount, init-name, */
    if (eeprom_read_word((const uint16_t *) (EE_MAGIC_OFFSET+0)) == EE_MAGIC_NUMBER) {
      //EEPROM valid -> read counters & settings
      for (uint16_t i=EE_OWID_OFFSET;i<EE_OWID_OFFSET+8;i++)
        owid[i-EE_OWID_OFFSET] = eeprom_read_byte((uint8_t *) i);
      rcnt = eeprom_read_word((const uint16_t *) (EE_RCNT_OFFSET+0)) + 1;
      eeprom_update_word((uint16_t *) (EE_RCNT_OFFSET+0), rcnt);
      //FIXME: this eats flashmem!
      Counter1 = eeprom_read_dword((const uint32_t *) (EE_COUNTER_OFFSET+0));
      Counter2 = eeprom_read_dword((const uint32_t *) (EE_COUNTER_OFFSET+4));
      Counter3 = eeprom_read_dword((const uint32_t *) (EE_COUNTER_OFFSET+8));
      Counter4 = eeprom_read_dword((const uint32_t *) (EE_COUNTER_OFFSET+12));
      //FIXME: read/write if counting on falling/rising-edge ?
      version = eeprom_read_word((const uint16_t *) (EE_VERSION_OFFSET+0));
      stype = eeprom_read_byte((uint8_t *) (EE_TYPE_OFFSET+0));
      pullupcfg = eeprom_read_byte((uint8_t *) (EE_PULLUP_OFFSET+0));
      outstate = eeprom_read_byte((uint8_t *) (EE_OUTPUT_OFFSET+0));
    } else {
      //Init values
      /* should cli(); here no sei(); yet enabled in main.. */
      eeprom_write_word((uint16_t *) (EE_MAGIC_OFFSET+0), EE_MAGIC_NUMBER);
      for (uint16_t i=EE_OWID_OFFSET;i<EE_OWID_OFFSET+8;i++)
        eeprom_write_byte((uint8_t *) i,owid[i-EE_OWID_OFFSET]);
      eeprom_write_word((uint16_t *) (EE_RCNT_OFFSET+0), rcnt);
      //FIXME: this eats flashmem!
      eeprom_write_dword((uint32_t *) (EE_COUNTER_OFFSET+0),0);
      eeprom_write_dword((uint32_t *) (EE_COUNTER_OFFSET+4),0);
      eeprom_write_dword((uint32_t *) (EE_COUNTER_OFFSET+8),0);
      eeprom_write_dword((uint32_t *) (EE_COUNTER_OFFSET+12),0);
      eeprom_write_byte((uint8_t *) (EE_TYPE_OFFSET+0), stype);
      eeprom_write_word((uint16_t *) (EE_VERSION_OFFSET+0), version);
      eeprom_write_byte((uint8_t *) (EE_PULLUP_OFFSET+0), 1);
      eeprom_write_byte((uint8_t *) (EE_OUTPUT_OFFSET+0), 0);
    }
}

//FIXME: enable real watchdog?
ISR(WDT_vect) {
  uptime += 2;
}

int main(void) {
    mode=OWM_SLEEP;
    wmode=OWW_NO_WRITE;
    OW_DDR&=~OW_PINN;

    INIT_AVR
    PWRSAVE_AVR

    init_eeprom();

    INIT_COUNTER_PINS
    INIT_LED_PINS
    INIT_OUT_PINS

    SET_OWINT_FALLING;
    DIS_TIMER;
    EN_OWINT;

    sei();
    //force sleep first
    DIS_TIMER;
    EN_OWINT; SET_OWINT_LOWLEVEL;
    sleepmode=SLEEP_MODE_PWR_DOWN;

    while(1){
        if (ewrite_flag) {
          eeprom_update_dword((uint32_t *) (ewrite_flag+0), eewrite_buf.u32_1);
          ewrite_flag = 0;
        }
        OWRXLED_PORT &= ~(1<<OWRXLED_PIN); /* led off */
        sleep_enable();
        set_sleep_mode(sleepmode);
        sleep_cpu();
    }
}