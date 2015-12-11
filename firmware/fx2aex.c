////////////////////////////////////////////////////

#define ALLOCATE_EXTERN
#include "fx2regs.h"

////////////////////////////////////////////////////

#define abs(x) ((x) >= 0 ? (x) : -(x))


//#define ENABLE_I2C


#define SYNCDELAY syncdelay()
void syncdelay(void) {
    // see FX2LP TRM 15.5
    // 16 nops are worst case. 3 should be ok...
    _asm nop _endasm;
    _asm nop _endasm;
    _asm nop _endasm;
    _asm nop _endasm;

    _asm nop _endasm;
    _asm nop _endasm;
    _asm nop _endasm;
    _asm nop _endasm;

    _asm nop _endasm;
    _asm nop _endasm;
    _asm nop _endasm;
    _asm nop _endasm;

    _asm nop _endasm;
    _asm nop _endasm;
    _asm nop _endasm;
    _asm nop _endasm;
}

////////////////////////////////////////////////////


////////////////////////////////////////////////////

void msleep(int j) {
    while (j--) {
        short i = 208;
        while (i--) {
            syncdelay();
        }
    }
}

void msleep_calibrate(void) {
    OED = 0x80;
    while (1) {
        IOD = 0x00;
        msleep(10);
        IOD = 0x80;
        msleep(10);
    }
}


////////////////////////////////////////////////////

#define EP1CMD_MONITOR (0x01)

#define EP1CMD_I2COUT (0x02)

// I2CS - E678
//  b7(RW)  b6(RW)  b5(RW)  b4(R)   b3(R)   b2(R)   b1(R)   b0(R)
//  START   STOP    LASTRD  ID1     ID0     BERR    ACK     DONE
// I2DAT - E679
//  b7(RW)  b6(RW)  b5(RW)  b4(RW)  b3(RW)  b2(RW)  b1(RW)  b0(RW)
//  D7      D6      D5      D4      D3      D2      D1      D0
// I2CTL - E67A
//  b7(R)   b6(R)   b5(R)   b4(R)   b3(R)   b2(R)   b1(RW)  b0(RW)
//  0       0       0       0       0       0       STOPIE  400kHz  
// EP01STAT - SFR 0xBA
//  b7(R)   b6(R)   b5(R)   b4(R)   b3(R)   b2(R)       b1(R)       b0(R)
//  0       0       0       0       0       EP1INBSY    EP1OUTBSY   EP0BSY
// TMOD - SFR 0x89
//  b7(RW)  b6(RW)  b5(RW)  b4(RW)  b3(RW)  b2(RW)  b1(RW)  b0(RW)
//  GATE1   C/T1z   T1M1    T1M0    GATE0   C/T0z   T0M1    T0M0
//  0       0       0       0       0       0       0       0       default
//  x       x       x       x       0       0       0       1

// GATE1[0] - Timer 1[0] gate control. When GATE1[0] = 1, Timer 1[0] will clock only when INT1[0]z = 1 and
// TR1[0] (TCON.6[4]) = 1. When GATE1[0] = 0, Timer 1[0] will clock only when TR1[0] = 1, regardless of
// the state of INT1[0]z.
// C/T1[0]z - Counter/Timer select. When C/T1[0]z = 0, Timer 1[0] is clocked by CLKOUT/4 or CLKOUT/12 (default), 
// depending on the state of T1[0]M (CKCON.4[3]). When C/T1[0]z = 1, Timer 1[0] is clocked by high-to-low 
// transitions on the T1[0] pin.
// T1[0]M1 - Timer 1[0] mode select bit 1.
// T1[0]M0 - Timer 1[0] mode select bit 0.
// T1[0]M1  T1[0]M0     Mode
// 0        0           Mode 0 : 13-bit counter
// 0        1           Mode 1 : 16-bit counter
// 1        0           Mode 2 : 8-bit counter with auto-reload
// 1        1           Mode 3 : Timer 1[0] stopped

// TCON - SFR 0x89
//  b7(R)   b6(RW)  b5(RW)  b4(R)   b3(R)   b2(RW)  b1(R)   b0(RW)
//  TF1     TR1     TF0     TR0     IE1     IT1     IE0     IT0
//  0       0       0       0       0       0       0       0       default
//  x       x       x       1       x       x       x       x

// TF1[0] - Timer 1[0] overflow flag. Set to 1 when the Timer 1[0] count overflows; automatically
// cleared when the EZ-USB vectors to the interrupt service routine.
// TR1[0] - Timer 1[0] run control. 1 = Enable counting on Timer 1[0].
// IE1[0] - Interrupt 1[0] edge detect. If external interrupt 1[0] is configured to be edge-sensitive
// (IT1[0] = 1), IE1[0] is set when a negative edge is detected on the INT1[0]z pin and is automatically 
// cleared when the EZ-USB vectors to the corresponding interrupt service routine. In this case, IE1[0] can 
// also be cleared by software. If external interrupt 1[0] is configured to be level-sensitive (IT1[0] = 0), 
// IE1[0] is set when the INT1[0]z pin is 0 and automatically cleared when the INT1[0]z pin is 1. 
// In level-sensitive mode, software cannot write to IE1[0].
// IT1[0] - Interrupt 1[0] type select. INT1[0]z is detected on falling edge when IT1[0] = 1; INT1[0] is
// detected as a low level when IT1[0] = 0.


#define EP1OUT_HASDATA ( !(EP01STAT & 0x02) )       // Is EP1OUT busy?

#define PORTA_MONEN     (0x01)
#define PORTA_FXREADY   (0x80)

#define I2CS_START      (0x80)
#define I2CS_STOP       (0x40)
#define I2CS_ID1        (0x20)
#define I2CS_ID2        (0x10)
#define I2CS_BERR       (0x04)
#define I2CS_ACK        (0x02)
#define I2CS_DONE       (0x01)
#define I2C_READ        (0x01)

#define I2C_BYTEWRITE   (0x00)              // Byte Write mode
#define I2C_PAGEWRITE   (0x01)              // Page Write mode
#define I2C_BYTEWRITE_MAXBYTE   3           // # of sent bytes in Byte Write mode
#define I2C_PAGEWRITE_MAXBYTE   62          // # of sent bytes in Page Write mode
#define I2C_FPGAADDR    (0x7F)              // FPGA device address
#define I2C_DONE        (I2CS & 0x01)       // Byte transfer completed?
#define I2C_BERR        (I2CS & I2CS_BERR)  // Conflict in master definition?
#define I2C_ACK         (I2CS & I2CS_ACK)   // Acknowlegde received?
#define I2C_STOP        (I2CS & I2CS_STOP)  // Stop received?

#define TIMER0_OVR      (TCON & 0x20)       // Byte transfer completed?

void ep1out_rearm(void) {
    EP1OUTBC = 0xFF; // write any value
}

#ifdef ENABLE_I2C
void i2c_write(void) {

char ack, step1=0;
int maxbytes=64, cnt;

//  Sending Data Sequence (EZ-USB_TRM.book pg13-16 (294))

    do {
//      1. Set START=1. if BERR=1, start timer
        I2CS = I2CS_START;  // start
        if (I2C_BERR) {
            //timer - TMOD = xxxx0001;  TCON = xxx1xxxx
            TMOD = (TMOD & 0xF0) | 0x01;
            TCON = (TCON | 0x10);
        }

//      2. Write the 7-bit peripheral address and the direction bit (0) to I2DAT
        I2DAT = I2C_FPGAADDR * 2 + !I2C_READ;

//      3. Wait for DONE=1 or for timer to expire. if BERR=1, go to 1.
        if (!I2C_BERR) {
            while ((!I2C_DONE) & (!TIMER0_OVR)) {   // include timer
                if I2CS_BERR {
                    step1 = 1;
                }
            }

//          4. if ACK=0, go to 9
            if ((I2C_ACK) && (!step1)) {
                switch (EP1OUTBUF[1]) { 
                    case I2C_BYTEWRITE:
                        maxbytes = I2C_BYTEWRITE_MAXBYTE;
                        break;
                    case I2C_PAGEWRITE:
                        maxbytes = I2C_PAGEWRITE_MAXBYTE;
                        break;
                    default:
                        break;
                }
                cnt = 1; 
                ack = 1;
                while (cnt < maxbytes || !I2C_BERR || ack || !step1) {
//                  5. Load I2DAT with a data byte
                    I2DAT = EP1OUTBUF[cnt+1];
//                  6. Wait for DONE=1. if BERR=1, go to 1.
                    while (!I2C_DONE || !I2C_BERR || !TIMER0_OVR) {     // include timer
                    }
                    if I2C_BERR {
                        step1 = 1;
                    }
//                  7. if ACK=0, go to step 9.
                    if ((!I2C_ACK) && (!step1)) {
                        ack = 0;
                    }
                    cnt++;
//                  8. Repeat 5-7 until all data transfered
                }
            }
//          9. Set STOP=1. Wait for STOP=0
            if (!step1) {
                I2CS = I2CS_STOP;
                while I2C_STOP {
                }
            }
        }
    } while (step1);
}
#endif // ENABLE_I2C

void process_ep1out_command(void) {

    switch (EP1OUTBUF[0]) {
        case EP1CMD_MONITOR:
            if (EP1OUTBUF[1]) {
                IOA |= PORTA_MONEN; // 0x80 | 0x01 = 0x81 
            } else {
                IOA &= ~PORTA_MONEN;    // 0x80 & 0x7F = 0x00
            }
            break;

#ifdef ENABLE_I2C
        case EP1CMD_I2COUT:
            do {
                i2c_write();
            } while (I2C_BERR); 
            break;
#endif

        default:
            break;
    }

}


////////////////////////////////////////////////////

void init(void) {
    
    // chip revision control
    REVCTL = 0x03;
    SYNCDELAY;

    // prepare for MonEn pin
    //
    // make flag D pin port A.7
    PORTACFG = 0x00;
    SYNCDELAY;
    // set A.7 low == NOT READY!
    IOA = 0x00;
    OEA = PORTA_FXREADY | PORTA_MONEN;      // 0x80 | 0x01 = 0x81
    SYNCDELAY;

    
    CPUCS = 0x12; // 48MHz CPU clock, CLKOUT enable
    //CPUCS = 0x10; // 48MHz CPU clock, CLKOUT disable
    SYNCDELAY;


    // I/O mode + clock
    // IFCONFIG bits 7..0: IFCLKSRC, 3048MHZ, IFCLKOE, IFCLKPOL, ASYNC, GSTATE, IFCFG1, IFCFG0
    //  - for slave fifo: IFCFG[1:0] = "11"
    //  - to activate clock output to IFCLK: IFCLKOE = 1 AND!!! IFCLKSRC = 1 (internal clock source mandatory)
    //
    //IFCONFIG = 0x03; // external clock, synchronous, IFCLK not driven
    //IFCONFIG = 0xA3; // internal clock, synchronous, IFCLK DRIVEN!, 30MHz
    IFCONFIG = 0xE3; // internal clock, synchronous, IFCLK DRIVEN!, 48MHz
    SYNCDELAY;
   

    // EP1IN / EP1OUT
    EP1INCFG  = 0x00; // off
    EP1OUTCFG = 0xA0; // 1010 0000 ep1out: valid, bulk
    //
    // arm ep1 out
    ep1out_rearm();


    
    //EP2CFG = 0x00; // off
    //EP2CFG = 0xA0; // 1010 0000 ep2:, valid, out, bulk, 512B, quad buf
    EP2CFG = 0xA2; // 1010 0000 ep2:, valid, out, bulk, 512B, double buf
    SYNCDELAY;
    EP4CFG = 0x00; // off
    SYNCDELAY;
    //EP6CFG = 0x00; // off
    //EP6CFG = 0xE0; // ep6:, valid, in, bulk, 512B, quad buf
    EP6CFG = 0xE2; // ep6:, valid, in, bulk, 512B, double buf
    SYNCDELAY;
    EP8CFG = 0x00; // off
    SYNCDELAY;

   
    // do a fifo reset
    FIFORESET = 0x80;
    SYNCDELAY;
    FIFORESET = 0x02;
    SYNCDELAY;
    FIFORESET = 0x04;
    SYNCDELAY;
    FIFORESET = 0x06;
    SYNCDELAY;
    FIFORESET = 0x08;
    SYNCDELAY;
    FIFORESET = 0x00;
    SYNCDELAY;
      

    // EP 2468 FIFO CFG
    //
    EP2FIFOCFG = 0x00; // ep2,    NO auto commit OUT, 8bit
    //EP2FIFOCFG = 0x10; // ep2, auto commit OUT, 8bit  !!! BROKEN !!!
    //EP2FIFOCFG = 0x11; // ep2, auto commit OUT, 16 bit
    SYNCDELAY;
    //
    EP4FIFOCFG = 0x00; // set 8bit to enable PORT D
    SYNCDELAY;
    //
    //EP6FIFOCFG = 0x08; // ep6, auto commit IN, 8bit
    EP6FIFOCFG = 0x0c; // ep6, auto commit IN, 8bit, zerolenin=1
    SYNCDELAY;
    //
    EP8FIFOCFG = 0x00; // set 8bit to enable PORT D
    SYNCDELAY;


    // out ep 2: 'prime the pump' see REVCTL, OUTPKTEND
    // do 1x per buffer (eg. 4x for quad buffering)
    OUTPKTEND = 0x82;
    SYNCDELAY;
    OUTPKTEND = 0x82;
    SYNCDELAY;
    /*
    OUTPKTEND = 0x82;
    SYNCDELAY;
    OUTPKTEND = 0x82;
    SYNCDELAY;
    */
   

    // flag config
    PINFLAGSAB = 0x64;   // flag B: EP6PF, flag A: EP2PF
    SYNCDELAY;
    ////////////////////////////////////////////////////////////////
    // UNTIL 2010-11-07:
    //PINFLAGSCD = 0x00;
    ////////////////////////////////////////////////////////////////
    // NEW:
    PINFLAGSCD = 0x08;   // flag D: unused/default, flag C: EP2EF
    ////////////////////////////////////////////////////////////////
    SYNCDELAY;

    
    // flag fifo fill levels
    //
    ////////////////////////////////////////////////////////////////
    // UNTIL 2010-11-07:
    // ep2: flag high if >= 1 in fifo
    //EP2FIFOPFH = 0x80; // DECIS=1, PKTSTAT=0, PKTS=0, PFC=0
    //SYNCDELAY;
    //EP2FIFOPFL = 0x01; // PFC += 1
    //SYNCDELAY;
    ////////////////////////////////////////////////////////////////
    // NEW:
    // ep2: flag high if >= 4 in fifo
    EP2FIFOPFH = 0x80; // DECIS=1, PKTSTAT=0, PKTS=0, PFC=0
    SYNCDELAY;
    EP2FIFOPFL = 0x04; // PFC += 4
    SYNCDELAY;
    ////////////////////////////////////////////////////////////////
    //
    // anounce fifo is full (less than 32B available):
    // ep6: flag high if >= 1p + (512-32)B in fifo
    // 512 - 32 = 480 = 0x01E0 = 256 + 224
    EP6FIFOPFH = 0x89; // DECIS=1, PKTSTAT=0, PKTS=1, PFC=256
    SYNCDELAY;
    EP6FIFOPFL = 0xe0; // PFC += 224
    SYNCDELAY;

 
    // PA7 / FLAGD as port A 7, not fifo flag + chip select
    PORTACFG = 0x00;
    SYNCDELAY;

    // all fifo control pins lowactive
    FIFOPINPOLAR = 0x00;
    SYNCDELAY;

    // AUTOIN len limit
    EP6AUTOINLENH = 0x02; // 512
    SYNCDELAY;
    EP6AUTOINLENL = 0x00; // + 0
    SYNCDELAY;


    // PORT D as inputs:
    OED = 0x00;
    SYNCDELAY;

    
    SYNCDELAY;
    SYNCDELAY;
    SYNCDELAY;
    SYNCDELAY;
}

void disconnect() {
    USBCS = 0x08;
    SYNCDELAY;
}

void reconnect() {
    USBCS = 0x00;
    SYNCDELAY;
}

#define IS_HIGHSPEED ((USBCS & 0x80) != 0x00)
void enforcehighspeed() {
    while ( !IS_HIGHSPEED ) {
        disconnect();
        msleep(300);
        reconnect();
        msleep(300);
    }
    init();
}

void iotest(void) {

    // DANGEROUS! check OE'ed pins!!!

#if 0
    //IFCONFIG = 0xC8;
    //SYNCDELAY;

    //OEB = 0xFF;
    //SYNCDELAY;
    OED = 0xFF;
    SYNCDELAY;
    //OEA = 0x80;
    //SYNCDELAY;

    while (1) {
        IOD = 0xF0;
        //IOA = 0x80;
        //IOB = 0xFF;
        SYNCDELAY;
        IOD = 0x00;
        //IOA = 0x00;
        //IOB = 0xFF;
        SYNCDELAY;
    }
#endif
}

#define EP2EF (EP2468STAT & 0x01)
#define FIFO2EF (EP24FIFOFLGS & 0x02)


void main(void)
{

    init();

    while (1) {
    
        enforcehighspeed();

        // signal FxReady to fpga (0x80);
        IOA |= PORTA_FXREADY;

        while (1) {       
            // did we get a command on ep1out?
            if (EP1OUT_HASDATA) {
                process_ep1out_command();
                ep1out_rearm();
            }

            // did a packet arrive over USB?
            if ( ! EP2EF ) {
                OUTPKTEND = 0x02; // commit packet
                SYNCDELAY;
            }

            // try to detect a disconnect
            if ( !IS_HIGHSPEED ) {
                // this does somehow not work. i have no clue 
                // how to DETECT a physical disconnect...
                break;
            }
        }

        // NOT signal FxReady to fpga
        IOA &= ~PORTA_FXREADY;
    }
}


