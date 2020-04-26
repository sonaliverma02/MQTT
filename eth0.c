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

#include <eth0.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "tm4c123gh6pm.h"
#include "wait.h"
#include "gpio.h"
#include "spi0.h"

// Pins
#define CS PORTA,3
#define WOL PORTB,3
#define INT PORTC,6

// Ether registers
#define ERDPTL      0x00
#define ERDPTH      0x01
#define EWRPTL      0x02
#define EWRPTH      0x03
#define ETXSTL      0x04
#define ETXSTH      0x05
#define ETXNDL      0x06
#define ETXNDH      0x07
#define ERXSTL      0x08
#define ERXSTH      0x09
#define ERXNDL      0x0A
#define ERXNDH      0x0B
#define ERXRDPTL    0x0C
#define ERXRDPTH    0x0D
#define ERXWRPTL    0x0E
#define ERXWRPTH    0x0F
#define EIE         0x1B
#define EIR         0x1C
#define RXERIF  0x01
#define TXERIF  0x02
#define TXIF    0x08
#define PKTIF   0x40
#define ESTAT       0x1D
#define CLKRDY  0x01
#define TXABORT 0x02
#define ECON2       0x1E
#define PKTDEC  0x40
#define ECON1       0x1F
#define RXEN    0x04
#define TXRTS   0x08
#define ERXFCON     0x38
#define EPKTCNT     0x39
#define MACON1      0x40
#define MARXEN  0x01
#define RXPAUS  0x04
#define TXPAUS  0x08
#define MACON2      0x41
#define MARST   0x80
#define MACON3      0x42
#define FULDPX  0x01
#define FRMLNEN 0x02
#define TXCRCEN 0x10
#define PAD60   0x20
#define MACON4      0x43
#define MABBIPG     0x44
#define MAIPGL      0x46
#define MAIPGH      0x47
#define MACLCON1    0x48
#define MACLCON2    0x49
#define MAMXFLL     0x4A
#define MAMXFLH     0x4B
#define MICMD       0x52
#define MIIRD   0x01
#define MIREGADR    0x54
#define MIWRL       0x56
#define MIWRH       0x57
#define MIRDL       0x58
#define MIRDH       0x59
#define MAADR1      0x60
#define MAADR0      0x61
#define MAADR3      0x62
#define MAADR2      0x63
#define MAADR5      0x64
#define MAADR4      0x65
#define MISTAT      0x6A
#define MIBUSY  0x01
#define ECOCON      0x75

// Ether phy registers
#define PHCON1      0x00
#define PDPXMD 0x0100
#define PHSTAT1     0x01
#define LSTAT  0x0400
#define PHCON2      0x10
#define HDLDIS 0x0100
#define PHLCON      0x14

// Packets
#define IP_ADD_LENGTH 4
#define HW_ADD_LENGTH 6

// ------------------------------------------------------------------------------
//  Globals
// ------------------------------------------------------------------------------

uint8_t nextPacketLsb = 0x00;
uint8_t nextPacketMsb = 0x00;
uint8_t sequenceId = 1;
uint32_t sum;
uint8_t macAddress[HW_ADD_LENGTH] = {2,3,4,5,6,141};
uint8_t ipAddress[IP_ADD_LENGTH] = {0,0,0,0};
uint8_t ipSubnetMask[IP_ADD_LENGTH] = {255,255,255,0};
uint8_t ipGwAddress[IP_ADD_LENGTH] = {0,0,0,0};
uint8_t DNSAddress[IP_ADD_LENGTH] = {0,0,0,0};
uint8_t MqttBrkipAddress[IP_ADD_LENGTH] = {0,0,0,0};
uint16_t src_prt;
bool    mqttEnabled = false;
bool EtherDhcp = false;
bool AvdSYN = true;
extern bool SverPubFlag;
//ipAddress
uint8_t DhcpIpaddress[4];
uint8_t DhcpipGwAddress[4];

// ------------------------------------------------------------------------------
//  Structures
// ------------------------------------------------------------------------------

// This M4F is little endian (TI hardwired it this way)
// Network byte order is big endian
// Must interpret uint16_t in reverse order

typedef struct _enc28j60Frame // 4-bytes
{
    uint16_t size;
    uint16_t status;
    uint8_t data;
} enc28j60Frame;

typedef struct _etherFrame // 14-bytes
{
    uint8_t destAddress[6];
    uint8_t sourceAddress[6];
    uint16_t frameType;
    uint8_t data;
} etherFrame;

typedef struct _ipFrame // minimum 20 bytes
{
    uint8_t revSize;
    uint8_t typeOfService;
    uint16_t length;
    uint16_t id;
    uint16_t flagsAndOffset;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t headerChecksum;
    uint8_t sourceIp[4];
    uint8_t destIp[4];
} ipFrame;

typedef struct _icmpFrame
{
    uint8_t type;
    uint8_t code;
    uint16_t check;
    uint16_t id;
    uint16_t seq_no;
    uint8_t data;
} icmpFrame;

typedef struct _arpFrame
{
    uint16_t hardwareType;
    uint16_t protocolType;
    uint8_t hardwareSize;
    uint8_t protocolSize;
    uint16_t op;
    uint8_t sourceAddress[6];
    uint8_t sourceIp[4];
    uint8_t destAddress[6];
    uint8_t destIp[4];
} arpFrame;

typedef struct _udpFrame // 8 bytes
{
    uint16_t sourcePort;
    uint16_t destPort;
    uint16_t length;
    uint16_t check;
    uint8_t  data;
} udpFrame;

typedef struct _dhcpFrame
{
    uint8_t op;
    uint8_t htype;
    uint8_t hlen;
    uint8_t hops;
    uint32_t  xid;
    uint16_t secs;
    uint16_t flags;
    uint8_t ciaddr[4];
    uint8_t yiaddr[4];
    uint8_t siaddr[4];
    uint8_t giaddr[4];
    uint8_t chaddr[16];
    uint8_t data[192];
    uint32_t magicCookie;
    uint8_t options[50];
} dhcpFrame;

typedef struct _tcpFrame
{
    uint16_t sourcePort;
    uint16_t destPort;
    uint32_t SeqNum;
    uint32_t AckNum;
    uint16_t DoRF;
    uint16_t WindowSize;
    uint16_t CheckSum;
    uint16_t UrgentPtr;
    //uint8_t  OptionsPad;
    uint8_t  data;
}tcpFrame;

typedef struct _tcpOptions
{
    uint8_t  MSSOption;
    uint8_t  MSSlen;
    uint16_t  MSSval;
    uint8_t  NOP;

}tcpOptions;

typedef struct _mqttFrame
{
    uint8_t cntrl_header;
    uint8_t Rem_length;
    uint8_t Len_header;
    uint8_t payload;

}mqttFrame;

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

// Buffer is configured as follows
// Receive buffer starts at 0x0000 (bottom 6666 bytes of 8K space)
// Transmit buffer at 01A0A (top 1526 bytes of 8K space)

void etherCsOn()
{
    setPinValue(CS, 0);
    __asm (" NOP");                    // allow line to settle
    __asm (" NOP");
    __asm (" NOP");
    __asm (" NOP");
}

void etherCsOff()
{
    setPinValue(CS, 1);
}

void etherWriteReg(uint8_t reg, uint8_t data)
{
    etherCsOn();
    writeSpi0Data(0x40 | (reg & 0x1F));
    readSpi0Data();
    writeSpi0Data(data);
    readSpi0Data();
    etherCsOff();
}

uint8_t etherReadReg(uint8_t reg)
{
    uint8_t data;
    etherCsOn();
    writeSpi0Data(0x00 | (reg & 0x1F));
    readSpi0Data();
    writeSpi0Data(0);
    data = readSpi0Data();
    etherCsOff();
    return data;
}

void etherSetReg(uint8_t reg, uint8_t mask)
{
    etherCsOn();
    writeSpi0Data(0x80 | (reg & 0x1F));
    readSpi0Data();
    writeSpi0Data(mask);
    readSpi0Data();
    etherCsOff();
}

void etherClearReg(uint8_t reg, uint8_t mask)
{
    etherCsOn();
    writeSpi0Data(0xA0 | (reg & 0x1F));
    readSpi0Data();
    writeSpi0Data(mask);
    readSpi0Data();
    etherCsOff();
}

void etherSetBank(uint8_t reg)
{
    etherClearReg(ECON1, 0x03);
    etherSetReg(ECON1, reg >> 5);
}

void etherWritePhy(uint8_t reg, uint16_t data)
{
    etherSetBank(MIREGADR);
    etherWriteReg(MIREGADR, reg);
    etherWriteReg(MIWRL, data & 0xFF);
    etherWriteReg(MIWRH, (data >> 8) & 0xFF);
}

uint16_t etherReadPhy(uint8_t reg)
{
    uint16_t data, dataH;
    etherSetBank(MIREGADR);
    etherWriteReg(MIREGADR, reg);
    etherWriteReg(MICMD, MIIRD);
    waitMicrosecond(11);
    etherSetBank(MISTAT);
    while ((etherReadReg(MISTAT) & MIBUSY) != 0);
    etherSetBank(MICMD);
    etherWriteReg(MICMD, 0);
    data = etherReadReg(MIRDL);
    dataH = etherReadReg(MIRDH);
    data |= (dataH << 8);
    return data;
}

void etherWriteMemStart()
{
    etherCsOn();
    writeSpi0Data(0x7A);
    readSpi0Data();
}

void etherWriteMem(uint8_t data)
{
    writeSpi0Data(data);
    readSpi0Data();
}

void etherWriteMemStop()
{
    etherCsOff();
}

void etherReadMemStart()
{
    etherCsOn();
    writeSpi0Data(0x3A);
    readSpi0Data();
}

uint8_t etherReadMem()
{
    writeSpi0Data(0);
    return readSpi0Data();
}

void etherReadMemStop()
{
    etherCsOff();
}

// Initializes ethernet device
// Uses order suggested in Chapter 6 of datasheet except 6.4 OST which is first here
void etherInit(uint16_t mode)
{
    // Initialize SPI0
    initSpi0(USE_SSI0_RX);
    setSpi0BaudRate(4e6, 40e6);
    setSpi0Mode(0, 0);

    // Enable clocks
    enablePort(PORTA);
    enablePort(PORTB);
    enablePort(PORTC);

    // Configure pins for ethernet module
    selectPinPushPullOutput(CS);
    selectPinDigitalInput(WOL);
    selectPinDigitalInput(INT);

    // make sure that oscillator start-up timer has expired
    while ((etherReadReg(ESTAT) & CLKRDY) == 0) {}

    // disable transmission and reception of packets
    etherClearReg(ECON1, RXEN);
    etherClearReg(ECON1, TXRTS);

    // initialize receive buffer space
    etherSetBank(ERXSTL);
    etherWriteReg(ERXSTL, LOBYTE(0x0000));
    etherWriteReg(ERXSTH, HIBYTE(0x0000));
    etherWriteReg(ERXNDL, LOBYTE(0x1A09));
    etherWriteReg(ERXNDH, HIBYTE(0x1A09));

    // initialize receiver write and read ptrs
    // at startup, will write from 0 to 1A08 only and will not overwrite rd ptr
    etherWriteReg(ERXWRPTL, LOBYTE(0x0000));
    etherWriteReg(ERXWRPTH, HIBYTE(0x0000));
    etherWriteReg(ERXRDPTL, LOBYTE(0x1A09));
    etherWriteReg(ERXRDPTH, HIBYTE(0x1A09));
    etherWriteReg(ERDPTL, LOBYTE(0x0000));
    etherWriteReg(ERDPTH, HIBYTE(0x0000));

    // setup receive filter
    // always check CRC, use OR mode
    etherSetBank(ERXFCON);
    etherWriteReg(ERXFCON, (mode | ETHER_CHECKCRC) & 0xFF);

    // bring mac out of reset
    etherSetBank(MACON2);
    etherWriteReg(MACON2, 0);

    // enable mac rx, enable pause control for full duplex
    etherWriteReg(MACON1, TXPAUS | RXPAUS | MARXEN);

    // enable padding to 60 bytes (no runt packets)
    // add crc to tx packets, set full or half duplex
    if ((mode & ETHER_FULLDUPLEX) != 0)
        etherWriteReg(MACON3, FULDPX | FRMLNEN | TXCRCEN | PAD60);
    else
        etherWriteReg(MACON3, FRMLNEN | TXCRCEN | PAD60);

    // leave MACON4 as reset

    // set maximum rx packet size
    etherWriteReg(MAMXFLL, LOBYTE(1518));
    etherWriteReg(MAMXFLH, HIBYTE(1518));

    // set back-to-back inter-packet gap to 9.6us
    if ((mode & ETHER_FULLDUPLEX) != 0)
        etherWriteReg(MABBIPG, 0x15);
    else
        etherWriteReg(MABBIPG, 0x12);

    // set non-back-to-back inter-packet gap registers
    etherWriteReg(MAIPGL, 0x12);
    etherWriteReg(MAIPGH, 0x0C);

    // leave collision window MACLCON2 as reset

    // setup mac address
    etherSetBank(MAADR0);
    etherWriteReg(MAADR5, macAddress[0]);
    etherWriteReg(MAADR4, macAddress[1]);
    etherWriteReg(MAADR3, macAddress[2]);
    etherWriteReg(MAADR2, macAddress[3]);
    etherWriteReg(MAADR1, macAddress[4]);
    etherWriteReg(MAADR0, macAddress[5]);

    // initialize phy duplex
    if ((mode & ETHER_FULLDUPLEX) != 0)
        etherWritePhy(PHCON1, PDPXMD);
    else
        etherWritePhy(PHCON1, 0);

    // disable phy loopback if in half-duplex mode
    etherWritePhy(PHCON2, HDLDIS);

    // Flash LEDA and LEDB
    etherWritePhy(PHLCON, 0x0880);
    waitMicrosecond(100000);

    // set LEDA (link status) and LEDB (tx/rx activity)
    // stretch LED on to 40ms (default)
    etherWritePhy(PHLCON, 0x0472);
    // enable reception
    etherSetReg(ECON1, RXEN);
}

// Returns true if link is up
bool etherIsLinkUp()
{
    return (etherReadPhy(PHSTAT1) & LSTAT) != 0;
}

// Returns TRUE if packet received
bool etherIsDataAvailable()
{
    return ((etherReadReg(EIR) & PKTIF) != 0);
}

// Returns true if rx buffer overflowed after correcting the problem
bool etherIsOverflow()
{
    bool err;
    err = (etherReadReg(EIR) & RXERIF) != 0;
    if (err)
        etherClearReg(EIR, RXERIF);
    return err;
}

// Returns up to max_size characters in data buffer
// Returns number of bytes copied to buffer
// Contents written are 16-bit size, 16-bit status, payload excl crc
uint16_t etherGetPacket(uint8_t packet[], uint16_t maxSize)
{
    uint16_t i = 0, size, tmp16, status;

    // enable read from FIFO buffers
    etherReadMemStart();

    // get next packet information
    nextPacketLsb = etherReadMem();
    nextPacketMsb = etherReadMem();

    // calc size
    // don't return crc, instead return size + status, so size is correct
    size = etherReadMem();
    tmp16 = etherReadMem();
    size |= (tmp16 << 8);

    // get status (currently unused)
    status = etherReadMem();
    tmp16 = etherReadMem();
    status |= (tmp16 << 8);

    // copy data
    if (size > maxSize)
        size = maxSize;
    while (i < size)
        packet[i++] = etherReadMem();

    // end read from FIFO buffers
    etherReadMemStop();

    // advance read pointer
    etherSetBank(ERXRDPTL);
    etherWriteReg(ERXRDPTL, nextPacketLsb); // hw ptr
    etherWriteReg(ERXRDPTH, nextPacketMsb);
    etherWriteReg(ERDPTL, nextPacketLsb);   // dma rd ptr
    etherWriteReg(ERDPTH, nextPacketMsb);

    // decrement packet counter so that PKTIF is maintained correctly
    etherSetReg(ECON2, PKTDEC);

    return size;
}

// Writes a packet
bool etherPutPacket(uint8_t packet[], uint16_t size)
{
    uint16_t i;

    // clear out any tx errors
    if ((etherReadReg(EIR) & TXERIF) != 0)
    {
        etherClearReg(EIR, TXERIF);
        etherSetReg(ECON1, TXRTS);
        etherClearReg(ECON1, TXRTS);
    }

    // set DMA start address
    etherSetBank(EWRPTL);
    etherWriteReg(EWRPTL, LOBYTE(0x1A0A));
    etherWriteReg(EWRPTH, HIBYTE(0x1A0A));

    // start FIFO buffer write
    etherWriteMemStart();

    // write control byte
    etherWriteMem(0);

    // write data
    for (i = 0; i < size; i++)
        etherWriteMem(packet[i]);

    // stop write
    etherWriteMemStop();

    // request transmit
    etherWriteReg(ETXSTL, LOBYTE(0x1A0A));
    etherWriteReg(ETXSTH, HIBYTE(0x1A0A));
    etherWriteReg(ETXNDL, LOBYTE(0x1A0A+size));
    etherWriteReg(ETXNDH, HIBYTE(0x1A0A+size));
    etherClearReg(EIR, TXIF);
    etherSetReg(ECON1, TXRTS);

    // wait for completion
    while ((etherReadReg(ECON1) & TXRTS) != 0);

    // determine success
    return ((etherReadReg(ESTAT) & TXABORT) == 0);
}

// Calculate sum of words
// Must use getEtherChecksum to complete 1's compliment addition
void etherSumWords(void* data, uint16_t sizeInBytes)
{
    uint8_t* pData = (uint8_t*)data;
    uint16_t i;
    uint8_t phase = 0;
    uint16_t data_temp;
    for (i = 0; i < sizeInBytes; i++)
    {
        if (phase)
        {
            data_temp = *pData;
            sum += data_temp << 8;
        }
        else
            sum += *pData;
        phase = 1 - phase;
        pData++;
    }
}

// Completes 1's compliment addition by folding carries back into field
uint16_t getEtherChecksum()
{
    uint16_t result;
    // this is based on rfc1071
    while ((sum >> 16) > 0)
        sum = (sum & 0xFFFF) + (sum >> 16);
    result = sum & 0xFFFF;
    return ~result;
}

void etherCalcIpChecksum(ipFrame* ip)
{
    // 32-bit sum over ip header
    sum = 0;
    etherSumWords(&ip->revSize, 10);
    etherSumWords(ip->sourceIp, ((ip->revSize & 0xF) * 4) - 12);
    ip->headerChecksum = getEtherChecksum();
}

// Converts from host to network order and vice versa
uint16_t htons(uint16_t value)
{
    return ((value & 0xFF00) >> 8) + ((value & 0x00FF) << 8);
}
#define ntohs htons

uint32_t htons32(uint32_t value)
{
    return (((value & 0xFF000000) >> 24) + ((value & 0x00FF0000) >> 8) + ((value & 0x0000FF00) << 8) +  ((value & 0x000000FF) << 24));
}

#define ntohs32 htons32

// Determines whether packet is IP datagram
bool etherIsIp(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    bool ok;
    ok = (ether->frameType == htons(0x0800));
    if (ok)
    {
        sum = 0;
        etherSumWords(&ip->revSize, (ip->revSize & 0xF) * 4);
        ok = (getEtherChecksum() == 0);
    }
    return ok;
}

// Determines whether packet is unicast to this ip
// Must be an IP packet
bool etherIsIpUnicast(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    uint8_t i = 0;
    bool ok = true;
    while (ok & (i < IP_ADD_LENGTH))
    {
        ok = (ip->destIp[i] == ipAddress[i]);
        i++;
    }
    return ok;
}

// Determines whether packet is ping request
// Must be an IP packet
bool etherIsPingRequest(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    icmpFrame* icmp = (icmpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    return (ip->protocol == 0x01 & icmp->type == 8);
}

// Sends a ping response given the request data
void etherSendPingResponse(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    icmpFrame* icmp = (icmpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    uint8_t i, tmp;
    uint16_t icmp_size;
    // swap source and destination fields
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        tmp = ether->destAddress[i];
        ether->destAddress[i] = ether->sourceAddress[i];
        ether->sourceAddress[i] = tmp;
    }
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        tmp = ip->destIp[i];
        ip->destIp[i] = ip ->sourceIp[i];
        ip->sourceIp[i] = tmp;
    }
    // this is a response
    icmp->type = 0;
    // calc icmp checksum
    sum = 0;
    etherSumWords(&icmp->type, 2);
    icmp_size = ntohs(ip->length);
    icmp_size -= 24; // sub ip header and icmp code, type, and check
    etherSumWords(&icmp->id, icmp_size);
    icmp->check = getEtherChecksum();
    // send packet
    etherPutPacket((uint8_t*)ether, 14 + ntohs(ip->length));
}

// Determines whether packet is ARP
bool etherIsArpRequest(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    arpFrame* arp = (arpFrame*)&ether->data;
    bool ok;
    uint8_t i = 0;
    ok = (ether->frameType == htons(0x0806));
    while (ok & (i < IP_ADD_LENGTH))
    {
        ok = (arp->destIp[i] == ipAddress[i]);
        i++;
    }
    if (ok)
        ok = (arp->op == htons(1));
    return ok;
}

bool IsArpResponse(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    arpFrame* arp = (arpFrame*)&ether->data;
    bool ok;
    uint8_t i = 0;
    ok = (ether->frameType == htons(0x0806));
    while (ok & (i < IP_ADD_LENGTH))
    {
        ok = (arp->destIp[i] == DhcpIpaddress[i]);
        i++;
    }
    if (ok)
        ok = (arp->op == htons(2));
    return ok;

}


// Sends an ARP response given the request data
void etherSendArpResponse(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    arpFrame* arp = (arpFrame*)&ether->data;
    uint8_t i, tmp;
    // set op to response
    arp->op = htons(2);
    // swap source and destination fields
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        arp->destAddress[i] = arp->sourceAddress[i];
        ether->destAddress[i] = ether->sourceAddress[i];
        ether->sourceAddress[i] = arp->sourceAddress[i] = macAddress[i];
    }
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        tmp = arp->destIp[i];
        arp->destIp[i] = arp->sourceIp[i];
        arp->sourceIp[i] = tmp;
    }
    // send packet
    etherPutPacket((uint8_t*)ether, 42);
}

// Sends an ARP request
void etherSendArpRequest(uint8_t packet[], uint8_t ip[])
{
    etherFrame* ether = (etherFrame*)packet;
    arpFrame* arp = (arpFrame*)&ether->data;
    uint8_t i;
    // fill ethernet frame
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        ether->destAddress[i] = 0xFF;
        ether->sourceAddress[i] = macAddress[i];
    }
    ether->frameType = 0x0608;
    // fill arp frame
    arp->hardwareType = htons(1);
    arp->protocolType = htons(0x0800);
    arp->hardwareSize = HW_ADD_LENGTH;
    arp->protocolSize = IP_ADD_LENGTH;
    arp->op = htons(1);
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        arp->sourceAddress[i] = macAddress[i];
        arp->destAddress[i] = 0xFF;
    }
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        arp->sourceIp[i] = ipAddress[i];
        arp->destIp[i] = ip[i];
    }
    // send packet

    etherPutPacket((uint8_t*)ether, 42);
}

// Determines whether packet is UDP datagram
// Must be an IP packet
bool etherIsUdp(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    udpFrame* udp = (udpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    bool ok;
    uint16_t tmp16;
    ok = (ip->protocol == 0x11);
    if (ok)
    {
        // 32-bit sum over pseudo-header
        sum = 0;
        etherSumWords(ip->sourceIp, 8);
        tmp16 = ip->protocol;
        sum += (tmp16 & 0xff) << 8;
        etherSumWords(&udp->length, 2);
        // add udp header and data
        etherSumWords(udp, ntohs(udp->length));
        ok = (getEtherChecksum() == 0);
    }
    return ok;
}


bool TcpListen(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    ip->revSize = 0x45;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    bool ok;
    uint8_t x;

    ok = (ip->protocol == 6); // if it is TCP

    tcp->sourcePort = htons(tcp->sourcePort);
    tcp->DoRF = htons(tcp->DoRF);

    x = (tcp->DoRF & 0xFF); //choose flags

    if(ok)
    {
        ok = (ether->destAddress[0] == 2);
        ok &= (ether->destAddress[1] == 3);
        ok &= (ether->destAddress[2] == 4);
        ok &= (ether->destAddress[3] == 5);
        ok &= (ether->destAddress[4] == 6);
        ok &= (ether->destAddress[5] == 141);

        //ok &= (tcp->destPort == htons(23));

        ok &= (x == 0x02);  //if it is SYN
    }

    return ok;
}

bool IsTcpSynAck(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    ip->revSize = 0x45;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    bool ok;
    uint8_t x;

    ok = (ip->protocol == 6); // if it is TCP

    tcp->sourcePort = htons(tcp->sourcePort);
    tcp->DoRF = htons(tcp->DoRF);

    x = (tcp->DoRF & 0xFF); //choose flags

    if(ok)
    {
        ok = (ether->destAddress[0] == 2);
        ok &= (ether->destAddress[1] == 3);
        ok &= (ether->destAddress[2] == 4);
        ok &= (ether->destAddress[3] == 5);
        ok &= (ether->destAddress[4] == 6);
        ok &= (ether->destAddress[5] == 141);

        //ok &= (tcp->destPort == htons(23));

        ok &= (x == 0x12);  //if it is SYN ACK
    }

    return ok;
}

bool IsTcpAck(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    ip->revSize = 0x45;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    bool ok;

    ok = (ether->destAddress[0] == 2);
    ok &= (ether->destAddress[1] == 3);
    ok &= (ether->destAddress[2] == 4);
    ok &= (ether->destAddress[3] == 5);
    ok &= (ether->destAddress[4] == 6);
    ok &= (ether->destAddress[5] == 141);

    uint8_t x;

    tcp->sourcePort = htons(tcp->sourcePort);

    if(ok)
    {
        tcp->DoRF = htons(tcp->DoRF);

        x = (tcp->DoRF & 0xFF); //choose flags

        ok &= (x == 0x10);  // if it is ACK

    }

    return ok;
}

uint32_t PayloadSize = 0;
bool IsTcpTelnet(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    ip->revSize = 0x45;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    bool ok;

    ok = (ether->destAddress[0] == 2);
    ok &= (ether->destAddress[1] == 3);
    ok &= (ether->destAddress[2] == 4);
    ok &= (ether->destAddress[3] == 5);
    ok &= (ether->destAddress[4] == 6);
    ok &= (ether->destAddress[5] == 141);


    uint32_t x;

    tcp->sourcePort = htons(tcp->sourcePort);

    if(ok)
    {
        x = (tcp->DoRF & 0xFF00); //choose flags
        ok &= (x == 0x1800);  // if it is PSH ACK

        //IP length - IP header size - TCP frame size
        PayloadSize = htons(ip->length) - 20 - 20;
    }

    return ok;
}

bool IsTcpFin(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    ip->revSize = 0x45;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    bool ok;

    ok = (ether->destAddress[0] == 2);
    ok &= (ether->destAddress[1] == 3);
    ok &= (ether->destAddress[2] == 4);
    ok &= (ether->destAddress[3] == 5);
    ok &= (ether->destAddress[4] == 6);
    ok &= (ether->destAddress[5] == 141);


    uint32_t x;

    if(ok)
    {
        x = (tcp->DoRF & 0xFF00); //choose flags
        ok &= (x == 0x0100);  // if it is FIN
    }

    return ok;

}

bool ISTcpFinAck(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    ip->revSize = 0x45;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    bool ok;

    ok = (ether->destAddress[0] == 2);
    ok &= (ether->destAddress[1] == 3);
    ok &= (ether->destAddress[2] == 4);
    ok &= (ether->destAddress[3] == 5);
    ok &= (ether->destAddress[4] == 6);
    ok &= (ether->destAddress[5] == 141);


    uint32_t x;

    if(ok)
    {
        tcp->DoRF = htons(tcp->DoRF);
        x = (tcp->DoRF & 0xFF); //choose flags
        ok &= (x == 0x11);  // if it is FIN ACK
    }

    return ok;
}

bool IsMqttConnectAck(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    ip->revSize = 0x45;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    bool ok;

    ok = (ether->destAddress[0] == 2);
    ok &= (ether->destAddress[1] == 3);
    ok &= (ether->destAddress[2] == 4);
    ok &= (ether->destAddress[3] == 5);
    ok &= (ether->destAddress[4] == 6);
    ok &= (ether->destAddress[5] == 141);

    uint8_t* copydata = &tcp->data;

    if(ok)
    {
        ok = (copydata[0] == 0x20); // conforming connect Ack
        ok &= (copydata[2] == 0);   // return code

        PayloadSize = htons(ip->length) - 20 - 20;
    }

    return ok;
}

bool IsMqttpublishServer(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    ip->revSize = 0x45;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    bool ok;

    ok = (ether->destAddress[0] == 2);
    ok &= (ether->destAddress[1] == 3);
    ok &= (ether->destAddress[2] == 4);
    ok &= (ether->destAddress[3] == 5);
    ok &= (ether->destAddress[4] == 6);
    ok &= (ether->destAddress[5] == 141);

    uint8_t* copydata = &tcp->data;
    //tcp->sourcePort = htons(tcp->sourcePort);

    uint8_t Mask = (copydata[0] & 0xF0); // mask QoS part and retain command type

    if(ok)
    {
        ok = (Mask == 0x30); // compare with publish command
        PayloadSize = htons(ip->length) - 20 - 20;
    }

    return ok;
}

bool IsPubAck(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    ip->revSize = 0x45;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    bool ok;

    ok = (ether->destAddress[0] == 2);
    ok &= (ether->destAddress[1] == 3);
    ok &= (ether->destAddress[2] == 4);
    ok &= (ether->destAddress[3] == 5);
    ok &= (ether->destAddress[4] == 6);
    ok &= (ether->destAddress[5] == 141);

    uint8_t* copydata = &tcp->data;

    copydata[0] = (copydata[0] & 0xF0); // mask QoS part and retain command type

    if(ok)
    {
        ok = (copydata[0] == 0x40); // compare with publish Ack command
    }

    return ok;
}

bool IsPubRec(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    ip->revSize = 0x45;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    bool ok;

    ok = (ether->destAddress[0] == 2);
    ok &= (ether->destAddress[1] == 3);
    ok &= (ether->destAddress[2] == 4);
    ok &= (ether->destAddress[3] == 5);
    ok &= (ether->destAddress[4] == 6);
    ok &= (ether->destAddress[5] == 141);

    uint8_t* copydata = &tcp->data;

    copydata[0] = (copydata[0] & 0xF0); // mask QoS part and retain command type

    if(ok)
    {
        ok = (copydata[0] == 0x50); // compare with publish Release command
        PayloadSize = htons(ip->length) - 20 - 20;
    }

    return ok;
}

bool IsSubAck(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    ip->revSize = 0x45;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    bool ok;

    ok = (ether->destAddress[0] == 2);
    ok &= (ether->destAddress[1] == 3);
    ok &= (ether->destAddress[2] == 4);
    ok &= (ether->destAddress[3] == 5);
    ok &= (ether->destAddress[4] == 6);
    ok &= (ether->destAddress[5] == 141);

    uint8_t* copydata = &tcp->data;

    if(ok)
    {
        ok = (copydata[0] == 0x90); // compare with Subscribe Ack
        PayloadSize = htons(ip->length) - 20 - 20;
    }

    return ok;
}

bool IsMqttPingResponse(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    ip->revSize = 0x45;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    bool ok;

    ok = (ether->destAddress[0] == 2);
    ok &= (ether->destAddress[1] == 3);
    ok &= (ether->destAddress[2] == 4);
    ok &= (ether->destAddress[3] == 5);
    ok &= (ether->destAddress[4] == 6);
    ok &= (ether->destAddress[5] == 141);

    uint8_t* copydata = &tcp->data;

    if(ok)
    {
        ok = (copydata[0] == 0xD0); // compare with Subscribe Ack
        PayloadSize = htons(ip->length) - 20 - 20;
    }

    return ok;
}

// Gets pointer to UDP payload of frame
uint8_t* etherGetUdpData(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    udpFrame* udp = (udpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    return &udp->data;
}

// Send responses to a udp datagram 
// destination port, ip, and hardware address are extracted from provided data
// uses destination port of received packet as destination of this packet
void etherSendUdpResponse(uint8_t packet[], uint8_t* udpData, uint8_t udpSize)
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    udpFrame* udp = (udpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    uint8_t *copyData;
    uint8_t i, tmp8;
    uint16_t tmp16;
    // swap source and destination fields
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        tmp8 = ether->destAddress[i];
        ether->destAddress[i] = ether->sourceAddress[i];
        ether->sourceAddress[i] = tmp8;
    }
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        tmp8 = ip->destIp[i];
        ip->destIp[i] = ip->sourceIp[i];
        ip->sourceIp[i] = tmp8;
    }
    // set source port of resp will be dest port of req
    // dest port of resp will be left at source port of req
    // unusual nomenclature, but this allows a different tx
    // and rx port on other machine
    udp->sourcePort = udp->destPort;
    // adjust lengths
    ip->length = htons(((ip->revSize & 0xF) * 4) + 8 + udpSize);
    // 32-bit sum over ip header
    sum = 0;
    etherSumWords(&ip->revSize, 10);
    etherSumWords(ip->sourceIp, ((ip->revSize & 0xF) * 4) - 12);
    ip->headerChecksum = getEtherChecksum();
    udp->length = htons(8 + udpSize);
    // copy data
    copyData = &udp->data;
    for (i = 0; i < udpSize; i++)
        copyData[i] = udpData[i];
    // 32-bit sum over pseudo-header
    sum = 0;
    etherSumWords(ip->sourceIp, 8);
    tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    etherSumWords(&udp->length, 2);
    // add udp header except crc
    etherSumWords(udp, 6);
    etherSumWords(&udp->data, udpSize);
    udp->check = getEtherChecksum();

    // send packet with size = ether + udp hdr + ip header + udp_size
    etherPutPacket((uint8_t*)ether, 22 + ((ip->revSize & 0xF) * 4) + udpSize);
}

static uint8_t stringLen(char* str)
{
    int count = 0;

    while(str[count]!=0)
    {
        count++;
    }
     return count;
}

static void stringCopy(char str1[], char str2[])
{
    uint8_t i;

    for(i=0; str2[i] != '\0'; ++i)
    {
        str1[i] = str2[i];
    }
    str1[i] = '\0';
}

static bool strngcomp(char str1[], char str2[])
{
    uint8_t i=0;
    while(str1[i]==str2[i])
    {
        if(str1[i]=='\0'||str2[i]=='\0')
            break;
        i++;
    }
    if(str1[i]=='\0' && str2[i]=='\0')
        return true;
    else
        return false;

}

static uint16_t MyRand(uint16_t start_range,uint16_t end_range)
{
    static uint16_t rand = 0xACE1; /* Any nonzero start state will work. */

    /*check for valid range.*/
    if(start_range == end_range) {
        return start_range;
    }

    /*get the random in end-range.*/
    rand += 0x3AD;
    rand %= (UINT16_MAX - end_range)  + end_range;

    /*get the random in start-range.*/
    while(rand < start_range){
        rand = rand + end_range - start_range;
    }

    return rand;
}

void SendTcpSynmessage(uint8_t packet[])
{

    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    ip->revSize = 0x45;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    tcpOptions* tcpopt = (tcpOptions*)&tcp->data;

    uint8_t flags,Offset;
    uint16_t x = 24;
    uint16_t a;

    //uint8_t* tcpoptions;

    // populating ether frame
    ether->destAddress[0] = 0x8c;
    ether->destAddress[1] = 0x16;
    ether->destAddress[2] = 0x45;
    ether->destAddress[3] = 0xd7;
    ether->destAddress[4] = 0x51;
    ether->destAddress[5] = 0x2f;

    ether->sourceAddress[0] = 2;
    ether->sourceAddress[1] = 3;
    ether->sourceAddress[2] = 4;
    ether->sourceAddress[3] = 5;
    ether->sourceAddress[4] = 6;
    ether->sourceAddress[5] = 141;

    ether->frameType = htons(0x0800);

    //populating IP field
    ip->typeOfService = 0;
    ip->ttl = 128;
    ip->protocol = 6; // TCP
    ip->id = 0;
    ip->flagsAndOffset = htons(0x4000); //don't fragment

    ip->sourceIp[0] = 192;
    ip->sourceIp[1] = 168;
    ip->sourceIp[2] = 1;
    ip->sourceIp[3] = 141;

    ip->destIp[0] = 192;
    ip->destIp[1] = 168;
    ip->destIp[2] = 1;
    ip->destIp[3] = 190;

    src_prt = htons(MyRand(1000,3000));

    //populating TCP
    tcp->destPort = htons(1883);
    tcp->sourcePort = src_prt;

    tcp->AckNum = 0;
    tcp->SeqNum = 0x00000000;

    Offset = x >> 2;
    flags = 0x02; // for SYN
    a = (Offset << 12) + flags;
    tcp->DoRF = htons(a);
    tcp->WindowSize = htons(1280);
    tcp->CheckSum = 0;
    tcp->UrgentPtr = 0;

    //populating TCP options

    tcpopt->MSSOption = 2;
    tcpopt->MSSlen = 4;
    tcpopt->MSSval = htons(1280);


    ip->length = htons(((ip->revSize & 0xF) * 4) + 20 + 4);
    //Ip checksum
    sum=0;
    etherSumWords(&ip->revSize, 10);
    etherSumWords(ip->sourceIp, ((ip->revSize & 0xF) * 4) - 12);
    ip->headerChecksum = getEtherChecksum();

    //TCP checksum

    uint16_t tmp16;

    uint16_t tcpLen = htons(20+4);
    sum = 0;
    etherSumWords(ip->sourceIp, 8);

    tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    etherSumWords(&tcpLen,2);

    etherSumWords(tcp, 20+4);
    //etherSumWords(tcpopt,4);
    // etherSumWords(&tcp->data, 0);
    tcp->CheckSum = getEtherChecksum();

    etherPutPacket((uint8_t*)ether, 14 + ((ip->revSize & 0xF) * 4) +  20 + 4);
}

/*
void SendTcpSynAckmessage(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    ip->revSize = 0x45;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    tcpOptions* tcpopt;

    uint8_t i,flags,Offset;
    uint16_t x = 24;
    uint16_t a;

    //uint8_t* tcpoptions;

    // populating ether frame
    for(i = 0; i < HW_ADD_LENGTH; i++)
    {
        ether->destAddress[i] = ether->sourceAddress[i];
    }

    ether->sourceAddress[0] = 2;
    ether->sourceAddress[1] = 3;
    ether->sourceAddress[2] = 4;
    ether->sourceAddress[3] = 5;
    ether->sourceAddress[4] = 6;
    ether->sourceAddress[5] = 141;

    ether->frameType = htons(0x0800);

    //populating IP field
    ip->typeOfService = 0;
    ip->ttl = 55;
    ip->protocol = 6; // TCP
    ip->id = 0;

    uint8_t temp8;
    for(i = 0; i < IP_ADD_LENGTH; i++)
    {
        temp8 = ip->destIp[i];
        ip->destIp[i] = ip->sourceIp[i];
        ip->sourceIp[i] = temp8;
    }


    //populating TCP
    tcp->destPort = ntohs(tcp->sourcePort);
    tcp->sourcePort = htons(1883);

    tcp->AckNum = tcp->SeqNum + htons32(0x00000001);
    tcp->SeqNum = 0x00000000;

    Offset = x >> 2;
    flags = 0x12; // for SYN and ACK
    a = (Offset << 12) + flags;
    tcp->DoRF = htons(a);
    tcp->WindowSize = htons(1280);
    tcp->CheckSum = 0;
    tcp->UrgentPtr = 0;

    //populating TCP options

    tcpopt->MSSOption = 2;
    tcpopt->MSSlen = 4;
    tcpopt->MSSval = htons(0x0500);

    ip->length = htons(((ip->revSize & 0xF) * 4) + 20 + 4);
    //Ip checksum
    sum=0;
    etherSumWords(&ip->revSize, 10);
    etherSumWords(ip->sourceIp, ((ip->revSize & 0xF) * 4) - 12);
    ip->headerChecksum = getEtherChecksum();

    //TCP checksum

    uint16_t tmp16;

    uint16_t tcpLen = htons(20+4);
    sum = 0;
    etherSumWords(ip->sourceIp, 8);

    tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    etherSumWords(&tcpLen,2);

    etherSumWords(tcp, 20+4);
    //etherSumWords(tcpopt,4);
   // etherSumWords(&tcp->data, 0);
    tcp->CheckSum = getEtherChecksum();

    etherPutPacket((uint8_t*)ether, 14 + ((ip->revSize & 0xF) * 4) +  20 + 4);
}
 */
void SendTcpAck(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    ip->revSize = 0x45;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    uint8_t i,flags,Offset;
    uint16_t x = 20; // total header length
    uint16_t a;

    // populating ether frame
    ether->destAddress[0] = 0x8c;
    ether->destAddress[1] = 0x16;
    ether->destAddress[2] = 0x45;
    ether->destAddress[3] = 0xd7;
    ether->destAddress[4] = 0x51;
    ether->destAddress[5] = 0x2f;

    ether->sourceAddress[0] = 2;
    ether->sourceAddress[1] = 3;
    ether->sourceAddress[2] = 4;
    ether->sourceAddress[3] = 5;
    ether->sourceAddress[4] = 6;
    ether->sourceAddress[5] = 141;

    ether->frameType = htons(0x0800);

    //populating IP field
    ip->typeOfService = 0;
    ip->ttl = 55;
    ip->protocol = 6; // TCP
    ip->id = 0;
    uint8_t temp8;
    for(i = 0; i < IP_ADD_LENGTH; i++)
    {
        temp8 = ip->destIp[i];
        ip->destIp[i] = ip->sourceIp[i];
        ip->sourceIp[i] = temp8;
    }
    /*
    for(i = 0; i < IP_ADD_LENGTH; i++)
    {
        ip->sourceIp[i] = DhcpIpaddress[i];
    }
     */
    //populating TCP
    uint32_t temp32;
    uint16_t temp16;

    temp16 = tcp->destPort;
    tcp->destPort = htons(tcp->sourcePort);
    tcp->sourcePort = temp16;


    temp32 = tcp->AckNum;
    tcp->AckNum = tcp->SeqNum;
    tcp->SeqNum = temp32;

    tcp->AckNum = tcp->AckNum + htons32(1);

    Offset = x >> 2;
    flags = 0x10; // ACK
    a = (Offset << 12) + flags;
    tcp->DoRF = htons(a);
    tcp->WindowSize = htons(1280);
    tcp->CheckSum = 0;
    tcp->UrgentPtr = 0;

    ip->length = htons(((ip->revSize & 0xF) * 4) + 20);
    //Ip checksum
    sum=0;
    etherSumWords(&ip->revSize, 10);
    etherSumWords(ip->sourceIp, ((ip->revSize & 0xF) * 4) - 12);
    ip->headerChecksum = getEtherChecksum();

    //TCP checksum

    uint16_t tmp16;

    uint16_t tcpLen = htons(20);
    sum = 0;
    etherSumWords(ip->sourceIp, 8);

    tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    etherSumWords(&tcpLen,2);

    etherSumWords(tcp, 20);
    // etherSumWords(&tcp->data, 0);
    tcp->CheckSum = getEtherChecksum();

    etherPutPacket((uint8_t*)ether, 14 + ((ip->revSize & 0xF) * 4) +  20);
}

void SendTcpPushAck(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    ip->revSize = 0x45;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    uint8_t flags,Offset;
    uint16_t x = 20;
    uint16_t a;

    uint8_t *copyData ;
    ether->destAddress[0] = 0x8c;
    ether->destAddress[1] = 0x16;
    ether->destAddress[2] = 0x45;
    ether->destAddress[3] = 0xd7;
    ether->destAddress[4] = 0x51;
    ether->destAddress[5] = 0x2f;

    ether->sourceAddress[0] = 2;
    ether->sourceAddress[1] = 3;
    ether->sourceAddress[2] = 4;
    ether->sourceAddress[3] = 5;
    ether->sourceAddress[4] = 6;
    ether->sourceAddress[5] = 141;

    ether->frameType = htons(0x0800);

    //populating IP field
    ip->typeOfService = 0;
    ip->ttl = 128;
    ip->protocol = 6; // TCP
    ip->id = 0;

    //populating TCP
    tcp->destPort = tcp->destPort;
    //tcp->sourcePort = htons(23);

    tcp->SeqNum = tcp->SeqNum;
    tcp->AckNum = tcp->AckNum;

    Offset = x >> 2;
    flags = 0x18; // for PSH and ACK
    a = (Offset << 12) + flags;
    tcp->DoRF = htons(a);
    tcp->WindowSize = htons(1280);
    tcp->CheckSum = 0;
    tcp->UrgentPtr = 0;


    ip->length = htons(((ip->revSize & 0xF) * 4) + 20 + 18);

    // 32-bit sum over ip header
    sum = 0;
    etherSumWords(&ip->revSize, 10);
    etherSumWords(ip->sourceIp, ((ip->revSize & 0xF) * 4) - 12);
    ip->headerChecksum = getEtherChecksum();

    copyData = &tcp->data;

    copyData[0] = 0x10;
    copyData[1] = 16;
    copyData[2] = 0x00;
    copyData[3] = 4;
    copyData[4] = (uint8_t)'M';
    copyData[5] = (uint8_t)'Q';
    copyData[6] = (uint8_t)'T';
    copyData[7] = (uint8_t)'T';
    copyData[8] = 4;
    copyData[9] = 2;
    copyData[10] = 0;
    copyData[11] = 0x3C;
    copyData[12] = 0;
    copyData[13] = 4;
    copyData[14] = (uint8_t)'P';
    copyData[15] = (uint8_t)'Q';
    copyData[16] = (uint8_t)'R';
    copyData[17] = (uint8_t)'T';

    uint16_t tmp16;

    uint16_t tcpLen = htons(20 + 18);
    // 32-bit sum over pseudo-header
    sum = 0;

    etherSumWords(ip->sourceIp, 8);
    tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    etherSumWords(&tcpLen, 2);
    // add udp header except crc


    etherSumWords(tcp,20+18);
    //etherSumWords(&tcp->data, tcpSize);

    tcp->CheckSum = getEtherChecksum();

    // send packet with size = ether + tcp hdr + ip header + tcp_size
    etherPutPacket((uint8_t*)ether, 14 + 20 + ((ip->revSize & 0xF) * 4) + 18 );
}

void SendMqttPublishClient(uint8_t packet[], char* Topic, char* Data)
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    ip->revSize = 0x45;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    uint8_t i,flags,Offset;
    uint16_t x = 20;
    uint16_t a;

    uint8_t *copyData ;


    //populating ether field
    for(i = 0; i < HW_ADD_LENGTH; i++)
    {
        ether->destAddress[i] = ether->destAddress[i];
    }

    ether->sourceAddress[0] = 2;
    ether->sourceAddress[1] = 3;
    ether->sourceAddress[2] = 4;
    ether->sourceAddress[3] = 5;
    ether->sourceAddress[4] = 6;
    ether->sourceAddress[5] = 141;

    ether->frameType = htons(0x0800);

    //populating IP field
    ip->typeOfService = 0;
    ip->ttl = 128;
    ip->protocol = 6; // TCP
    ip->id = 0;
    ip->sourceIp[0] = 192;
    ip->sourceIp[1] = 168;
    ip->sourceIp[2] = 1;
    ip->sourceIp[3] = 141;

    ip->destIp[0] = 192;
    ip->destIp[1] = 168;
    ip->destIp[2] = 1;
    ip->destIp[3] = 190;

    //populating TCP
    tcp->destPort = tcp->destPort;
    //tcp->sourcePort = htons(23);

    tcp->SeqNum = tcp->SeqNum;
    tcp->AckNum = tcp->AckNum;

    Offset = x >> 2;
    flags = 0x18; // for PSH and ACK
    a = (Offset << 12) + flags;
    tcp->DoRF = htons(a);
    tcp->WindowSize = htons(1280);
    tcp->CheckSum = 0;
    tcp->UrgentPtr = 0;

    uint8_t Top_Len = stringLen(Topic);
    uint8_t Data_Len = stringLen(Data);


    //MQTT begains
    copyData = &tcp->data;
    copyData[0] = 0x33; // for publish
    if(copyData[0] == 0x35 || copyData[0] == 0x33 || copyData[0] == 0x34 || copyData[0] == 0x32)
    {
        AvdSYN = false;
        copyData[1] = Top_Len + Data_Len + 2 + 2;
    }else
    {
        AvdSYN = true;
        copyData[1] = Top_Len + Data_Len + 2;
    }
    uint16_t *ptr = (uint16_t*)&copyData[2];

    *ptr = htons(Top_Len);
    for(i = 4; i < (Top_Len + 4); i++)
    {
        copyData[i] = (uint8_t)Topic[i-4];
    }
    if(copyData[0] == 0x35 || copyData[0] == 0x33 || copyData[0] == 0x34 || copyData[0] == 0x32)
    {
        uint16_t *ptr_ID = (uint16_t*)&copyData[i];
        *ptr_ID = MyRand(300,400);
        for(i = (Top_Len+4+2) ; i < (Data_Len+Top_Len+4+2) ; i++)
        {
                copyData[i] = (uint8_t)Data[i-Top_Len-4-2];
        }

    }else
    {
        for(i = (Top_Len+4) ; i < (Data_Len+Top_Len+4) ; i++)
        {
                copyData[i] = (uint8_t)Data[i-Top_Len-4];
        }
    }

    //IP checksum
    if(copyData[0] == 0x35 || copyData[0] == 0x33 || copyData[0] == 0x34 || copyData[0] == 0x32)
    {
        ip->length = htons(((ip->revSize & 0xF) * 4) + 20 + Top_Len + Data_Len + 4 + 2);
    }else
    {
        ip->length = htons(((ip->revSize & 0xF) * 4) + 20 + Top_Len + Data_Len + 4);
    }

    // 32-bit sum over ip header
    sum = 0;

    etherSumWords(&ip->revSize, 10);
    etherSumWords(ip->sourceIp, ((ip->revSize & 0xF) * 4) - 12);
    ip->headerChecksum = getEtherChecksum();

    uint16_t tmp16, tcpLen;

    if(copyData[0] == 0x35 || copyData[0] == 0x33 || copyData[0] == 0x34 || copyData[0] == 0x32)
    {
         tcpLen = htons(20 + Top_Len + Data_Len + 4 + 2);
    }else
    {
         tcpLen = htons(20 + Top_Len + Data_Len + 4);
    }

    // 32-bit sum over pseudo-header
    sum = 0;

    etherSumWords(ip->sourceIp, 8);
    tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    etherSumWords(&tcpLen, 2);

    if(copyData[0] == 0x35 || copyData[0] == 0x33 || copyData[0] == 0x34 || copyData[0] == 0x32)
    {
        etherSumWords(tcp,20 + Top_Len + Data_Len + 4 + 2);
    }else
    {
        etherSumWords(tcp,20 + Top_Len + Data_Len + 4);
    }
    //etherSumWords(&tcp->data, tcpSize);

    tcp->CheckSum = getEtherChecksum();


    if(copyData[0] == 0x35 || copyData[0] == 0x33 || copyData[0] == 0x34 || copyData[0] == 0x32)
    {
        // send packet with size = ether + tcp hdr + ip header + tcp_size
        etherPutPacket((uint8_t*)ether, 14 + 20 + ((ip->revSize & 0xF) * 4) + Top_Len + Data_Len + 4 + 2);
    }else
    {
        // send packet with size = ether + tcp hdr + ip header + tcp_size
        etherPutPacket((uint8_t*)ether, 14 + 20 + ((ip->revSize & 0xF) * 4) + Top_Len + Data_Len + 4);
    }
}


void SendTcpAck1(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    ip->revSize = 0x45;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    uint8_t i,flags,Offset;
    uint16_t x = 20; // total header length
    uint16_t a;

    // populating ether frame
    for(i = 0; i < HW_ADD_LENGTH; i++)
    {
        ether->destAddress[i] = ether->sourceAddress[i];
    }

    ether->sourceAddress[0] = 2;
    ether->sourceAddress[1] = 3;
    ether->sourceAddress[2] = 4;
    ether->sourceAddress[3] = 5;
    ether->sourceAddress[4] = 6;
    ether->sourceAddress[5] = 141;

    ether->frameType = htons(0x0800);

    //populating IP field
    ip->typeOfService = 0;
    ip->ttl = 128;
    ip->protocol = 6; // TCP
    ip->id = 0;
    ip->flagsAndOffset = htons(0x4000); //don't fragment
    uint8_t temp8;
    for(i = 0; i < IP_ADD_LENGTH; i++)
    {
        temp8 = ip->destIp[i];
        ip->destIp[i] = ip->sourceIp[i];
        ip->sourceIp[i] = temp8;
    }

    //populating TCP
    uint32_t temp32;
    uint16_t temp16;

    temp16 = tcp->destPort;
    tcp->destPort = tcp->sourcePort;
    tcp->sourcePort = temp16;
    tcp->destPort = htons(1883);



    temp32 = tcp->AckNum;
    tcp->AckNum = tcp->SeqNum;
    tcp->SeqNum = temp32;

    tcp->AckNum = tcp->AckNum + htons32(PayloadSize);

    Offset = x >> 2;
    flags = 0x10; // ACK
    a = (Offset << 12) + flags;
    tcp->DoRF = htons(a);
    tcp->WindowSize = htons(1280);
    tcp->CheckSum = 0;
    tcp->UrgentPtr = 0;

    ip->length = htons(((ip->revSize & 0xF) * 4) + 20);
    //Ip checksum
    sum=0;
    etherSumWords(&ip->revSize, 10);
    etherSumWords(ip->sourceIp, ((ip->revSize & 0xF) * 4) - 12);
    ip->headerChecksum = getEtherChecksum();

    //TCP checksum

    uint16_t tmp16;

    uint16_t tcpLen = htons(20);
    sum = 0;
    etherSumWords(ip->sourceIp, 8);

    tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    etherSumWords(&tcpLen,2);

    etherSumWords(tcp, 20);
    // etherSumWords(&tcp->data, 0);
    tcp->CheckSum = getEtherChecksum();

    etherPutPacket((uint8_t*)ether, 14 + ((ip->revSize & 0xF) * 4) +  20);
}

void SendMqttSubscribeClient(uint8_t packet[], char* Topic)
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    ip->revSize = 0x45;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    uint8_t i,flags,Offset;
    uint16_t x = 20;
    uint16_t a;

    uint8_t *copyData ;

    //populating ether field
    for(i = 0; i < HW_ADD_LENGTH; i++)
    {
        ether->destAddress[i] = ether->destAddress[i];
    }

    ether->sourceAddress[0] = 2;
    ether->sourceAddress[1] = 3;
    ether->sourceAddress[2] = 4;
    ether->sourceAddress[3] = 5;
    ether->sourceAddress[4] = 6;
    ether->sourceAddress[5] = 141;

    ether->frameType = htons(0x0800);

    //populating IP field
    ip->typeOfService = 0;
    ip->ttl = 128;
    ip->protocol = 6; // TCP
    ip->id = 0;
    ip->sourceIp[0] = 192;
    ip->sourceIp[1] = 168;
    ip->sourceIp[2] = 1;
    ip->sourceIp[3] = 141;

    ip->destIp[0] = 192;
    ip->destIp[1] = 168;
    ip->destIp[2] = 1;
    ip->destIp[3] = 190;

    //populating TCP
    tcp->destPort = tcp->destPort;
    //tcp->sourcePort = htons(23);


    tcp->SeqNum = tcp->SeqNum;
    tcp->AckNum = tcp->AckNum;

    Offset = x >> 2;
    flags = 0x18; // for PSH and ACK
    a = (Offset << 12) + flags;
    tcp->DoRF = htons(a);
    tcp->WindowSize = htons(1280);
    tcp->CheckSum = 0;
    tcp->UrgentPtr = 0;

    uint8_t Top_Len = stringLen(Topic);
    uint8_t j = 1;
    bool Idflag = false;

    //bool Idflag = false;
    copyData = &tcp->data;

    if(SubTopicFrame.Topic_names == 0)
    {
        stringCopy(SubTopicFrame.SubTopicArr[SubTopicFrame.Topic_names], Topic);
        SubTopicFrame.times[SubTopicFrame.Topic_names] = 1;
        j = 1;
        SubTopicFrame.Topic_names++;
    }else
    {
        Idflag = false;
        for(i = 0 ; i < SubTopicFrame.Topic_names; i++)
        {
            if(strngcomp(SubTopicFrame.SubTopicArr[i],Topic))
            {
                Idflag = true;
                SubTopicFrame.times[i]++;
                j = SubTopicFrame.times[i];
                break;

            }
        }

            if(Idflag == false)
            {
                stringCopy(SubTopicFrame.SubTopicArr[SubTopicFrame.Topic_names], Topic);
                SubTopicFrame.times[SubTopicFrame.Topic_names] = 1;
                j = 1;
                SubTopicFrame.Topic_names++;
            }

    }


    ip->length = htons(((ip->revSize & 0xF) * 4) + 20 + Top_Len + 2 + 2 + 2 + 1);

    // 32-bit sum over ip header
    sum = 0;


    etherSumWords(&ip->revSize, 10);
    etherSumWords(ip->sourceIp, ((ip->revSize & 0xF) * 4) - 12);
    ip->headerChecksum = getEtherChecksum();

    //MQTT begins


    copyData[0] = 0x82;
    copyData[1] = Top_Len + 2 + 2 + 1;
    copyData[2] = 0;
    copyData[3] = j;
    copyData[4] = 0;
    copyData[5] = Top_Len;
    for(i = 6; i < (Top_Len + 6); i++)
    {
        copyData[i] = (uint8_t)Topic[i-6];
    }
    copyData[i] = 0;


    uint16_t tmp16;

    uint16_t tcpLen = htons(20 + Top_Len + 2 + 2 + 2 + 1);
    // 32-bit sum over pseudo-header
    sum = 0;

    etherSumWords(ip->sourceIp, 8);
    tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    etherSumWords(&tcpLen, 2);


    etherSumWords(tcp,20 + Top_Len + 2 + 2 + 2 + 1);
    //etherSumWords(&tcp->data, tcpSize);

    tcp->CheckSum = getEtherChecksum();

    // send packet with size = ether + tcp hdr + ip header + tcp_size
    etherPutPacket((uint8_t*)ether, 14 + 20 + ((ip->revSize & 0xF) * 4) + Top_Len + 2 + 2 + 2 + 1);

}

void SendMqttPublishRel(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    ip->revSize = 0x45;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    uint8_t i,flags,Offset;
    uint16_t x = 20;
    uint16_t a;

    uint8_t *copyData ;

    //populating ether field
    for(i = 0; i < HW_ADD_LENGTH; i++)
    {
        ether->destAddress[i] = ether->destAddress[i];
    }

    ether->sourceAddress[0] = 2;
    ether->sourceAddress[1] = 3;
    ether->sourceAddress[2] = 4;
    ether->sourceAddress[3] = 5;
    ether->sourceAddress[4] = 6;
    ether->sourceAddress[5] = 141;

    ether->frameType = htons(0x0800);

    //populating IP field
    ip->typeOfService = 0;
    ip->ttl = 128;
    ip->protocol = 6; // TCP
    ip->id = 0;
    ip->sourceIp[0] = 192;
    ip->sourceIp[1] = 168;
    ip->sourceIp[2] = 1;
    ip->sourceIp[3] = 141;

    ip->destIp[0] = 192;
    ip->destIp[1] = 168;
    ip->destIp[2] = 1;
    ip->destIp[3] = 190;

    //populating TCP
    uint32_t temp32;
    uint16_t temp16;

    temp16 = tcp->destPort;
    tcp->destPort = tcp->sourcePort;
    tcp->sourcePort = temp16;


    temp32 = tcp->AckNum;
    tcp->AckNum = tcp->SeqNum;
    tcp->SeqNum = temp32;

    tcp->AckNum = tcp->AckNum + htons32(PayloadSize);

    Offset = x >> 2;
    flags = 0x18; // for PSH and ACK
    a = (Offset << 12) + flags;
    tcp->DoRF = htons(a);
    tcp->WindowSize = htons(1280);
    tcp->CheckSum = 0;
    tcp->UrgentPtr = 0;

    copyData = &tcp->data;

    ip->length = htons(((ip->revSize & 0xF) * 4) + 20 + 4);

    // 32-bit sum over ip header
    sum = 0;


    etherSumWords(&ip->revSize, 10);
    etherSumWords(ip->sourceIp, ((ip->revSize & 0xF) * 4) - 12);
    ip->headerChecksum = getEtherChecksum();

    //MQTT begins


    copyData[0] = 0x60;
    copyData[1] = 2;
    copyData[2] = copyData[2];
    copyData[3] = copyData[3];

    uint16_t tmp16;

    uint16_t tcpLen = htons(20 + 4);
    // 32-bit sum over pseudo-header
    sum = 0;

    etherSumWords(ip->sourceIp, 8);
    tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    etherSumWords(&tcpLen, 2);


    etherSumWords(tcp,20 + 4);
    //etherSumWords(&tcp->data, tcpSize);

    tcp->CheckSum = getEtherChecksum();

    // send packet with size = ether + tcp hdr + ip header + tcp_size
    etherPutPacket((uint8_t*)ether, 14 + 20 + ((ip->revSize & 0xF) * 4) + 4);

}

void SendMqttPingRequest(uint8_t packet[])
{
    SverPubFlag = false;
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    ip->revSize = 0x45;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    uint8_t i,flags,Offset;
    uint16_t x = 20;
    uint16_t a;

    // populating ether frame
    ether->destAddress[0] = 0x8c;
    ether->destAddress[1] = 0x16;
    ether->destAddress[2] = 0x45;
    ether->destAddress[3] = 0xd7;
    ether->destAddress[4] = 0x51;
    ether->destAddress[5] = 0x2f;

    ether->sourceAddress[0] = 2;
    ether->sourceAddress[1] = 3;
    ether->sourceAddress[2] = 4;
    ether->sourceAddress[3] = 5;
    ether->sourceAddress[4] = 6;
    ether->sourceAddress[5] = 141;

    ether->frameType = htons(0x0800);

    //populating IP field
    ip->typeOfService = 0;
    ip->ttl = 128;
    ip->protocol = 6; // TCP
    ip->id = 0;
    uint8_t temp8;
    for(i = 0; i < IP_ADD_LENGTH; i++)
    {
        temp8 = ip->destIp[i];
        ip->destIp[i] = ip->sourceIp[i];
        ip->sourceIp[i] = temp8;
    }

    //populating TCP
    uint32_t temp32;
    uint16_t temp16;

    temp16 = tcp->destPort;
    tcp->destPort = htons(tcp->sourcePort);
    tcp->sourcePort = temp16;


    temp32 = tcp->AckNum;
    tcp->AckNum = tcp->SeqNum;
    tcp->SeqNum = temp32;

    tcp->AckNum = tcp->AckNum + htons32(PayloadSize);

    Offset = x >> 2;
    flags = 0x10; // ACK
    a = (Offset << 12) + flags;
    tcp->DoRF = htons(a);
    tcp->WindowSize = htons(1280);
    tcp->CheckSum = 0;
    tcp->UrgentPtr = 0;

    ip->length = htons(((ip->revSize & 0xF) * 4) + 20 + 2);
    //Ip checksum
    sum=0;
    etherSumWords(&ip->revSize, 10);
    etherSumWords(ip->sourceIp, ((ip->revSize & 0xF) * 4) - 12);
    ip->headerChecksum = getEtherChecksum();

    uint8_t *copydata = &tcp->data;

    copydata[0] = 0xC0;
    copydata[1] = 0;

    //TCP checksum

    uint16_t tmp16;

    uint16_t tcpLen = htons(20 + 2);
    sum = 0;
    etherSumWords(ip->sourceIp, 8);

    tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    etherSumWords(&tcpLen,2);

    etherSumWords(tcp, 20 + 2);
    // etherSumWords(&tcp->data, 0);
    tcp->CheckSum = getEtherChecksum();

    etherPutPacket((uint8_t*)ether, 14 + ((ip->revSize & 0xF) * 4) +  20 + 2);
}

char* CollectPubData(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    ip->revSize = 0x45;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    tcp->sourcePort = htons(1883);
    tcp->destPort   = src_prt;

    uint8_t* copydata = &tcp->data;
    uint8_t i,j,QoS;
    char* topic;
    char* Data;

    uint8_t Mask = copydata[0]&0x0F;

    QoS = Mask;

    uint8_t Msg_len = copydata[1];

    uint16_t Top_len, data_len;

    uint8_t temp8 = copydata[2];

    Top_len = temp8 << 8;

    temp8 = copydata[3];

    Top_len = Top_len + temp8;

    for(i = 4; i < (Top_len + 4); i++)
    {
        topic[i-4] = (char)copydata[i];
    }


    data_len = (uint16_t)Msg_len - Top_len - 2;
    j = 0;
    while(data_len != 0)
    {
        Data[j] = (char)copydata[i];
        i++;
        j++;
        data_len--;
    }
/*
    for(i = data_len; i < 10; i++)
    {
        Data[i] = 0;
    }
*/
    return Data;
}

/*
void SendTcpmessage(uint8_t packet[], uint8_t* tcpData, uint8_t tcpSize)
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    ip->revSize = 0x45;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    uint8_t i,flags,Offset;
    uint16_t x = 20;
    uint16_t a;

    uint8_t *copyData;
    for(i = 0; i < HW_ADD_LENGTH; i++)
    {
        ether->destAddress[i] = ether->destAddress[i];
    }

    ether->sourceAddress[0] = 2;
    ether->sourceAddress[1] = 3;
    ether->sourceAddress[2] = 4;
    ether->sourceAddress[3] = 5;
    ether->sourceAddress[4] = 6;
    ether->sourceAddress[5] = 141;

    ether->frameType = htons(0x0800);

    //populating IP field
    ip->typeOfService = 0;
    ip->ttl = 55;
    ip->protocol = 6; // TCP
    ip->id = 0;

    //populating TCP
    tcp->destPort = tcp->destPort;
    //tcp->sourcePort = htons(23);

    tcp->SeqNum = tcp->SeqNum;
    tcp->AckNum = tcp->AckNum;

    Offset = x >> 2;
    flags = 0x18; // for PSH and ACK
    a = (Offset << 12) + flags;
    tcp->DoRF = htons(a);
    tcp->WindowSize = htons(1280);
    tcp->CheckSum = 0;
    tcp->UrgentPtr = 0;

    ip->length = htons(((ip->revSize & 0xF) * 4) + 20 + tcpSize);

    // 32-bit sum over ip header
    sum = 0;
    etherSumWords(&ip->revSize, 10);
    etherSumWords(ip->sourceIp, ((ip->revSize & 0xF) * 4) - 12);
    ip->headerChecksum = getEtherChecksum();

    copyData = &tcp->data;
    for (i = 0; i < tcpSize; i++)
    {
        copyData[i] = tcpData[i];
    }

    uint16_t tmp16;

    uint16_t tcpLen = htons(20 + tcpSize);
    // 32-bit sum over pseudo-header
    sum = 0;

    etherSumWords(ip->sourceIp, 8);
    tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    etherSumWords(&tcpLen, 2);
    // add udp header except crc


    //uint16_t destPort = htons(tcp->destPort);
    //etherSumWords(&tcp->sourcePort,2);
    //sum += (destPort & 0xFF) << 8;
    //    etherSumWords(tcp,4);
    //    sum += htons32(tcp->SeqNum);
    //    sum += htons32(tcp->AckNum);
    //    etherSumWords(&tcp->DoRF, 8);

    etherSumWords(tcp,20+tcpSize);
    //etherSumWords(&tcp->data, tcpSize);

    tcp->CheckSum = getEtherChecksum();

    // send packet with size = ether + tcp hdr + ip header + tcp_size
    etherPutPacket((uint8_t*)ether, 14 + 20 + ((ip->revSize & 0xF) * 4) + tcpSize );
}
*/

void SendTcpFin(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    ip->revSize = 0x45;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    uint8_t i,flags,Offset;
    uint16_t x = 20;
    uint16_t a;

    for(i = 0; i < HW_ADD_LENGTH; i++)
    {
        ether->destAddress[i] = ether->destAddress[i];
    }

    ether->sourceAddress[0] = 2;
    ether->sourceAddress[1] = 3;
    ether->sourceAddress[2] = 4;
    ether->sourceAddress[3] = 5;
    ether->sourceAddress[4] = 6;
    ether->sourceAddress[5] = 14;

    ether->frameType = htons(0x0800);

    //populating IP field
    ip->typeOfService = 0;
    ip->ttl = 128;
    ip->protocol = 6; // TCP
    ip->id = 0;

    uint8_t temp8;
    for(i = 0; i < IP_ADD_LENGTH; i++)
    {
        temp8 = ip->destIp[i];
        ip->destIp[i] = ip->sourceIp[i];
        ip->sourceIp[i] = temp8;
    }
    /*
        for(i = 0; i < IP_ADD_LENGTH; i++)
        {
            ip->sourceIp[i] = DhcpIpaddress[i];
        }
     */
    //populating TCP
    tcp->destPort = htons(tcp->sourcePort);
    tcp->sourcePort = htons(23);

    //tcp->SeqNum = tcp->AckNum;
    //tcp->AckNum = tcp->SeqNum + htons32(1);
    uint32_t temp32;

    temp32 = tcp->AckNum;

    tcp->AckNum = tcp->SeqNum;

    tcp->SeqNum = temp32;
    //tcp->AckNum = 0;


    Offset = x >> 2;
    flags = 0x11; // for FIN and ACK
    a = (Offset << 12) + flags;
    tcp->DoRF = htons(a);
    tcp->WindowSize = htons(1280);
    tcp->CheckSum = 0;
    tcp->UrgentPtr = 0;

    ip->length = htons(((ip->revSize & 0xF) * 4) + 20);

    //IP checksum
    sum=0;
    etherSumWords(&ip->revSize, 10);
    etherSumWords(ip->sourceIp, ((ip->revSize & 0xF) * 4) - 12);
    ip->headerChecksum = getEtherChecksum();

    //TCP checksum

    uint16_t tmp16;

    uint16_t tcpLen = htons(20);
    sum = 0;
    etherSumWords(ip->sourceIp, 8);

    tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    etherSumWords(&tcpLen,2);

    etherSumWords(tcp, 20);
    // etherSumWords(&tcp->data, 0);
    tcp->CheckSum = getEtherChecksum();

    etherPutPacket((uint8_t*)ether, 14 + ((ip->revSize & 0xF) * 4) +  20);


}

void SendTcpLastAck(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    ip->revSize = 0x45;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    uint8_t i,flags,Offset;
    uint16_t x = 20; // total header length
    uint16_t a;

    // populating ether frame
    for(i = 0; i < HW_ADD_LENGTH; i++)
    {
        ether->destAddress[i] = ether->sourceAddress[i];
    }

    ether->sourceAddress[0] = 2;
    ether->sourceAddress[1] = 3;
    ether->sourceAddress[2] = 4;
    ether->sourceAddress[3] = 5;
    ether->sourceAddress[4] = 6;
    ether->sourceAddress[5] = 141;

    ether->frameType = htons(0x0800);

    //populating IP field
    ip->typeOfService = 0;
    ip->ttl = 55;
    ip->protocol = 6; // TCP
    ip->id = 0;
    uint8_t temp8;
    for(i = 0; i < IP_ADD_LENGTH; i++)
    {
        temp8 = ip->destIp[i];
        ip->destIp[i] = ip->sourceIp[i];
        ip->sourceIp[i] = temp8;
    }
    /*
        for(i = 0; i < IP_ADD_LENGTH; i++)
        {
            ip->sourceIp[i] = DhcpIpaddress[i];
        }
     */
    //populating TCP
    uint32_t temp32;
    tcp->destPort = ntohs(tcp->sourcePort);
    tcp->sourcePort = htons(23);


    temp32 = tcp->SeqNum;
    tcp->SeqNum = tcp->AckNum;
    tcp->AckNum = temp32;

    tcp->AckNum = tcp->AckNum + htons32(1);

    Offset = x >> 2;
    flags = 0x10; // ACK
    a = (Offset << 12) + flags;
    tcp->DoRF = htons(a);
    tcp->WindowSize = htons(1280);
    tcp->CheckSum = 0;
    tcp->UrgentPtr = 0;



    ip->length = htons(((ip->revSize & 0xF) * 4) + 20);
    //Ip checksum
    sum=0;
    etherSumWords(&ip->revSize, 10);
    etherSumWords(ip->sourceIp, ((ip->revSize & 0xF) * 4) - 12);
    ip->headerChecksum = getEtherChecksum();

    //TCP checksum

    uint16_t tmp16;

    uint16_t tcpLen = htons(20);
    sum = 0;
    etherSumWords(ip->sourceIp, 8);

    tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    etherSumWords(&tcpLen,2);

    etherSumWords(tcp, 20);
    // etherSumWords(&tcp->data, 0);
    tcp->CheckSum = getEtherChecksum();

    etherPutPacket((uint8_t*)ether, 14 + ((ip->revSize & 0xF) * 4) +  20);
}


uint16_t etherGetId()
{
    return htons(sequenceId);
}

void etherIncId()
{
    sequenceId++;
}

void etherEnableDhcpMode()
{
    EtherDhcp = true;
}

void etherDisableDhcpMode()
{
    EtherDhcp = false;
}

bool etherIsDhcpEnabled()
{
    return EtherDhcp;
}

// Enable or disable DHCP mode
void etherEnablemqtt()
{
    mqttEnabled = true;
}

void etherDisablemqtt()
{
    mqttEnabled = false;
}

bool IsMqttEnabled()
{
    return mqttEnabled;
}
// Determines if the IP address is valid
bool etherIsIpValid()
{
    return ipAddress[0] || ipAddress[1] || ipAddress[2] || ipAddress[3];
}

// Sets IP address
void etherSetIpAddress(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3)
{
    ipAddress[0] = ip0;
    ipAddress[1] = ip1;
    ipAddress[2] = ip2;
    ipAddress[3] = ip3;
}
//Sets MQTT broker IP Address
void etherSetMqttBrkIp(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3)
{
    MqttBrkipAddress[0] = ip0;
    MqttBrkipAddress[1] = ip1;
    MqttBrkipAddress[2] = ip2;
    MqttBrkipAddress[3] = ip3;
}
// Gets IP address
void etherGetIpAddress(uint8_t ip[4])
{
    uint8_t i;
    for (i = 0; i < 4; i++)
        ip[i] = ipAddress[i];
}
//Gets MQTT broker IP Address
void etherGetMqttBrkIpAddress(uint8_t ip[4])
{
    uint8_t i;
    for (i = 0; i < 4; i++)
        ip[i] = MqttBrkipAddress[i];
}
// Sets IP subnet mask
void etherSetIpSubnetMask(uint8_t mask0, uint8_t mask1, uint8_t mask2, uint8_t mask3)
{
    ipSubnetMask[0] = mask0;
    ipSubnetMask[1] = mask1;
    ipSubnetMask[2] = mask2;
    ipSubnetMask[3] = mask3;
}

// Gets IP subnet mask
void etherGetIpSubnetMask(uint8_t mask[4])
{
    uint8_t i;
    for (i = 0; i < 4; i++)
        mask[i] = ipSubnetMask[i];
}

// Sets IP gateway address
void etherSetIpGatewayAddress(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3)
{
    ipGwAddress[0] = ip0;
    ipGwAddress[1] = ip1;
    ipGwAddress[2] = ip2;
    ipGwAddress[3] = ip3;
}

// Gets IP gateway address
void etherGetIpGatewayAddress(uint8_t ip[4])
{
    uint8_t i;
    for (i = 0; i < 4; i++)
        ip[i] = ipGwAddress[i];
}

// Sets DNS address
void etherSetDNSAddress(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3)
{
    DNSAddress[0] = ip0;
    DNSAddress[1] = ip1;
    DNSAddress[2] = ip2;
    DNSAddress[3] = ip3;
}

// Gets DNS address
void etherGetDNSAddress(uint8_t ip[4])
{
    uint8_t i;
    for (i = 0; i < 4; i++)
        ip[i] = DNSAddress[i];
}
// Sets MAC address
void etherSetMacAddress(uint8_t mac0, uint8_t mac1, uint8_t mac2, uint8_t mac3, uint8_t mac4, uint8_t mac5)
{
    macAddress[0] = mac0;
    macAddress[1] = mac1;
    macAddress[2] = mac2;
    macAddress[3] = mac3;
    macAddress[4] = mac4;
    macAddress[5] = mac5;
}

// Gets MAC address
void etherGetMacAddress(uint8_t mac[6])
{
    uint8_t i;
    for (i = 0; i < 6; i++)
        mac[i] = macAddress[i];
}

