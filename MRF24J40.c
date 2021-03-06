// Driver for Microchip MRF24J40 802.15.4 radio hardware
// Originally based on Microchip MiWi DE v.3.1.3, 5/28/2010 (c) Microchip

#include <string.h>			// memset()
#include <stdio.h>
#include <stdlib.h>
//#include "debug.h"			// debug status
//#include "hardware.h"
#include "MRF24J40.h"
#include "radioAddress.h"	// addr for radio
#include "spija.h"
#include "config.h"


#define HARDWARE_SPI							// vs. software bit-bang (slower)
#define BYTEPTR(x)			((UINT8*)&(x))		// converts x to a UINT8* for bytewise access ala x[foo]


#define FOSC				(20000000)			// PIC32 cpu clock speed, Hz
#define ONE_SECOND (FOSC/2)						// 1s of PIC32 core timer ticks (== Hz)
#define MS_TO_CORE_TICKS(x) ((UINT64)(x)*ONE_SECOND/1000)
#define CT_TICKS_SINCE(tick) (ReadCoreTimer() - (tick))								// number of core timer ticks since "tick"

// globals

MRF24J40_STATUS volatile RadioStatus;						// radio state
UINT8 volatile RXBuffer[PACKET_BUFFERS][RX_BUFFER_SIZE];	// rx packet buffers 
PACKET Tx, Rx;												// structures describing transmitted and received packets
char buffer[256];

// this combines memcpy with incrementing the source point.  It copies bytewise, allowing it to copy to/from unaligned addresses
unsigned char* readBytes(unsigned char* dstPtr, unsigned char* srcPtr, unsigned int count)
{
	while(count--)
		*dstPtr++ = *srcPtr++;

	return srcPtr;
}

/* The key to understanding SPI is that there is only 1 clock line, and so all transfers
   are always bidirectional - you send one bit for each you receive and vice-versa.  And the CLK
   only runs (in Master mode) when you transmit.

   So to transmit, you store to the TX buffer, wait for it to clock out, flush away the bogus received byte.

   And to receive, you send, wait for the byte to clock in, then read it.

*/



void spiPut(unsigned char v)				// write 1 byte to SPI
{
	unsigned char i;

    #ifdef HARDWARE_SPI

		#ifdef SPI_INTERRUPTS

			ByteQueueStore(&SPITxQueue, v);

			if (SPI2STATbits.SPITBE) 		// if SPI tx is not busy
				INTSetFlag(INT_SPI2TX);		// kick it
		#else

			while(!SPI2STATbits.SPITBE); 	// wait for TX buffer to empty (should already be)
			SPI2BUF=v;						// write byte to TX buffer

			while(!SPI2STATbits.SPIRBF);	// wait for RX buffer to fill
			i=SPI2BUF;						// read RX buffer (don't know why we need to do this here, but we do)

		#endif 
	#else
        RADIO_CLK = 0;

        for(i = 0; i < 8; i++)
        {
            RADIO_TX = (v >> (7-i));
            RADIO_CLK = 1;
            RADIO_CLK = 0;
        }
    #endif
    

}

unsigned char spiGet(void)							// read 1 byte from SPI
{
    #ifdef HARDWARE_SPI
		
		#ifdef SPI_INTERRUPTS 

			while(!SPI2STATbits.SPITBE); 			// wait for SPI to go idle
			SPIRxQueue.read = 0;
			SPIRxQueue.write = 0;					// empty RX queue

			ByteQueueStore(&SPITxQueue, 0);			// force clock to run

			if (SPI2STATbits.SPITBE) 				// if SPI tx is not buxy
				INTSetFlag(INT_SPI2TX);				// kick it

			while (ByteQueueEmpty(&SPIRxQueue));	// wait for RX byte to come in
			
			return ByteQueueFetch(&SPIRxQueue);
					

		#else
			while(!SPI2STATbits.SPITBE); 			// wait for TX buffer to empty
			SPI2BUF=0x00;							// write to TX buffer (force CLK to run for TX transfer)

			while(!SPI2STATbits.SPIRBF);			// wait for RX buffer to fill
			return(SPI2BUF);						// read RX buffer
		#endif
	#else
        unsigned char i;
        unsigned char spidata = 0;

        RADIO_TX = 0;
        RADIO_CLK = 0;

        for(i = 0; i < 8; i++)
        {
            spidata = (spidata << 1) | RADIO_RX;
            RADIO_CLK = 1;
            RADIO_CLK = 0;
        }

        return spidata;
    #endif
}

// reads byte from radio at long "address"
UINT8 highRead(UINT16 address)
{
	UINT8 toReturn;
    
    #ifndef SPI_INTERRUPTS
	//UINT8 tmpRFIE = RFIE;
	//RFIE = 0;										// disable radio ints during communication
	//RADIO_CS = 0;									// select radio SPI bus
    lat_SPIJA_CE = 0;
#endif

    spiPut((((UINT8)(address>>3))&0x7F)|0x80);
    spiPut((((UINT8)(address<<5))&0xE0));
	//spiPut(((address>>3)&0x7F)|0x80);
	//spiPut(((address<<5)&0xE0));
	toReturn = spiGet();
    
    #ifndef SPI_INTERRUPTS
	//RADIO_CS = 1;									// de-select radio SPI bus
	//RFIE = tmpRFIE;									// restore interrupt state
    lat_SPIJA_CE = 1;
#endif


	return toReturn;
}

// writes "value" to radio at long "address"
void highWrite(UINT16 address, UINT8 value)
{

#ifndef SPI_INTERRUPTS
	//UINT8 tmpRFIE = RFIE;
	//RFIE = 0;										// disable radio ints during communication
	//RADIO_CS = 0;									// select radio SPI bus
    lat_SPIJA_CE = 0;
#endif
    //sprintf(buffer, "Address %x \n\r", (address));
    //UART_PutString(buffer);
	spiPut((((UINT8)(address>>3))&0x7F)|0x80);
    //sprintf(buffer, "First SPI put %x \n\r", ((((UINT8)(address>>3))&0x7F)|0x80));
    //UART_PutString(buffer);
	spiPut((((UINT8)(address<<5))&0xE0)|0x10);
    //sprintf(buffer, "Second SPI put %x \n\r", ((((UINT8)(address<<5))&0xE0)|0x10));
    //UART_PutString(buffer);
	spiPut(value);
#ifndef SPI_INTERRUPTS
	//RADIO_CS = 1;									// de-select radio SPI bus
	//RFIE = tmpRFIE;									// restore interrupt state
    lat_SPIJA_CE = 1;
#endif
}

// reads byte from radio at short "address"
UINT8 lowRead(UINT8 address)
{
	UINT8 toReturn;

#ifndef SPI_INTERRUPTS
	//UINT8 tmpRFIE = RFIE;
	//RFIE = 0;										// disable radio ints during communication
	//RADIO_CS = 0;									// select radio SPI bus
    lat_SPIJA_CE = 0;
#endif
	spiPut(address);
	toReturn = spiGet();
#ifndef SPI_INTERRUPTS
	//RADIO_CS = 1;									// de-select radio SPI bus
	//RFIE = tmpRFIE;									// restore interrupt state
    lat_SPIJA_CE = 1;
#endif
	return toReturn;
}

// writes "value" to radio at short "address"
void lowWrite(UINT8 address, UINT8 value)
{
#ifndef SPI_INTERRUPTS
	//UINT8 tmpRFIE = RFIE;
	//RFIE = 0;
	//RADIO_CS = 0;
    lat_SPIJA_CE = 0;
#endif
	spiPut(address);
	spiPut(value);
#ifndef SPI_INTERRUPTS
	//RADIO_CS = 1;
	//RFIE = tmpRFIE;
    lat_SPIJA_CE = 1;
#endif
}

// writes count consecutive bytes from source into consecutive FIFO slots starting at "register".  Returns next empty register #.
UINT8 toTXfifo(UINT16 reg, UINT8* source, UINT8 count)
{
	while(count--)
		highWrite(reg++,*source++);

	return reg;
}

// warm start radio hardware, tunes to Channel.  Takes about 0.37 ms on PIC32 at 20 MHz, 10 MHz SPI hardware clock
// on return, 0=no radio hardare, 1=radio is reset
UINT8 initMRF24J40(void)
{
	UINT8 i;
	UINT32 radioReset = ReadCoreTimer();	// record time we started the reset procedure

	RadioStatus.ResetCount++;

	RadioStatus.TX_BUSY = 0;			// tx is not busy after reset
	RadioStatus.TX_FAIL = 1;			// if we had to reset, consider last packet (if any) as failed
	RadioStatus.TX_PENDING_ACK = 0;		// not pending an ack after reset
	RadioStatus.SLEEPING = 0;			// radio is not sleeping

	/* do a soft reset */
	lowWrite(WRITE_SOFTRST,0x07);		// reset everything (power, baseband, MAC) (also does wakeup if in sleep)
	do
	{
		i = lowRead(READ_SOFTRST);

		if (CT_TICKS_SINCE(radioReset) > MS_TO_CORE_TICKS(50))		// if no reset in a reasonable time
			return 0;												// then there is no radio hardware
	}
	while((i&0x07) != (UINT8)0x00);   	// wait for hardware to clear reset bits
    
    lowWrite(WRITE_FFOEN, 0x98);		// PACON2, per datasheet init
    lowWrite(WRITE_TXPEMISP, 0x95);  	// TXSTBL; RFSTBL=9, MSIFS-5
    highWrite(RFCTRL0,0x03);			// RFOPT=0x03
    sprintf(buffer, "WRITE_RFCTRL0 0x%x\n\r", highRead(RFCTRL0));
    UART_PutString(buffer);
	highWrite(RFCTRL1,0x02);			// VCOOPT=0x02, per datasheet
	highWrite(RFCTRL2,0x80);			// PLL enable
    highWrite(RFCTRL6,0x90);			// TXFILter on, 20MRECVR set to < 3 mS
	highWrite(RFCTRL7,0x80);			// sleep clock 100 kHz internal
	highWrite(RFCTRL8,0x10);			// RFVCO to 1
	highWrite(SCLKDIV, 0x21);			// CLKOUT disabled, sleep clock divisor is 2    SLPCON1
    lowWrite(WRITE_BBREG2,0x80);		// CCA energy threshold mode
	lowWrite(WRITE_RSSITHCCA,0x60);		// CCA threshold ~ -69 dBm  
    lowWrite(WRITE_BBREG6,0x40);		// RSSI on every packet
    lowWrite(WRITE_INTMSK,0b11110110);	// INTCON, enabled=0. RXIE and TXNIE only enabled.
    highWrite(CLKIRQCR, 0b10);
    RadioSetChannel(RadioStatus.Channel);	// tune to current radio channel
    highWrite(RFCTRL3, TX_POWER);		// set transmit power
    lowWrite(WRITE_RFCTL,0x04);			// reset RF state machine
    DelayAprox10Us(19);  
	lowWrite(WRITE_RFCTL,0x00);			// back to normal operation
    DelayAprox10Us(19);  
    
    sprintf(buffer, "WRITE_FFOEN 0x%x\n\r", lowRead(READ_FFOEN));
    UART_PutString(buffer);
    sprintf(buffer, "WRITE_TXPEMISP 0x%x\n\r", lowRead(READ_TXPEMISP));
    UART_PutString(buffer);
    sprintf(buffer, "WRITE_RFCTRL0 0x%x\n\r", highRead(RFCTRL0));
    UART_PutString(buffer);
    sprintf(buffer, "WRITE_RFCTRL1 0x%x\n\r", highRead(RFCTRL1));
    UART_PutString(buffer);
    sprintf(buffer, "WRITE_RFCTRL2 0x%x\n\r", highRead(RFCTRL2));
    UART_PutString(buffer);
    sprintf(buffer, "WRITE_RFCTRL3 0x%x\n\r", highRead(RFCTRL3));
    UART_PutString(buffer);
    sprintf(buffer, "WRITE_RFCTRL4 0x%x\n\r", highRead(RFCTRL4));
    UART_PutString(buffer);
    sprintf(buffer, "WRITE_RFCTRL5 0x%x\n\r", highRead(RFCTRL5));
    UART_PutString(buffer);
    sprintf(buffer, "WRITE_RFCTRL6 0x%x\n\r", highRead(RFCTRL6));
    UART_PutString(buffer);
    sprintf(buffer, "WRITE_RFCTRL7 0x%x\n\r", highRead(RFCTRL7));
    UART_PutString(buffer);
    sprintf(buffer, "WRITE_RFCTRL8 0x%x\n\r", highRead(RFCTRL8));
    UART_PutString(buffer);
    sprintf(buffer, "SCLKDIV 0x%x\n\r", highRead(SCLKDIV));
    UART_PutString(buffer);
    sprintf(buffer, "WRITE_BBREG2 0x%x\n\r", lowRead(READ_BBREG2));
    UART_PutString(buffer);
    sprintf(buffer, "WRITE_RSSITHCCA 0x%x\n\r", lowRead(READ_RSSITHCCA));
    UART_PutString(buffer);
    sprintf(buffer, "WRITE_BBREG6 0x%x\n\r", lowRead(READ_BBREG6));
    UART_PutString(buffer);
    sprintf(buffer, "WRITE_RFCTRL3 0x%x\n\r", highRead(RFCTRL3));
    UART_PutString(buffer);
    sprintf(buffer, "WRITE_RFCTL 0x%x\n\r", lowRead(READ_RFCTL));
    UART_PutString(buffer);
    
    
	//lowWrite(WRITE_RXFLUSH,0x01);		// flush the RX fifo, leave WAKE pin disabled

	RadioSetAddress(RadioStatus.MyShortAddress, RadioStatus.MyLongAddress, RadioStatus.MyPANID);

	



	

	/*#if defined(ENABLE_PA_LNA)
		highWrite(TESTMODE, 0x0F);		// setup for PA_LNA mode control
	#endif*/

    
    char buffer[32];
    sprintf(buffer, "hello 0x%x\n\r", highRead(RFSTATE));
    UART_PutString(buffer);
    sprintf(buffer, "TXSTBL 0x%x\n\r", lowRead(READ_SOFTRST));
    UART_PutString(buffer);

	while((highRead(RFSTATE)&0xA0) != 0xA0);	// wait till RF state machine in RX mode
    sprintf(buffer, "NOT 0 0x%x\n\r", highRead(RFSTATE));
    UART_PutString(buffer);
    

	//lowWrite(WRITE_INTMSK,0b11110110);	// INTCON, enabled=0. RXIE and TXNIE only enabled.

	// Make RF communication stable under extreme temperatures
	//highWrite(RFCTRL0, 0x03);			// this was previously done above
	//highWrite(RFCTRL1, 0x02);			// VCCOPT - whatever that does...

	/*#ifdef TURBO_MODE					// propriatary TURBO_MODE runs at 625 kbps (vs. 802.15.4 compliant 250 kbps)
		lowWrite(WRITE_BBREG0, 0x01);	// TURBO mode enable
		lowWrite(WRITE_BBREG3, 0x38);	// PREVALIDTH to turbo optimized setting
		lowWrite(WRITE_BBREG4, 0x5C);	// CSTH carrier sense threshold to turbo optimal
	#endif*/


	// now delay at least 192 uS per datasheet init

	return 1;
}

// on return, 1=radio is setup, 0=there is no radio
BOOL RadioInit(void)					// cold start radio init
{
	BOOL radio;

	memset((void*)&RadioStatus, 0, sizeof(RadioStatus));   //STARTING ADRESS VALUE TO BE FILLED NUMBER OF BYTES TO FILL

	RadioStatus.MyPANID 		= MY_PAN_ID;
	RadioStatus.MyShortAddress 	= MY_SHORT_ADDRESS;
	RadioStatus.MyLongAddress  	= MY_LONG_ADDRESS;

	RadioStatus.Channel = 11;			// start at channel 11

	radio = initMRF24J40();				// init radio hardware, tune to RadioStatus.Channel

	//RFIE = 1;							// enable radio interrupts

	return radio;
}

// set short address and PANID
void RadioSetAddress(UINT16 shortAddress, UINT64 longAddress, UINT16 panID)
{
	UINT8 i;

	lowWrite(WRITE_SADRL,BYTEPTR(shortAddress)[0]);
	lowWrite(WRITE_SADRH,BYTEPTR(shortAddress)[1]);

	lowWrite(WRITE_PANIDL,BYTEPTR(panID)[0]);
	lowWrite(WRITE_PANIDH,BYTEPTR(panID)[1]);

	for(i=0;i<sizeof(longAddress);i++)	// program long MAC address
		lowWrite(WRITE_EADR0+i*2,BYTEPTR(longAddress)[i]);

	RadioStatus.MyPANID 		= panID;
	RadioStatus.MyShortAddress 	= shortAddress;
	RadioStatus.MyLongAddress  	= longAddress;
}

// Set radio channel.  Returns with success/fail flag.
BOOL RadioSetChannel(UINT8 channel)
{
	if( channel < 11 || channel > 26)
		return FALSE;

	#if defined(ENABLE_PA_LNA)	// Permitted band is 2400 to 2483.5 MHz.
		if( channel == 26 )		// Center Freq. is 2405+5(k-11) MHz, for k=channel 11 to 26
			return FALSE;		// max output is 100mW (USA)
	#endif						// rolloff is not steep enough to avoid 2483.5 from channel 26 center of 2480 MHz at full MB power

	RadioStatus.Channel = channel;
	highWrite(RFCTRL0,((channel-11)<<4)|0x03);
	lowWrite(WRITE_RFCTL,0x04);	// reset RF state machine
	lowWrite(WRITE_RFCTL,0x00);	// back to normal

	return TRUE;
}

// Put the RF transceiver into sleep or wake it up
// Radio power, MRF24J40MB - ENABLE_PA_LNA on:
//	RX:  	28.4 mA
//	TX: 	65.8 mA (as fast as I can xmit; nominal peak 130 mA)
//	Sleep:	0.245 mA (spec is 5 uA with 'sleep clock disabled'; setting register 0x211 to 0x01 doesn't seem to help)
// Note that you can in practice turn the radio power off completely for short periods (with a MOSFET) and then do a warm start.
void RadioSetSleep(UINT8 powerState)
{
	if (powerState)
	{
		#if defined(ENABLE_PA_LNA)
			highWrite(TESTMODE, 0x08);      // Disable automatic switch on PA/LNA
			lowWrite(WRITE_GPIODIR, 0x0F);	// Set GPIO direction to OUTPUT (control PA/LNA)
			lowWrite(WRITE_GPIO, 0x00);     // Disable PA and LNA
		#endif

		lowWrite(WRITE_SOFTRST, 0x04);		// power management reset to ensure device goes to sleep
		lowWrite(WRITE_WAKECON,0x80);		// WAKECON; enable immediate wakeup
		lowWrite(WRITE_SLPACK,0x80);		// SLPACK; force radio to sleep now

		RadioStatus.SLEEPING = 1;			// radio is sleeping
	}	
	else
		initMRF24J40();		// could wakeup with WAKE pin or by toggling REGWAKE (1 then 0), but this is simpler
}

// Do a single (128 us) energy scan on current channel.  Return RSSI.
UINT8 RadioEnergyDetect(void)
{
	UINT8 RSSIcheck;

	#if defined(ENABLE_PA_LNA)
		highWrite(TESTMODE, 0x08);          // Disable automatic switch on PA/LNA
		lowWrite(WRITE_GPIODIR, 0x0F);      // Set GPIO direction to OUTPUT (control PA/LNA)
		lowWrite(WRITE_GPIO, 0x0C);         // Enable LNA, disable PA
	#endif

	lowWrite(WRITE_BBREG6, 0x80);			// set RSSIMODE1 to initiate RSSI measurement

	RSSIcheck = lowRead (READ_BBREG6);		// Read RSSIRDY
	while ((RSSIcheck & 0x01) != 0x01)		// Wait until RSSIRDY goes to 1; this indicates result is ready
		RSSIcheck = lowRead (READ_BBREG6);	// this takes max 8 symbol periods (16 uS each = 128 uS)

	RSSIcheck = highRead(0x210);			// read the RSSI

	lowWrite(WRITE_BBREG6, 0x40);			// enable RSSI on received packets again after energy scan is finished

	#if defined(ENABLE_PA_LNA)
		lowWrite(WRITE_GPIO, 0);
		lowWrite(WRITE_GPIODIR, 0x00);		// Set GPIO direction to INPUT
		highWrite(TESTMODE, 0x0F);			// setup for automatic PA/LNA control
	#endif

	return RSSIcheck;
}

// TX side - what goes in the TX FIFO (MRF24J40 datahseet figure 3-11):
//
// Size Offset	Descr
// 1		0		Header length (m)
// 1		1		Frame length (m+n)
// 1		2		LSB of Frame Control (bits/i)
// 1		3		MSB of Frame Control (type)
// 1		4		Sequence number
// 20		24		Addressing fields, worst case (PANIDx2 = 4, LONGx2=16 total =20)
// 103		127		Payload (from TxBuffer)

// sends raw packet per already setup Tx structure.  No error checking here.
void RadioTXRaw(void)
{
	UINT8 wReg;													// radio write register (into TX FIFO starting at long addr 0)

	wReg = toTXfifo(2,BYTEPTR(Tx)+1,2+1);						// frame control (2) + sequence number (1) 
    sprintf(buffer, "Setting wReg to toTxFifo1 %d\n\r", wReg);
    UART_PutString(buffer);

	if (Tx.dstAddrMode == SHORT_ADDR_FIELD)						// if a short dest addr is present
	{
		wReg = toTXfifo(wReg,BYTEPTR(Tx.dstPANID), 2);			// write dstPANID
		wReg = toTXfifo(wReg,BYTEPTR(Tx.dstAddr), 2);			// write short address
	}
	else if (Tx.dstAddrMode == LONG_ADDR_FIELD)					// if a long dest addr is present
	{
		wReg = toTXfifo(wReg,BYTEPTR(Tx.dstPANID), 2);			// write dstPANID
		wReg = toTXfifo(wReg,BYTEPTR(Tx.dstAddr), 8);			// long addr
	}

	// now wReg is at start of source PANID (if present)
    sprintf(buffer, "Setting wReg to PANID %d\n\r", wReg);
    UART_PutString(buffer);

	if ( Tx.srcAddrMode != NO_ADDR_FIELD && 					// if source present
		 Tx.dstAddrMode != NO_ADDR_FIELD && 					// and dest present
		 !Tx.panIDcomp )										// and no PANID compression
			wReg = toTXfifo(wReg,BYTEPTR(Tx.srcPANID), 2);		// then write src PANID
		
	if (Tx.srcAddrMode == SHORT_ADDR_FIELD)						// if a short src addr is present
		wReg = toTXfifo(wReg,BYTEPTR(Tx.srcAddr), 2);
	else if (Tx.srcAddrMode == LONG_ADDR_FIELD)					// if a long src addr is present
		wReg = toTXfifo(wReg,BYTEPTR(Tx.srcAddr), 8);
	
	// now wReg is pointing to first wReg after header (m)
    sprintf(buffer, "Setting wReg to first wReg after header (m) %d\n\r", wReg);
    UART_PutString(buffer);
	
	highWrite(0, wReg-2);										// header length, m (-2 for header & frame lengths)

	wReg = toTXfifo(wReg,Tx.payload,Tx.payloadLength);

	highWrite(1, wReg-2);										// frame length (m+n)

	RadioStatus.TX_BUSY = 1;									// mark TX as busy TXing
	RadioStatus.TX_PENDING_ACK = Tx.ackRequest;
//COMMENTED OUT ACK SINCE NO HANDSHAKE OCCURS
	lowWrite(WRITE_TXNMTRIG, Tx.ackRequest << 2 | 1);			// kick off transmit with above parameters
	RadioStatus.LastTXTriggerTick = ReadCoreTimer();			// record time (used to check for locked-up radio or PLL loss)
}

// Sends next packet from Tx.  Blocks for up to MRF24J40_TIMEOUT_TICKS if transmitter is
// not ready (RadioStatus.TX_BUSY).  If you don't want to be blocked, don't call
// ths until RadioStatus.TX_BUSY == 0.  
//
// This automatically sets frame number and source address for you
void RadioTXPacket(void)
{
	if (Tx.srcAddrMode == SHORT_ADDR_FIELD)
		Tx.srcAddr = RadioStatus.MyShortAddress;
	else if (Tx.srcAddrMode == LONG_ADDR_FIELD)
		Tx.srcAddr = RadioStatus.MyLongAddress;

	Tx.frameNumber = RadioStatus.IEEESeqNum++;

	while(RadioStatus.TX_BUSY)									// If TX is busy, wait for it to clear (for a resaonable time)
		if ( CT_TICKS_SINCE(RadioStatus.LastTXTriggerTick) > MRF24J40_TIMEOUT_TICKS )	// if not ready in a resonable time
			initMRF24J40();										// reset radio hardware (stay on same channel)

	RadioTXRaw();
}


// returns status of last transmitted packet: TX_SUCCESS (1), TX_FAILED (2), or 0 = no result yet because TX busy
UINT8 RadioTXResult(void)
{
    sprintf(buffer, "Inside TXresult %d\n\r", (TX_RESULT_SUCCESS + RadioStatus.TX_FAIL));
            UART_PutString(buffer);
            //RadioStatus.TX_BUSY = 0;
	if (RadioStatus.TX_BUSY)									// if TX not done yet
		return TX_RESULT_BUSY;
				
	return TX_RESULT_SUCCESS + RadioStatus.TX_FAIL;				// 1=success, 2=fail
}

// returns TX_RESULT_SUCCESS or TX_RESULT_FAILED.  Waits up to MRF24J40_TIMEOUT_TICKS.
UINT8 RadioWaitTXResult(void)
{
	while(RadioStatus.TX_BUSY)									// If TX is busy, wait for it to clear (for a resaonable time)
		if ( CT_TICKS_SINCE(RadioStatus.LastTXTriggerTick) > MRF24J40_TIMEOUT_TICKS )		// if not ready in a resonable time
			initMRF24J40();										// reset radio hardware (stay on same channel)

	return TX_RESULT_SUCCESS + RadioStatus.TX_FAIL;				// 1=success, 2=fail
}


//	RX side - what goes in RXBuffer (from MRF24J40 datasheet figure 3-9)
//
//	Size	Offset
//	1		0		Frame length (m+n+2 = header + 102 + FCS)
//	1		1		LSB of Frame Control (bits)
//	1		2		MSB of Frame Control (type)
//	1		3		Sequence number
//	20		23		Addressing fields, worst case (PANIDx2 = 4, MACx2=16 total =20)
//	103		126		Payload
//	2		128		FCS
//	1		129		LQI
//	1		130		RSSI

// Returns count of received packets waiting to be processed & discarded.  Next packet to process is in "Rx".
// Note this gives you ALL received packets (not just ones addressed to you).   Check the addressing yourself if you care.
// Also be aware that sucessive identical packets (same frame number) will be received if the far-end misses your ACK (it
// will re-transmit).  Check for that if you care.
UINT8 RadioRXPacket(void)
{
	if (!RadioStatus.RXPacketCount){
		return 0;				
    }// no packets to process

	UINT8* readPoint = (UINT8*)RXBuffer[RadioStatus.RXReadBuffer];		// recieved packet read point

	if(RadioStatus.TX_BUSY)		//STAYS BUSY?										// time out and reset radio if we missed interrupts for a long time
		if ( CT_TICKS_SINCE(RadioStatus.LastTXTriggerTick) > MRF24J40_TIMEOUT_TICKS )
				initMRF24J40();											// reset radio hardware (stay on same channel)

	readPoint = readBytes(BYTEPTR(Rx), readPoint, 1+2+1+2);				// copy frame length (1), frame control (2), frame number (1), PANID (2)
	
	if( Rx.securityEnabled )											// if security enabled, toss it (not supported)
	{
		RadioStatus.RXSecurityEnabled++;								// log error
		RadioDiscardPacket();
		return RadioRXPacket();											// yes I know it's a little recursive, but the RXBuffer is small enough that the stack is unlikely to overflow
	}

	if (Rx.frameType == PACKET_TYPE_ACK)								// no PANID present on ACK frames [802.15.4 weakness: No way to know if this ACK is really for you]
		readPoint -= 2;

	// readPoint now just after first PANID field

	if (Rx.dstAddrMode == SHORT_ADDR_FIELD)								// if a short dest addr is present
		readPoint = readBytes(BYTEPTR(Rx.dstAddr), readPoint, 2);
	else if (Rx.dstAddrMode == LONG_ADDR_FIELD)							// if a long dest addr is present
		readPoint = readBytes(BYTEPTR(Rx.dstAddr), readPoint, 8);

	Rx.srcPANID = Rx.dstPANID;											// copy first PANID because we don't know if it's src or dst yet
	Rx.srcAddr = Rx.dstAddr;											// ditto for address

	// now readPoint is at start of source PANID (if present)

	if ( Rx.srcAddrMode != NO_ADDR_FIELD && 							// if source present
		 Rx.dstAddrMode != NO_ADDR_FIELD && 							// and dest present
		 !Rx.panIDcomp )												// and no PANID compression
			readPoint = readBytes(BYTEPTR(Rx.srcPANID),readPoint, 2);	// then read src PANID
		
	if (Rx.srcAddrMode == SHORT_ADDR_FIELD)								// if a short src addr is present
		readPoint = readBytes(BYTEPTR(Rx.srcAddr),readPoint, 2);
	else if (Rx.srcAddrMode == LONG_ADDR_FIELD)							// if a long src addr is present
		readPoint = readBytes(BYTEPTR(Rx.srcAddr),readPoint,8);
	
	Rx.payload = readPoint;												// now readPoint points at the start of the payload
	Rx.payloadLength = Rx.frameLength - (readPoint - RXBuffer[RadioStatus.RXReadBuffer]) + 1;

	Rx.lqi = RXBuffer[RadioStatus.RXReadBuffer][RXBuffer[RadioStatus.RXReadBuffer][0]+3];
	Rx.rssi = RXBuffer[RadioStatus.RXReadBuffer][RXBuffer[RadioStatus.RXReadBuffer][0]+4];

	return RadioStatus.RXPacketCount;
}

void RadioDiscardPacket(void)
{
	if (RadioStatus.RXPacketCount)										// just in case we get called more than we ought
	{
		RadioStatus.RXPacketCount--;
		RadioStatus.RXReadBuffer = (RadioStatus.RXReadBuffer + 1) & (PACKET_BUFFERS - 1);
	}
	else
		RadioStatus.RadioExtraDiscard++;
}


// Interrupt handler for the MRF24J40 and P2P stack (PIC32 only, no security)
void __ISR(_CHANGE_NOTICE_VECTOR) CN_Handler(void)				// from INT pin on MRF24J40 radio
{
	MRF24J40_IFREG iflags;

	//PUSH_DEBUG_STATE();
	//SET_DEBUG_STATE(CPU_BUSY);

	//RFIF = 0;		
    // clear IF immediately to allow next interrupt
    
    IEC1bits.CNCIE= 0;

	iflags.Val = lowRead(READ_ISRSTS);									// read ISR to see what caused the interrupt

	if(iflags.bits.RXIF)												// RX int?
	{
		UINT8 i, bytes;

		lowWrite(WRITE_BBREG1, 0x04);									// set RXDECINV to disable hw RX while we're reading the FIFO

		bytes = highRead(0x300) + 2;									// get the size of the packet w/FCS, + 2 more bytes for RSSI and LQI

		if( bytes > RX_BUFFER_SIZE)										// if too big for the RX buffer
		{
			RadioStatus.RXPacketTooBig++;
			bytes = RX_BUFFER_SIZE;										// truncate to fit
		}

		RXBuffer[RadioStatus.RXWriteBuffer][0] = bytes - 4;				// store length of packet (not counting length byte, FCS, LQI and RSSI)

		for(i=1;i<=bytes;i++)											// copy data from the FIFO into the RX buffer, plus RSSI and LQI
			RXBuffer[RadioStatus.RXWriteBuffer][i] = highRead(0x300+i);

		RadioStatus.RXPacketCount++;
		RadioStatus.RXWriteBuffer = (RadioStatus.RXWriteBuffer+1) & (PACKET_BUFFERS-1);	// mod PACKET_BUFFERS

		if ( (RadioStatus.RXPacketCount > PACKET_BUFFERS) || (RadioStatus.RXWriteBuffer == RadioStatus.RXReadBuffer) )
			RadioStatus.RXBufferOverruns++;

		lowWrite(WRITE_RXFLUSH,0x01);									// flush RX hw FIFO manually (workaround for silicon errata #1)
		lowWrite(WRITE_BBREG1, 0x00);									// reset RXDECINV to enable radio to receive next packet
	}

	if(iflags.bits.TXIF)												// TX int?  If so, this means TX is no longer busy, and the result (if any) of the ACK request is in
	{
		RadioStatus.TX_BUSY = 0;										// clear busy flag (TX is complete now)

		if(RadioStatus.TX_PENDING_ACK)									// if we were waiting for an ACK
		{
			UINT8 TXSTAT = lowRead(READ_TXSR);							// read TXSTAT, transmit status register
			RadioStatus.TX_FAIL    = TXSTAT & 1;						// read TXNSTAT (TX failure status)
			RadioStatus.TX_RETRIES = TXSTAT >> 6;						// read TXNRETRY, number of retries of last sent packet (0..3)
			RadioStatus.TX_CCAFAIL = TXSTAT & 0b00100000;				// read CCAFAIL

			RadioStatus.TX_PENDING_ACK = 0;								// TX finished, clear that I am pending an ACK, already got it (if I was gonna get it)
		}
	}
    
    PORTC;
    IFS1bits.CNCIF=0;
    IEC1bits.CNCIE=1;
/*
#ifdef SPI_INTERRUPTS
		if (SPI2STATbits.SPITBE) 
			INTSetFlag(INT_SPI2TX);			
#endif
*/

	//POP_DEBUG_STATE();
}




