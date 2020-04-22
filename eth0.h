// ETH0 Library
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

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#ifndef ETH0_H_
#define ETH0_H_

#include <stdint.h>
#include <stdbool.h>

#define ETHER_UNICAST        0x80
#define ETHER_BROADCAST      0x01
#define ETHER_MULTICAST      0x02
#define ETHER_HASHTABLE      0x04
#define ETHER_MAGICPACKET    0x08
#define ETHER_PATTERNMATCH   0x10
#define ETHER_CHECKCRC       0x20

#define ETHER_HALFDUPLEX     0x00
#define ETHER_FULLDUPLEX     0x100

#define LOBYTE(x) ((x) & 0xFF)
#define HIBYTE(x) (((x) >> 8) & 0xFF)

/*
 * DHCP parameters
 */

#define DHCPDISCOVER       1
#define DHCPOFFER          2
#define DHCPREQUEST        3
#define DHCPDECLINCE       4
#define DHCPACK            5
#define DHCPNACK           6
#define DHCPRELEASE        7
#define DHCPINFORM         8
#define TEST_IP            9
#define BOUND              10
#define WAITING            11
#define INIT               12


#define REQUESTING         20

//DHCP OP fields

#define BOOTREQUEST        1
#define BOOTREPLY          2

//TCP

#define TCPCLOSED          21
#define TCPLISTEN          22
#define TCPSYN             23
#define TCPSYN_SENT        24
#define TCPSYN_RECV        25
#define TCPACK             26
#define TCPESTABLISHED     27
#define TCPFINWAIT1        28
#define TCPFINWAIT2        29
#define TCPTIMEWAIT        30


//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

void etherInit(uint16_t mode);
bool etherIsLinkUp();

bool etherIsDataAvailable();
bool etherIsOverflow();
uint16_t etherGetPacket(uint8_t packet[], uint16_t maxSize);
bool etherPutPacket(uint8_t packet[], uint16_t size);

bool etherIsIp(uint8_t packet[]);
bool etherIsIpUnicast(uint8_t packet[]);

bool etherIsPingRequest(uint8_t packet[]);
void etherSendPingResponse(uint8_t packet[]);

bool etherIsArpRequest(uint8_t packet[]);
void etherSendArpResponse(uint8_t packet[]);
void etherSendArpRequest(uint8_t packet[], uint8_t ip[]);

bool etherIsUdp(uint8_t packet[]);
bool IsArpResponse(uint8_t packet[]);
uint8_t* etherGetUdpData(uint8_t packet[]);
void etherSendUdpResponse(uint8_t packet[], uint8_t* udpData, uint8_t udpSize);

void etherEnableDhcpMode();
void etherDisableDhcpMode();
bool etherIsDhcpEnabled();
bool IsMqttEnabled();
void etherEnablemqtt();
void etherDisablemqtt();
bool etherIsIpValid();
bool IsTcpSynAck(uint8_t packet[]);
bool TcpListen(uint8_t packet[]);
bool IsTcpAck(uint8_t packet[]);
bool IsTcpTelnet(uint8_t packet[]);
bool IsTcpFin(uint8_t packet[]);
bool ISTcpFinAck(uint8_t packet[]);
bool IsMqttConnectAck(uint8_t packet[]);
bool IsMqttpublishServer(uint8_t packet[]);

void etherSetIpAddress(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3);
void etherGetIpAddress(uint8_t ip[4]);
void etherSetIpGatewayAddress(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3);
void etherGetIpGatewayAddress(uint8_t ip[4]);
void etherSetIpSubnetMask(uint8_t mask0, uint8_t mask1, uint8_t mask2, uint8_t mask3);
void etherGetIpSubnetMask(uint8_t mask[4]);
void etherSetDNSAddress(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3);
void etherGetDNSAddress(uint8_t ip[4]);
void etherSetMacAddress(uint8_t mac0, uint8_t mac1, uint8_t mac2, uint8_t mac3, uint8_t mac4, uint8_t mac5);
void etherGetMacAddress(uint8_t mac[6]);
void etherSetMqttBrkIp(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3);
void etherGetMqttBrkIpAddress(uint8_t ip[4]);

void SendTcpSynmessage(uint8_t packet[]);
void SendTcpSynAckmessage(uint8_t packet[]);
void SendTcpAck(uint8_t packet[]);
void SendTcpAck1(uint8_t packet[]);
void SendTcpPushAck(uint8_t packet[]);
void SendTcpmessage(uint8_t packet[], uint8_t* tcpData, uint8_t tcpSize);
void SendTcpFin(uint8_t packet[]);
void SendTcpLastAck(uint8_t packet[]);

void SendMqttPublishClient(uint8_t packet[], char* Topic, char* Data);
void SendMqttSubscribeClient(uint8_t packet[], char* Topic);

uint16_t htons(uint16_t value);
#define ntohs htons

#endif
