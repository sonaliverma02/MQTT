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
//#include <string.h>
#include "tm4c123gh6pm.h"
#include "eth0.h"
#include "gpio.h"
#include "spi0.h"
#include "uart0.h"
#include "wait.h"
#include "Timer.h"
#include "mosquitto.h"
#include "mosquitto_plugin.h"

// Pins
#define RED_LED PORTF,1
#define BLUE_LED PORTF,2
#define GREEN_LED PORTF,3
#define PUSH_BUTTON PORTF,4

uint8_t state;
uint8_t tcpstate = TCPCLOSED;

bool Pubflag = false;
bool Subflag = false;


//bool initflag = true;
//-----------------------------------------------------------------------------
// Subroutines                
//-----------------------------------------------------------------------------

// Initialize Hardware

void initHw()
{
    // Configure HW to work with 16 MHz XTAL, PLL enabled, system clock of 40 MHz
    SYSCTL_RCC_R = SYSCTL_RCC_XTAL_16MHZ | SYSCTL_RCC_OSCSRC_MAIN | SYSCTL_RCC_USESYSDIV | (4 << SYSCTL_RCC_SYSDIV_S);

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

void initEeprom()
{
    SYSCTL_RCGCEEPROM_R = 1;
    _delay_cycles(3);
    while (EEPROM_EEDONE_R & EEPROM_EEDONE_WORKING);
}

void writeEeprom(uint16_t add, uint32_t eedata)
{
    EEPROM_EEBLOCK_R = add >> 4;
    EEPROM_EEOFFSET_R = add & 0xF;
    EEPROM_EERDWR_R = eedata;
    while (EEPROM_EEDONE_R & EEPROM_EEDONE_WORKING);
}

uint32_t readEeprom(uint16_t add)
{
    EEPROM_EEBLOCK_R = add >> 4;
    EEPROM_EEOFFSET_R = add & 0xF;
    return EEPROM_EERDWR_R;
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
    SubTopicFrame.Topic_names = 0;

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
                             etherEnablemqtt();
                 }

           }

            if(isCommand(&info,"publish",3))
            {
                Pub_topic = getFieldString(&info,2);
                Pub_data = getFieldString(&info,3);
                Pubflag = true;
            }

            if(isCommand(&info,"subscribe",2))
            {
                Sub_topic = getFieldString(&info,2);
                Subflag = true;
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
            if(Pubflag || Subflag)
            {
                SendTcpSynmessage(data);
                tcpstate = TCPLISTEN;
            }
        }
       // while(1);
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

                         //_delay_cycles(6);
                     }

                     if(Subflag)// if client is sending subscribe
                     {
                         SendTcpAck1(data);
                         _delay_cycles(6);
                         SendMqttSubscribeClient(data,Sub_topic);
                         Subflag = false;
                         tcpstate = TCPCLOSED;
                     }

                 }

                 if(IsPubAck(data)) // it comes when QoS level of publish is 1
                 {
                     Pubflag = false;
                     tcpstate = TCPCLOSED;
                 }

                 if(IsPubRec(data)) // it comes when QoS level of publish is 2
                 {
                    // SendTcpAck1(data);
                     //_delay_cycles(6);
                     SendMqttPublishRel(data);
                     Pubflag = false;
                     tcpstate = TCPCLOSED;
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
