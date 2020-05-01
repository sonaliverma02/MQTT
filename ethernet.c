<<<<<<< HEAD
// Ethernet Example
// Jason Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: EK-TM4C123GXL w/ ENC28J60
// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz

// Hardware configuration:
// ENC28J60 Ethernet controller on SPI0
//   MOSI (SSI0Tx) on PA5
//   MISO (SSI0Rx) on PA4
//   SCLK (SSI0Clk) on PA2
//   ~CS (SW controlled) on PA3
//   WOL on PB3
//   INT on PC6

// Pinning for IoT projects with wireless modules:
// N24L01+ RF transceiver
//   MOSI (SSI0Tx) on PA5
//   MISO (SSI0Rx) on PA4
//   SCLK (SSI0Clk) on PA2
//   ~CS on PE0
//   INT on PB2
// Xbee module
//   DIN (UART1TX) on PC5
//   DOUT (UART1RX) on PC4

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "EEPROM.h"
#include "tm4c123gh6pm.h"
#include "eth0.h"
#include "gpio.h"
#include "spi0.h"
#include "uart0.h"
#include "wait.h"


// Pins
#define RED_LED PORTF,1
#define BLUE_LED PORTF,2
#define GREEN_LED PORTF,3
#define PUSH_BUTTON PORTF,4

uint8_t state;
uint8_t tcpstate;
uint32_t counterTimer =0 ;

bool Conflag = false;
bool Disflag = false;
bool pingflag = false;
bool Pubflag = false;
bool Subflag = false;
bool UnSubflag  = false;
extern bool AvdSYN;


#define AIN0_MASK 8
#define AIN1_MASK 4


//bool initflag = true;
//-----------------------------------------------------------------------------
// Subroutines                
//-----------------------------------------------------------------------------

// Initialize Hardware

void initHw()
{
    // Configure HW to work with 16 MHz XTAL, PLL enabled, system clock of 40 MHz
    SYSCTL_RCC_R = SYSCTL_RCC_XTAL_16MHZ | SYSCTL_RCC_OSCSRC_MAIN | SYSCTL_RCC_USESYSDIV | (4 << SYSCTL_RCC_SYSDIV_S);


    SYSCTL_RCGCADC_R |= SYSCTL_RCGCADC_R0 | SYSCTL_RCGCADC_R1; // ENABLING AD0 and AD1

    SYSCTL_RCGC2_R |= SYSCTL_RCGC2_GPIOE;

    // Configure AIN0 and AI1 as an analog input
    GPIO_PORTE_AFSEL_R |= AIN0_MASK | AIN1_MASK;                 // select alternative functions for AIN0 and AIN1 on Port E (PE3 and PE2)
    GPIO_PORTE_DEN_R &= ~AIN0_MASK & ~AIN1_MASK;                  // turn off digital operation on pin PE3 and PE2
    GPIO_PORTE_AMSEL_R |= AIN0_MASK | AIN1_MASK;                 // turn on analog operation on pin PE3 and PE2

    // Configure ADC
    ADC0_CC_R = ADC_CC_CS_SYSPLL;                    // select PLL as the time base (not needed, since default value)
    ADC0_ACTSS_R &= ~ADC_ACTSS_ASEN0;                // disable sample sequencer 3 (SS3) for programming
    ADC0_EMUX_R = ADC_EMUX_EM0_PROCESSOR;            // select SS3 bit in ADCPSSI as trigger
    ADC0_SSMUX0_R = 0;                               // set first sample to AIN0
    ADC0_SSCTL0_R = ADC_SSCTL0_END0;                 // mark first sample as the end
    ADC0_ACTSS_R |= ADC_ACTSS_ASEN0;                 // enable SS3 for operation

    ADC1_CC_R = ADC_CC_CS_SYSPLL;                    // select PLL as the time base (not needed, since default value)
    ADC1_ACTSS_R &= ~ADC_ACTSS_ASEN0;                // disable sample sequencer 3 (SS3) for programming
    ADC1_EMUX_R = ADC_EMUX_EM0_PROCESSOR;            // select SS3 bit in ADCPSSI as trigger
    ADC1_SSMUX0_R = 1;                               // set first sample to AIN1
    ADC1_SSCTL0_R = ADC_SSCTL0_END0;                 // mark first sample as the end
    ADC1_ACTSS_R |= ADC_ACTSS_ASEN0;                 // enable SS3 for operation

    SYSCTL_RCGCTIMER_R |= SYSCTL_RCGCTIMER_R4;
    _delay_cycles(3);
    // Configure Timer 4 for 1 sec tick
    TIMER4_CTL_R &= ~TIMER_CTL_TAEN;                 // turn-off timer before reconfiguring
    TIMER4_CFG_R = TIMER_CFG_32_BIT_TIMER;           // configure as 32-bit timer (A+B)
    TIMER4_TAMR_R = TIMER_TAMR_TACDIR;          // configure for periodic mode (count down)
    TIMER4_IMR_R = 0;                                 // turn-off interrupt
    TIMER4_TAV_R = 0;                                //Counter starts from zero
    TIMER4_CTL_R |= TIMER_CTL_TAEN;                  // turn-on timer
    NVIC_EN2_R &= ~(1 << (INT_TIMER4A-80));             // turn-off interrupt

    // Enable clocks
    enablePort(PORTF);
    _delay_cycles(3);

    // Configure LED and pushbutton pins
    selectPinPushPullOutput(RED_LED);
    selectPinPushPullOutput(GREEN_LED);
    selectPinPushPullOutput(BLUE_LED);
    selectPinDigitalInput(PUSH_BUTTON);

}

int16_t readAdc0Temp()
{
    ADC0_SSCTL0_R |= ADC_SSCTL0_TS0;
    while (ADC0_ACTSS_R & ADC_ACTSS_BUSY);           // wait until SS0 is not busy
    return ADC0_SSFIFO0_R;                           // get single result from the FIFO
}

int16_t readAdc1Temp()
{
    ADC1_SSCTL0_R |= ADC_SSCTL0_TS0;
    while (ADC1_ACTSS_R & ADC_ACTSS_BUSY);           // wait until SS0 is not busy
    return ADC1_SSFIFO0_R;                           // get single result from the FIFO
}

void displayConnectionInfo()
{
    uint8_t i;
    //char str[10];
    char* str1;
    uint8_t mac[6];
    uint8_t ip[4];
    etherGetMacAddress(mac);
    putsUart0("HW: ");
    //putsUart0("\n\r");
    for (i = 0; i < 6; i++)
    {
        //sprintf(str, "%02x", mac[i]);
        str1 = itostring(mac[i]);
        putsUart0(str1);
        if (i < 6-1)
            putcUart0(':');
    }
    putsUart0("\n\r");
    etherGetIpAddress(ip);
    putsUart0("IP: ");
    for (i = 0; i < 4; i++)
    {
        //sprintf(str, "%u", ip[i]);
        str1 = itostring(ip[i]);
        putsUart0(str1);
        if (i < 4-1)
            putcUart0('.');
    }
    putsUart0("\n\r");
    etherGetMqttBrkIpAddress(ip);
    putsUart0("MQTT BROKER IP: ");
    for (i = 0; i < 4; i++)
    {
        //sprintf(str, "%u", ip[i]);
        str1 = itostring(ip[i]);
        putsUart0(str1);
        if (i < 4-1)
            putcUart0('.');
    }
    putsUart0("\n\r");
    etherGetDNSAddress(ip);
    putsUart0("DNS: ");
    for (i = 0; i < 4; i++)
    {
        //sprintf(str, "%u", ip[i]);
        str1 = itostring(ip[i]);
        putsUart0(str1);
        if (i < 4-1)
            putcUart0(':');
    }
    putsUart0("\n\r");

    if (etherIsLinkUp())
        putsUart0("Link is up\n\r");
    else
        putsUart0("Link is down\n\r");
}


//-----------------------------------------------------------------------------
// Main
//-----------------------------------------------------------------------------

// Max packet is calculated as:
// Ether frame header (18) + Max MTU (1500) + CRC (4)
#define MAX_PACKET_SIZE 1522

int main(void)
{
    uint8_t* udpData;
    uint8_t data[MAX_PACKET_SIZE];
    char* Pub_topic;
    char* Pub_data;
    char* Sub_topic;
    char* UnSub_topic;
    SubTopicFrame.Topic_names = 0;
    //char* Pub_server_data;
    uint8_t Switchcase = 0;

    USER_DATA info;

    // Init controller
    initHw();

    // Setup UART0 and EEPROM
    initUart0();
    setUart0BaudRate(115200, 40e6);
    initEeprom();


    // Init ethernet interface (eth0)
    putsUart0("\nStarting e0th0\n");
    putsUart0("\n\r");
    etherSetIpAddress(192,168,1,141);
    etherSetMacAddress(2, 3, 4, 5, 6, 141);
    etherSetMqttBrkIp(readEeprom(0x0020),readEeprom(0x0021), readEeprom(0x0022), readEeprom(0x0023));

    //tcp = true;
    etherInit(ETHER_UNICAST | ETHER_BROADCAST | ETHER_HALFDUPLEX);


    // Flash LED
    setPinValue(GREEN_LED, 1);
    waitMicrosecond(100000);
    setPinValue(GREEN_LED, 0);
    waitMicrosecond(100000);


    // Main Loop
    // RTOS and interrupts would greatly improve this code,
    // but the goal here is simplicity
    while (true)
    {

        // Put terminal processing here
        if (kbhitUart0())
        {
            getsUart0(&info);
            parseFields(&info);


            if(isCommand(&info,"set",2))
            {
                if(stringcmp("mqtt",getFieldString(&info,2)))
                {
                    etherSetMqttBrkIp(getFieldInteger(&info,3), getFieldInteger(&info,4), getFieldInteger(&info,5), getFieldInteger(&info,6));
                    writeEeprom(0x0020,getFieldInteger(&info,3));
                    writeEeprom(0x0021,getFieldInteger(&info,4));
                    writeEeprom(0x0022,getFieldInteger(&info,5));
                    writeEeprom(0x0023,getFieldInteger(&info,6));
                }

            }

            if(isCommand(&info,"help",2))
            {
                if(stringcmp("subs",getFieldString(&info,2)))
                {
                    putsUart0("Subscribed Topics are: \r\n");
                    uint8_t i;
                    for(i = 0; i < 9; i++)
                    {
                        putsUart0(SubTopicFrame.SubTopicArr[i]);
                        putsUart0("\n\r");
                    }
                }

            }

            if(isCommand(&info,"publish",3))
            {
                Pub_topic = getFieldString(&info,2);
                Pub_data = getFieldString(&info,3);
                Pubflag = true;
                tcpstate = TCPCLOSED;
            }

            if(isCommand(&info,"connect",1))
            {
                Conflag = true;
                tcpstate = TCPCLOSED;
            }

            if(isCommand(&info,"subscribe",2))
            {
                Sub_topic = getFieldString(&info,2);
                Subflag = true;
                //pingflag = false;
                tcpstate = TCPCLOSED;
            }

            if(isCommand(&info,"unsubscribe",2))
            {

                UnSub_topic = getFieldString(&info,2);
                UnSubflag = true;
                tcpstate = TCPCLOSED;
            }

            if(isCommand(&info,"disconnect",1))
            {
                setPinValue(RED_LED, 0);
                pingflag = false;
                Disflag = true;
            }

            if(isCommand(&info,"ifconfig",1))
            {
                displayConnectionInfo();
            }

            if(isCommand(&info,"reboot",1))
            {
                NVIC_APINT_R = 0x05FA0004;
            }

        }

        // Packet processing

        if(tcpstate == TCPCLOSED)
        {
            if(Pubflag || Subflag || UnSubflag || Conflag)
            {
                SendTcpSynmessage(data);
                tcpstate = TCPLISTEN;
            }
        }

        if(pingflag)
        {
            if(TIMER4_TAV_R > 40e6)
            {
                counterTimer++;
                TIMER4_TAV_R = 0;
                /*
                if(counterTimer%2 == 0)
                {
                    setPinValue(RED_LED, 1);
                }
                else
                {
                    setPinValue(RED_LED, 0);
                }
                  */
            }

            if(counterTimer > 30)
            {

                //SverPubFlag = true;
                SendMqttPingRequest(data);
                TIMER4_TAV_R =0;
                counterTimer = 0;
                Switchcase = PING;

            }

        }

        if(Disflag)
        {
            sendMqttDisconnectRequest(data);
            Disflag = false;
            Switchcase = DISCON;
        }

        if (etherIsDataAvailable())
        {
            if (etherIsOverflow())
            {
                setPinValue(RED_LED, 1);
                waitMicrosecond(100000);
                setPinValue(RED_LED, 0);
            }

            // Get packet
            etherGetPacket(data, MAX_PACKET_SIZE);

            // Handle ARP request
            if (etherIsArpRequest(data))
            {
                etherSendArpResponse(data);
            }


            // Handle IP datagram
            if (etherIsIp(data))
            {
                if (etherIsIpUnicast(data))
                {
                    // handle icmp ping request
                    if (etherIsPingRequest(data))
                    {
                        etherSendPingResponse(data);
                    }

                    // Process UDP datagram
                    // test this with a udp send utility like sendip
                    //   if sender IP (-is) is 192.168.1.198, this will attempt to
                    //   send the udp datagram (-d) to 192.168.1.199, port 1024 (-ud)
                    // sudo sendip -p ipv4 -is 192.168.1.198 -p udp -ud 1024 -d "on" 192.168.1.199
                    // sudo sendip -p ipv4 -is 192.168.1.198 -p udp -ud 1024 -d "off" 192.168.1.199
                    if (etherIsUdp(data))
                    {
                        udpData = etherGetUdpData(data);
                        if (stringcmp("on",(char*)udpData))
                            setPinValue(GREEN_LED, 1);
                        if (stringcmp("off",(char*)udpData))
                            setPinValue(GREEN_LED, 0);
                        etherSendUdpResponse(data, (uint8_t*)"Received", 9);

                    }

                }
            }

            if(IsMqttpublishServer(data))
            {
                Elements pub;

                pub = CollectPubData(data);
                putsUart0(pub.Data);
                putsUart0("\n\r");
                SendTcpAck1(data);
                //tcpstate = TCPCLOSED;
                if(stringcmp("led",pub.topic))
                {
                    if(stringcmp("on",pub.Data))
                    {
                        setPinValue(BLUE_LED, 1);
                    }else if(stringcmp("off",pub.Data))
                    {
                        setPinValue(BLUE_LED, 0);
                    }
                }
            }

            if(tcpstate == TCPLISTEN)
            {
                if(IsTcpSynAck(data))
                {
                    //SendTcpSynAckmessage(data);
                    tcpstate =  TCPSYN_RECV;

                }
            }

            if(tcpstate == TCPSYN_RECV)
            {

                SendTcpAck(data);
                _delay_cycles(6);
                SendTcpPushAck(data);
                tcpstate = TCPESTABLISHED;
                //_delay_cycles(6);

            }

            if(tcpstate == TCPESTABLISHED)
            {
                if(IsMqttConnectAck(data))
                {
                    if(Pubflag)//if client is sending publish
                    {
                        SendTcpAck1(data);
                        _delay_cycles(6);
                        SendMqttPublishClient(data,Pub_topic,Pub_data);
                        Switchcase = PUB;
                        //_delay_cycles(6);
                    }

                    if(Subflag)// if client is sending subscribe
                    {
                        SendTcpAck1(data);
                        _delay_cycles(6);
                        SendMqttSubscribeClient(data,Sub_topic);
                        Switchcase = SUB;
                        Subflag = false;
                        //tcpstate = TCPCLOSED;
                    }

                    if(UnSubflag)
                    {
                        SendTcpAck1(data);
                        _delay_cycles(6);
                        SendMqttUnSubscribeClient(data,UnSub_topic);
                        Switchcase = UNSUB;
                    }

                }

                switch(Switchcase)
                {
                case PUB:
                    if(AvdSYN)
                    {
                        if(IsTcpAck(data))//IT comes when QoS level of publish is 0
                        {
                            Pubflag = false;
                            AvdSYN = true;

                            Switchcase = 0;
                        }
                    }

                    if(IsPubAck(data)) // it comes when QoS level of publish is 1
                    {
                        Pubflag = false;

                        Switchcase = 0;
                    }

                    if(IsPubRec(data)) // it comes when QoS level of publish is 2
                    {
                        // SendTcpAck1(data);
                        //_delay_cycles(6);
                        SendMqttPublishRel(data);
                        Pubflag = false;

                        Switchcase = 0;
                    }

                    break;

                case SUB:
                    if(IsSubAck(data))
                    {

                        SendTcpAck1(data);
                        TIMER4_TAV_R = 0;
                        Switchcase = 0;
                        pingflag = true;
                        //Subflag = false;
                    }

                    break;
                case UNSUB:
                    if(IsUnsubAck(data))
                    {
                        Switchcase = 0;
                        UnSubflag = false;
                        uint8_t i;
                        for(i = 0; i < 10; i++)
                        {
                            if(stringcmp(SubTopicFrame.SubTopicArr[i], UnSub_topic))
                            {
                                break;
                            }
                        }

                        while(i < 9)
                        {
                            stringCopy(SubTopicFrame.SubTopicArr[i], SubTopicFrame.SubTopicArr[i+1]);
                            i++;
                        }

                        i = 0;
                        //pingflag = true;
                    }
                    break;

                case PING:

                    if(IsMqttPingResponse(data))
                    {
                        SendTcpAck1(data);
                        Switchcase = 0;
                    }
                    break;

                case DISCON:
                    if(ISTcpFinAck(data))
                    {
                        SendTcpFin(data);
                        Disflag = false;
                        tcpstate = TCPCLOSED;
                    }
                    break;

                default:

                    break;

                }

            }

        }

    }
}
=======
// Ethernet Example
// Jason Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: EK-TM4C123GXL w/ ENC28J60
// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz

// Hardware configuration:
// ENC28J60 Ethernet controller on SPI0
//   MOSI (SSI0Tx) on PA5
//   MISO (SSI0Rx) on PA4
//   SCLK (SSI0Clk) on PA2
//   ~CS (SW controlled) on PA3
//   WOL on PB3
//   INT on PC6

// Pinning for IoT projects with wireless modules:
// N24L01+ RF transceiver
//   MOSI (SSI0Tx) on PA5
//   MISO (SSI0Rx) on PA4
//   SCLK (SSI0Clk) on PA2
//   ~CS on PE0
//   INT on PB2
// Xbee module
//   DIN (UART1TX) on PC5
//   DOUT (UART1RX) on PC4

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "EEPROM.h"
#include "tm4c123gh6pm.h"
#include "eth0.h"
#include "gpio.h"
#include "spi0.h"
#include "uart0.h"
#include "wait.h"


// Pins
#define RED_LED PORTF,1
#define BLUE_LED PORTF,2
#define GREEN_LED PORTF,3
#define PUSH_BUTTON PORTF,4

uint8_t state;
uint8_t tcpstate;
uint32_t counterTimer =0 ;

bool Conflag = false;
bool Disflag = false;
bool pingflag = false;
bool Pubflag = false;
bool Subflag = false;
bool UnSubflag  = false;
extern bool AvdSYN;
bool SverPubFlag = false;

//bool initflag = true;
//-----------------------------------------------------------------------------
// Subroutines                
//-----------------------------------------------------------------------------

// Initialize Hardware

void initHw()
{
    // Configure HW to work with 16 MHz XTAL, PLL enabled, system clock of 40 MHz
    SYSCTL_RCC_R = SYSCTL_RCC_XTAL_16MHZ | SYSCTL_RCC_OSCSRC_MAIN | SYSCTL_RCC_USESYSDIV | (4 << SYSCTL_RCC_SYSDIV_S);

    SYSCTL_RCGCTIMER_R |= SYSCTL_RCGCTIMER_R4;
    _delay_cycles(3);
    // Configure Timer 4 for 1 sec tick
    TIMER4_CTL_R &= ~TIMER_CTL_TAEN;                 // turn-off timer before reconfiguring
    TIMER4_CFG_R = TIMER_CFG_32_BIT_TIMER;           // configure as 32-bit timer (A+B)
    TIMER4_TAMR_R = TIMER_TAMR_TACDIR;          // configure for periodic mode (count down)
    TIMER4_IMR_R = 0;                                 // turn-off interrupt
    TIMER4_TAV_R = 0;                                //Counter starts from zero
    TIMER4_CTL_R |= TIMER_CTL_TAEN;                  // turn-on timer
    NVIC_EN2_R &= ~(1 << (INT_TIMER4A-80));             // turn-off interrupt

    // Enable clocks
    enablePort(PORTF);
    _delay_cycles(3);

    // Configure LED and pushbutton pins
    selectPinPushPullOutput(RED_LED);
    selectPinPushPullOutput(GREEN_LED);
    selectPinPushPullOutput(BLUE_LED);
    selectPinDigitalInput(PUSH_BUTTON);


}




void displayConnectionInfo()
{
    uint8_t i;
    //char str[10];
    char* str1;
    uint8_t mac[6];
    uint8_t ip[4];
    etherGetMacAddress(mac);
    putsUart0("HW: ");
    //putsUart0("\n\r");
    for (i = 0; i < 6; i++)
    {
        //sprintf(str, "%02x", mac[i]);
        str1 = itostring(mac[i]);
        putsUart0(str1);
        if (i < 6-1)
            putcUart0(':');
    }
    putsUart0("\n\r");
    etherGetIpAddress(ip);
    putsUart0("IP: ");
    for (i = 0; i < 4; i++)
    {
        //sprintf(str, "%u", ip[i]);
        str1 = itostring(ip[i]);
        putsUart0(str1);
        if (i < 4-1)
            putcUart0('.');
    }
    putsUart0("\n\r");
    etherGetMqttBrkIpAddress(ip);
    putsUart0("MQTT BROKER IP: ");
    for (i = 0; i < 4; i++)
    {
        //sprintf(str, "%u", ip[i]);
        str1 = itostring(ip[i]);
        putsUart0(str1);
        if (i < 4-1)
            putcUart0('.');
    }
    putsUart0("\n\r");
    etherGetDNSAddress(ip);
    putsUart0("DNS: ");
    for (i = 0; i < 4; i++)
    {
        //sprintf(str, "%u", ip[i]);
        str1 = itostring(ip[i]);
        putsUart0(str1);
        if (i < 4-1)
            putcUart0(':');
    }
    putsUart0("\n\r");

    if (etherIsLinkUp())
        putsUart0("Link is up\n\r");
    else
        putsUart0("Link is down\n\r");
}



//-----------------------------------------------------------------------------
// Main
//-----------------------------------------------------------------------------

// Max packet is calculated as:
// Ether frame header (18) + Max MTU (1500) + CRC (4)
#define MAX_PACKET_SIZE 1522

int main(void)
{
    uint8_t* udpData;
    uint8_t data[MAX_PACKET_SIZE];
    char* Pub_topic;
    char* Pub_data;
    char* Sub_topic;
    char* UnSub_topic;
    SubTopicFrame.Topic_names = 0;
    char* Pub_server_data;
    uint8_t Switchcase = 0;

    USER_DATA info;



    // Init controller
    initHw();

    // Setup UART0 and EEPROM
    initUart0();
    setUart0BaudRate(115200, 40e6);
    initEeprom();


    // Init ethernet interface (eth0)
    putsUart0("\nStarting e0th0\n");
    putsUart0("\n\r");
    etherSetIpAddress(192,168,1,141);
    etherSetMacAddress(2, 3, 4, 5, 6, 141);
    etherSetMqttBrkIp(readEeprom(0x0020),readEeprom(0x0021), readEeprom(0x0022), readEeprom(0x0023));

    //tcp = true;
    etherInit(ETHER_UNICAST | ETHER_BROADCAST | ETHER_HALFDUPLEX);


    // Flash LED
    setPinValue(GREEN_LED, 1);
    waitMicrosecond(100000);
    setPinValue(GREEN_LED, 0);
    waitMicrosecond(100000);


    // Main Loop
    // RTOS and interrupts would greatly improve this code,
    // but the goal here is simplicity




    while (true)
    {




        // Put terminal processing here
        if (kbhitUart0())
        {
            getsUart0(&info);
            parseFields(&info);


            if(isCommand(&info,"set",2))
            {
                if(stringcmp("mqtt",getFieldString(&info,2)))
                {
                    etherSetMqttBrkIp(getFieldInteger(&info,3), getFieldInteger(&info,4), getFieldInteger(&info,5), getFieldInteger(&info,6));
                    // etherSetIpAddress(getFieldInteger(&info,3), getFieldInteger(&info,4), getFieldInteger(&info,5), getFieldInteger(&info,6));
                    writeEeprom(0x0020,getFieldInteger(&info,3));
                    writeEeprom(0x0021,getFieldInteger(&info,4));
                    writeEeprom(0x0022,getFieldInteger(&info,5));
                    writeEeprom(0x0023,getFieldInteger(&info,6));
                    //                    if((getFieldInteger(&info,3) == 192) && (getFieldInteger(&info,4) == 168) && (getFieldInteger(&info,5) == 1) && (getFieldInteger(&info,6) == 190))
                    //                    {
                    //                        etherEnablemqtt();
                    //                        putsUart0("valid broker IP \n\r");
                    //                    }else{
                    //                        etherDisablemqtt();
                    //                        putsUart0("invalid broker IP \n\r");
                    //                    }
                }

            }

            if(isCommand(&info,"publish",3))
            {
                Pub_topic = getFieldString(&info,2);
                Pub_data = getFieldString(&info,3);
                Pubflag = true;
                tcpstate = TCPCLOSED;
            }

            if(isCommand(&info,"connect",1))
            {
                Conflag = true;
                tcpstate = TCPCLOSED;
            }

            if(isCommand(&info,"subscribe",2))
            {
                Sub_topic = getFieldString(&info,2);
                Subflag = true;
                tcpstate = TCPCLOSED;
            }

            if(isCommand(&info,"unsubscribe",2))
            {

                //                TIMER4_IMR_R &= ~TIMER_IMR_TATOIM;                // turn-off interrupt
                //                NVIC_EN2_R &= ~(1 << (INT_TIMER4A-80)) ;
                UnSub_topic = getFieldString(&info,2);
                UnSubflag = true;
                tcpstate = TCPCLOSED;
            }


            if(isCommand(&info,"disconnect",1))
            {

                Disflag = true;

            }
            if(isCommand(&info,"ifconfig",1))
            {
                displayConnectionInfo();
            }

            if(isCommand(&info,"reboot",1))
            {
                NVIC_APINT_R = 0x05FA0004;
            }
        }

        // Packet processing

        if(tcpstate == TCPCLOSED)
        {
            if(Pubflag || Subflag || UnSubflag || Conflag)
            {
                SendTcpSynmessage(data);
                tcpstate = TCPLISTEN;
            }
        }

        /* if(SverPubFlag)
        {

            SendMqttPingRequest(data);

            //startPeriodicTimer((_callback)toggleFlag,2);
            Switchcase = PING;

          //  NVIC_EN2_R |= 1 << (INT_TIMER4A-80);
        }*/

        if(pingflag)
        {
            if(TIMER4_TAV_R>40e6)
            {
                counterTimer++;
                TIMER4_TAV_R = 0;
                if(counterTimer%2 == 0)
                {
                setPinValue(RED_LED, 1);
                }
                else
                {
                    setPinValue(RED_LED, 0);
                }
               //waitMicrosecond(100000);
                //putsUart0("one second \n\r");
            }

            // _delay_cycles(6);
            //waitMicrosecond(5000000);
            if( counterTimer>30)
            {

                //SverPubFlag = true;
                SendMqttPingRequest(data);
                TIMER4_TAV_R =0;
                counterTimer = 0;
                Switchcase = PING;

            }

        }
        if(Disflag)
        {
            sendMqttDisconnectRequest(data);
            Disflag = false;
            Switchcase = DISCON;
        }

        if (etherIsDataAvailable())
        {
            if (etherIsOverflow())
            {
                setPinValue(RED_LED, 1);
                waitMicrosecond(100000);
                setPinValue(RED_LED, 0);
            }

            // Get packet
            etherGetPacket(data, MAX_PACKET_SIZE);

            // Handle ARP request
            if (etherIsArpRequest(data))
            {
                etherSendArpResponse(data);
            }


            // Handle IP datagram
            if (etherIsIp(data))
            {
                if (etherIsIpUnicast(data))
                {
                    // handle icmp ping request
                    if (etherIsPingRequest(data))
                    {
                        etherSendPingResponse(data);
                    }

                    // Process UDP datagram
                    // test this with a udp send utility like sendip
                    //   if sender IP (-is) is 192.168.1.198, this will attempt to
                    //   send the udp datagram (-d) to 192.168.1.199, port 1024 (-ud)
                    // sudo sendip -p ipv4 -is 192.168.1.198 -p udp -ud 1024 -d "on" 192.168.1.199
                    // sudo sendip -p ipv4 -is 192.168.1.198 -p udp -ud 1024 -d "off" 192.168.1.199
                    if (etherIsUdp(data))
                    {
                        udpData = etherGetUdpData(data);
                        if (stringcmp("on",(char*)udpData))
                            setPinValue(GREEN_LED, 1);
                        if (stringcmp("off",(char*)udpData))
                            setPinValue(GREEN_LED, 0);
                        etherSendUdpResponse(data, (uint8_t*)"Received", 9);

                    }

                }
            }

            if(IsMqttpublishServer(data))
            {
                //                    TIMER4_IMR_R &= ~TIMER_IMR_TATOIM;                // turn-off interrupt
                //                     NVIC_EN2_R &= ~(1 << (INT_TIMER4A-80)) ;
                //stopTimer((_callback)toggleFlag);
                //SverPubFlag = false;
                //etherGetPacket(data, MAX_PACKET_SIZE);
                Pub_server_data = CollectPubData(data);
                putsUart0(Pub_server_data);
                putsUart0("\n\r");
                SendTcpAck1(data);
                //tcpstate = TCPCLOSED;

            }

            if(tcpstate == TCPLISTEN)
            {
                if(IsTcpSynAck(data))
                {
                    //SendTcpSynAckmessage(data);
                    tcpstate =  TCPSYN_RECV;

                }
            }
            if(tcpstate == TCPSYN_RECV)
            {

                SendTcpAck(data);
                _delay_cycles(6);
                SendTcpPushAck(data);
                tcpstate = TCPESTABLISHED;
                //_delay_cycles(6);

            }
            if(tcpstate == TCPESTABLISHED)
            {
                if(IsMqttConnectAck(data))
                {
                    if(Pubflag)//if client is sending publish
                    {
                        SendTcpAck1(data);
                        _delay_cycles(6);
                        SendMqttPublishClient(data,Pub_topic,Pub_data);

                        Switchcase = PUB;
                        //_delay_cycles(6);
                    }

                    if(Subflag)// if client is sending subscribe
                    {
                        SendTcpAck1(data);
                        _delay_cycles(6);
                        SendMqttSubscribeClient(data,Sub_topic);

                        Switchcase = SUB;
                        Subflag = false;
                        //tcpstate = TCPCLOSED;
                    }

                    if(UnSubflag)
                    {
                        SendTcpAck1(data);
                        _delay_cycles(6);
                        SendMqttUnSubscribeClient(data,UnSub_topic);
                        Switchcase = UNSUB;

                    }

                }


                switch(Switchcase)
                {
                case PUB:
                    if(AvdSYN)
                    {
                        if(IsTcpAck(data))//IT comes when QoS level of publish is 0
                        {
                            Pubflag = false;
                            AvdSYN = true;

                            Switchcase = 0;
                        }
                    }

                    if(IsPubAck(data)) // it comes when QoS level of publish is 1
                    {
                        Pubflag = false;

                        // SendMqttPingRequest(data);
                        //waitMicrosecond(1000000);

                        Switchcase = 0;
                    }

                    if(IsPubRec(data)) // it comes when QoS level of publish is 2
                    {
                        // SendTcpAck1(data);
                        //_delay_cycles(6);
                        SendMqttPublishRel(data);
                        Pubflag = false;



                        Switchcase = 0;
                    }

                    break;

                case SUB:
                    if(IsSubAck(data))
                    {

                        SendTcpAck1(data);
                        TIMER4_TAV_R = 0;
                        //putsUart0("hi");
                        Switchcase = 0;
                        pingflag = true;
                        //Subflag = false;
                    }

                    break;
                case UNSUB:
                    if(IsUnsubAck(data))
                    {
                        //                        TIMER4_IMR_R &= ~TIMER_IMR_TATOIM;                // turn-off interrupt
                        //                        NVIC_EN2_R &= ~(1 << (INT_TIMER4A-80)) ;

                        Switchcase = 0;
                        UnSubflag = false;
                        //pingflag = true;
                    }
                    break;

                case PING:

                    if(IsMqttPingResponse(data))
                    {
                        SendTcpAck1(data);
                        Switchcase = 0;
                    }

                    break;

                case DISCON:
                    if(ISTcpFinAck(data))
                    {
                        SendTcpFin(data);
                        Disflag = false;
                        tcpstate = TCPCLOSED;

                    }
                    break;
                default:

                    break;

                }









            }

            /*
             if(tcpstate == TCPFINWAIT1)
             {
                 waitMicrosecond(500000);

                     if(IsTcpAck(data))
                     {
                          SendTcpFin(data);
                          tcpstate = TCPFINWAIT2;
                     }



                 //SendTcpFin(data);
                _delay_cycles(6);

             }

             if(tcpstate == TCPFINWAIT2)
             {
                 if(IsTcpAck(data))
                 {
                     tcpstate = TCPTIMEWAIT;
                    // waitMicrosecond(1000000);
                 }
             }

             if(tcpstate == TCPTIMEWAIT)
             {
                 SendTcpLastAck(data);

                 waitMicrosecond(1000000);
                 tcpstate = TCPCLOSED;
             }
             */

        }


    }
}
>>>>>>> b30f9bb7bc9ab327df81cc943002dc66f211b912
