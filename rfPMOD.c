/*===================================CPEG222====================================
 * Program:		RFTransciever
 * Authors: 	Brandon Brooks
 * Date: 		9/12/2019
 * Description: Project 1 
 * The program waits for a BTN press or SWT with the following results:
 * -BTNC turns on the next LED and turns off current one from LD0 - LD7
 * Input: Button press or Switch
 * Output: LED is turned on and off one by one.
==============================================================================*/
/*------------------ Board system settings. PLEASE DO NOT MODIFY THIS PART ----------*/
#ifndef _SUPPRESS_PLIB_WARNING          //suppress the plib warning during compiling
    #define _SUPPRESS_PLIB_WARNING      
#endif
#pragma config FPLLIDIV = DIV_2         // PLL Input Divider (2x Divider)
#pragma config FPLLMUL = MUL_20         // PLL Multiplier (20x Multiplier)
#pragma config FPLLODIV = DIV_1         // System PLL Output Clock Divider (PLL Divide by 1)
#pragma config FNOSC = PRIPLL           // Oscillator Selection Bits (Primary Osc w/PLL (XT+,HS+,EC+PLL))
#pragma config FSOSCEN = OFF            // Secondary Oscillator Enable (Disabled)
#pragma config POSCMOD = XT             // Primary Oscillator Configuration (XT osc mode)
#pragma config FPBDIV = DIV_8           // Peripheral Clock Divisor (Pb_Clk is Sys_Clk/8)
/*----------------------------------------------------------------------------*/
     
#include <xc.h>   //Microchip XC processor header which links to the PIC32MX370512L header
#include <p32xxxx.h>
#include <plib.h>
#include "config.h" // Basys MX3 configuration header
#include "spija.h"
#include "radioAddress.h"
#include "MRF24J40.h"
#include <stdio.h>
#include "uart.h"

UINT8 txPayload[TX_PAYLOAD_SIZE];		// TX payload buffer

// inits Tx structure for simple point-to-point connection between a single pair of devices who both use the same address
// after calling this, you can send packets by just filling out:
// txPayload[] with payload and
// Tx.payloadLength,
// then calling RadioTXPacket()

/*----------------------------Functions---------------------------------------*/
void RadioInitP2P(void)
{
	Tx.frameType = PACKET_TYPE_DATA;
	Tx.securityEnabled = 0;
	Tx.framePending = 0;
	Tx.ackRequest = 0;
	Tx.panIDcomp = 1;
	Tx.dstAddrMode = SHORT_ADDR_FIELD;
	Tx.frameVersion = 0;
	Tx.srcAddrMode = NO_ADDR_FIELD;
	Tx.dstPANID = RadioStatus.MyPANID;
	Tx.dstAddr = RadioStatus.MyShortAddress;
	Tx.payload = txPayload;
}
void delay_ms(int ms);
void configLEDS(void);



/*--------------------------Variables-----------------------------------------*/
int alternator=0;
int buttonLock=0;
int password=40;
int flag=0;

/* -------------------------- Definitions------------------------------------ */
#define SYS_FREQ    (80000000L) // 80MHz system clock
#define tris_BTNC   TRISFbits.TRISF0
#define BTNC    PORTFbits.RF0
#define  LED0  LATAbits.LATA0
#define  LED1  LATAbits.LATA1
#define  LED2  LATAbits.LATA2
#define LED3 LATAbits.LATA3
#define LED4 LATAbits.LATA4
#define tris_BTNC   TRISFbits.TRISF0
#define BTNC    PORTFbits.RF0

int main(void) 
{
    configLEDS();
    UART_Init(9600); 
    UART_PutString("UART Demo \n\r");
    
    tris_PMODS_JA8 = 0x0; //set as output
    lat_PMODS_JA8 = 1; //set reset to high...so off
    //rp_PMODS_JA8 =  RPG7R;
    
    tris_PMODS_JA9 = 0x0; 
    //rp_PMODS_JA9   RPG8R
    lat_PMODS_JA9 = 0;    
    
    SPIJA_Init();
    RadioInit();			// cold start MRF24J40 radio
	RadioInitP2P();			// setup for simple peer-to-peer communication

    int lastFrameNumber = 0;
    char buffer[2000];
    int iterator = 0;
    int i = 0;
    LED4=1;
    
    
    
    tris_PMODS_JA7 = 0x1;
    
    
    CNCONCbits.ON=1;
    CNENC = 0b1000;
    CNPDC = 0b1000;
    //CNPUC = 0x0;
    
    PORTC;
    IPC8bits.CNIP = 6;
    IPC8bits.CNIS = 3;
    
    IFS1bits.CNCIF=0;
    IEC1bits.CNCIE=1;
    INTEnableSystemMultiVectoredInt();
    
    
    
    while(1)				// main program loop
    {
		// process any received packets
        

        
		while(RadioRXPacket())         //RadioRXPacket()        BEING SKIPPED OVER     GETTING STUCK HERE
		{
            //sprintf(buffer, "RxPacketCount inside if %d\n\r", RadioStatus.RXPacketCount);
            //UART_PutString(buffer);
            sprintf(buffer, "%d\n\r", Rx.payloadLength);
            UART_PutString(buffer);
            //sprintf(buffer, "RxPayload is \n\r", *Rx.payload);
            //UART_PutString(buffer);
            int i = 0;
            for(i = 0; i < Rx.payloadLength; i++) {
                //sprintf(buffer, "%d\n\r", Rx.payload[i]);
                //UART_PutString(buffer);
                if(Rx.payload[i]==password){
                    LED3=1;
                    //sprintf(buffer, "SUCCESS\n\r");
                    //UART_PutString(buffer);
                }
            }
            //sprintf(buffer, "TEST HERE %d\n\r", iterator++);
            //UART_PutString(buffer);
			if (Rx.frameNumber != lastFrameNumber)				// skip duplicate packets (Usually because far-end missed my ACK)
			{
                //sprintf(buffer, "Framenumber == lastfraenumber \n\r");
                //UART_PutString(buffer);
				lastFrameNumber = Rx.frameNumber;
		
				Rx.payload[Rx.payloadLength] = 0;				// put terminating null on received payload
				//printf("%s", Rx.payload);						// print payload as an ASCII string
			}
            if(*Rx.payload>=0){           //NEED TO TEST INCOMING PACKET
                LED0=1;
            }
            alternator++;
            LED2=1;
            alternator= alternator%2==0 ? 0:1;              //if alternator%2==0 then alternator=0
            
			RadioDiscardPacket();
		}

		//Tx.payloadLength = sprintf(Tx.payload, "This is message A for Alpha.\n" );
		//RadioTXPacket();
        
           if(BTNC&&!buttonLock){
               Tx.payloadLength=50;
               //*Tx.payload=810;
               UINT8 j=0;
               for(j;j<10;j++){
                   txPayload[j]=2*j;
               }
               Tx.dstAddr=0xaa55;
               alternator++;
               alternator= alternator%2==0 ? 0:1;  
               LED1=alternator;
               RadioTXPacket();
               RadioTXResult();
               //RadioStatus.TX_BUSY = 0;
               buttonLock=1;
            }
         if(buttonLock&&!BTNC){
             buttonLock=0;
             delay_ms(50);
        }
		
        
        //printf("hello %d\n", iterator++);
        

		
	}	
}



void configLEDS(){
    LED0=0;
    LED1=0;
    LED2=0;
    LED3=0;
    LED4=0;
    TRISAbits.TRISA0 = 0; //Configure ports of LEDS to outputs for use
    TRISAbits.TRISA1 = 0;
    TRISAbits.TRISA2=0;
    TRISAbits.TRISA3=0;
    TRISAbits.TRISA4=0;
    DDPCONbits.JTAGEN = 0; //Statement is required to use pin RA0 as I/O
    tris_BTNC = 1;//Makes button C an input
}

void delay_ms(int ms){
	int		i,counter;
	for (counter=0; counter<ms; counter++){
        for(i=0;i<1426;i++){}   //software delay 1 millisec
    }
}
