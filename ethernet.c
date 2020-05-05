


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
uint32_t counterTimer = 0;
uint32_t counterTimer2 = 0;

bool Conflag = false;
bool Disflag = false;
bool pingflag = false;
bool Pubflag = false;
bool Subflag = false;
bool UnSubflag  = false;
extern bool AvdSYN;

//-----------------------------------------------------------------------------
// Subroutines                
//-----------------------------------------------------------------------------

// Initialize Hardware

void initHw()
{
    // Configure HW to work with 16 MHz XTAL, PLL enabled, system clock of 40 MHz
    SYSCTL_RCC_R = SYSCTL_RCC_XTAL_16MHZ | SYSCTL_RCC_OSCSRC_MAIN | SYSCTL_RCC_USESYSDIV | (4 << SYSCTL_RCC_SYSDIV_S);

//    SYSCTL_RCC2_R = SYSCTL_RCC2_OSCSRC2_32;
//
//    SYSCTL_RCGCHIB_R |= SYSCTL_RCGCHIB_R0;

    SYSCTL_RCGCADC_R |= SYSCTL_RCGCADC_R0; // ENABLING AD0 and AD1

    SYSCTL_RCGC2_R |= SYSCTL_RCGC2_GPIOE;

    GPIO_PORTE_AFSEL_R |= 0x08;                      // select alternative functions for AN0 (PE3)
    GPIO_PORTE_DEN_R &= ~0x08;                       // turn off digital operation on pin PE3
    GPIO_PORTE_AMSEL_R |= 0x08;                      // turn on analog operation on pin PE3

    // Configure ADC
    ADC0_CC_R = ADC_CC_CS_SYSPLL;                    // select PLL as the time base (not needed, since default value)
    ADC0_ACTSS_R &= ~ADC_ACTSS_ASEN3;                // disable sample sequencer 3 (SS3) for programming
    ADC0_EMUX_R = ADC_EMUX_EM3_PROCESSOR;            // select SS0 bit in ADCPSSI as trigger
    ADC0_SSMUX3_R = 0;                               // set first sample to AIN0
    ADC0_SSCTL3_R = ADC_SSCTL3_END0 | ADC_SSCTL3_TS0;// mark first sample as the end and set TS0 bit get raw value of internal temperature
    ADC0_ACTSS_R |= ADC_ACTSS_ASEN3;

    //configuring timer 4 to send Ping request
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

    //configuring timer 2 to publish internal temperature
    SYSCTL_RCGCTIMER_R |= SYSCTL_RCGCTIMER_R2;
    _delay_cycles(3);
    // Configure Timer 2 for 1 sec tick
    TIMER2_CTL_R &= ~TIMER_CTL_TAEN;                 // turn-off timer before reconfiguring
    TIMER2_CFG_R = TIMER_CFG_32_BIT_TIMER;           // configure as 32-bit timer (A+B)
    TIMER2_TAMR_R = TIMER_TAMR_TACDIR;          // configure for periodic mode (count down)
    TIMER2_IMR_R = 0;                                 // turn-off interrupt
    TIMER2_TAV_R = 0;                                //Counter starts from zero
    TIMER2_CTL_R |= TIMER_CTL_TAEN;                  // turn-on timer
    NVIC_EN0_R &= ~(1 << (INT_TIMER2A-16));             // turn-off interrupt

//    HIB_IM_R  |= HIB_IM_WC;
//    HIB_CTL_R = 0x40;
//    while(HIB_MIS_R & 0x10);

    // Enable clocks
    enablePort(PORTF);
    _delay_cycles(3);

    // Configure LED and pushbutton pins
    selectPinPushPullOutput(RED_LED);
    selectPinPushPullOutput(GREEN_LED);
    selectPinPushPullOutput(BLUE_LED);
    selectPinDigitalInput(PUSH_BUTTON);

}

/*
 * converts the raw value into actual value of internal temperature
 */
int16_t readAdc0Temp()
{
    ADC0_PSSI_R |= ADC_PSSI_SS3;
    while (ADC0_ACTSS_R & ADC_ACTSS_BUSY);           // wait until SS0 is not busy
    return ADC0_SSFIFO3_R;                           // get single result from the FIFO
}

/*
 * Gets the internal temperature value in degree celcius
 */
char* Get_Temp()
{
    int16_t T1 = readAdc0Temp();

    int16_t temp = 147.5 - ((75*T1*3.3)/4096);

    return itostring(temp);

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
    uint8_t data[MAX_PACKET_SIZE];
    char* Pub_topic;
    char* Pub_data;
    char* Sub_topic;
    char* UnSub_topic;
    SubTopicFrame.Topic_names = 0;
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
    putsUart0("\nIt is recommended to set MQTT Broker IP before staring the project\n");
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

            if(isCommand(&info,"temp",1))
            {
                putsUart0(Get_Temp());
            }

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
                if(stringcmp("inputs",getFieldString(&info,2)))
                {
                    putsUart0("INPUTS 1. LED : subscribe led (give this command from putty and publish with topic name led and with data on/off on another mosquitto Client)\r\n");
                    putsUart0("       2. Internal temperature: Temperature sensor will be publishing the temperature data with the topic name temperature for every 50 seconds\r\n");
                    putsUart0("       3. UDP: give the following command in sfk shell (for windows) ----> sfk udpsend 192.168.1.141:5000 -listen ''hello''\r\n");
                    putsUart0("               192.168.1.141 is IP of red board and 5000 is UDP port and hello is a UDP data\r\n");
                }

                if(stringcmp("outputs",getFieldString(&info,2)))
                {
                    putsUart0("OUTPUTS 1. LED: when subscribed to the led topic give the following commands through another mosquitto client (for windows):\r\n");
                    putsUart0("                mosquitto_pub -h (broker IP) -t led -m on ------> If this command is given, then BLUE LED turns on\r\n");
                    putsUart0("                mosquitto_pub -h (broker IP) -t led -m off ------> If this command is given, then BLUE LED turns off\r\n");
                    putsUart0("\r\n");
                    putsUart0("        2. Internal temperature: The incoming temperature data from RED BOARD can be subscribed through another mosquitto client with the topic name temperature\r\n");
                    putsUart0("        3. UDP: when the command is given in sfk shell as mentioned in Inputs, Red Board publishes the UDP data with the topic name udp\r\n");
                }

                if(stringcmp("subs",getFieldString(&info,2)))
                {
                    putsUart0("Subscribed Topics are: \r\n");
                    uint8_t i;
                    for(i = 0; i < 9; i++)
                    {
                        putsUart0(SubTopicFrame.SubTopicArr[i]);
                        putsUart0("\n\r");
                        i++;
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

        /*
         * Publishes internal temperature for every 50 seconds
         */
        if(TIMER2_TAV_R > 40e6)
        {
            counterTimer2++;
            TIMER2_TAV_R = 0;
        }

        if(counterTimer2 > 50)
        {
            tcpstate = TCPCLOSED;
            Pubflag = true;
            Pub_topic = "temperature";
            Pub_data = Get_Temp();
            TIMER2_TAV_R = 0;
            counterTimer2 = 0;
            Switchcase = PUB;
        }

        // Packet processing

        /*
         * Sends TCP SYN
         */

        if(tcpstate == TCPCLOSED)
        {
            if(Pubflag || Subflag || UnSubflag || Conflag)
            {
                SendTcpSynmessage(data);
                tcpstate = TCPLISTEN;
            }
        }

        /*
         * sends Ping request for every 30 seconds
         */
        if(pingflag)
        {
            if(TIMER4_TAV_R > 40e6)
            {
                counterTimer++;
                TIMER4_TAV_R = 0;
            }

            if(counterTimer > 30)
            {
                SendMqttPingRequest(data);
                TIMER4_TAV_R = 0;
                counterTimer = 0;
                Switchcase = PING;
            }
        }

        /*
         * Sends MQTT disconnect
         */
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

            /*
             * sfk udpsend 192.168.1.141:5000 -listen "hello" (for windows)
             *
             * If UDP data is sent by sfk file, the UDP data converts into MQTT and publishes the
             * UDP data with the topic name "udp"
             *
             * Sendip for linux
             */

            if (etherIsUdp(data))
            {
                Pub_data = etherGetUdpData(data);
                tcpstate = TCPCLOSED;
                Pubflag = true;
                Pub_topic = "udp";
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

                }
            }

            /*
             * Returns true when broker publishes the data
             */

            if(IsMqttpublishServer(data))
            {
                Elements pub;
                // collects the data and topic from MQTT broker when it publishes
                pub = CollectPubData(data);
                putsUart0(pub.Data);
                putsUart0("\n\r");
                SendTcpAck1(data);
                /*
                 * IFTT for publish
                 */
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
                else if(stringcmp("udp",pub.topic))
                {
                    etherSendUdpResponse(data,(uint8_t*)pub.Data, 9);
                }
            }

            /*
             * check wether SYN ACk is received
             */
            if(tcpstate == TCPLISTEN)
            {
                if(IsTcpSynAck(data))
                {
                    tcpstate =  TCPSYN_RECV;
                }
            }

            /*
             * Send ACk and Connect command
             */
            if(tcpstate == TCPSYN_RECV)
            {
                SendTcpAck(data);
                _delay_cycles(6);
                SendTcpPushAck(data);
                tcpstate = TCPESTABLISHED;
            }

            if(tcpstate == TCPESTABLISHED)
            {
                /*
                 * check for the states (publish or subscribe or unsubscribe or ping response or Fin ack when disconnect request is sent)
                 */
                switch(Switchcase)
                {
                case PUB:
                    if(AvdSYN)
                    {
                        if(IsTcpAck(data))//it comes when QoS level of publish is 0
                        {
                            Pubflag = false;
                            AvdSYN = true;
                            Switchcase = 0;
                            pingflag = true;
                        }
                    }

                    if(IsPubAck(data)) // it comes when QoS level of publish is 1
                    {
                        SendTcpAck1(data);
                        Pubflag = false;
                        Switchcase = 0;
                        pingflag = true;
                    }

                    if(IsPubRec(data)) // it comes when QoS level of publish is 2
                    {
                        SendMqttPublishRel(data);
                        Pubflag = false;
                    }

                    if(IsPubCom(data))
                    {
                        SendTcpAck1(data);
                        Switchcase = 0;
                        pingflag = true;
                    }
                    break;

                case SUB:
                    if(IsSubAck(data))
                    {
                        SendTcpAck1(data);
                        TIMER4_TAV_R = 0;
                        Switchcase = 0;
                    }
                    break;

                case UNSUB:
                    if(IsUnsubAck(data))
                    {
                        SendTcpAck1(data);
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
                        pingflag = false;
                        SendTcpFin(data);
                        Disflag = false;
                        tcpstate = TCPCLOSED;
                        Switchcase = 0;
                    }
                    break;

                default:

                    break;

                }

                /*
                 * check weather MQTT connect ACK is received
                 */
                if(IsMqttConnectAck(data))
                {
                    SendTcpAck1(data);
                    _delay_cycles(6);

                    //Sends Publish message
                    if(Pubflag)//if client is sending publish
                    {
                        SendMqttPublishClient(data,Pub_topic,Pub_data);
                        Switchcase = PUB;

                    }

                    //Sends Subscribe message
                    if(Subflag)// if client is sending subscribe
                    {
                        SendMqttSubscribeClient(data,Sub_topic);
                        Switchcase = SUB;
                        Subflag = false;

                    }

                    //Sends Unsubscribe message
                    if(UnSubflag)// if client is sending unsubscribe
                    {
                        SendMqttUnSubscribeClient(data,UnSub_topic);
                        Switchcase = UNSUB;
                    }
                }

            }

        }

    }
}
