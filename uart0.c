// UART0 Library
// Jason Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: EK-TM4C123GXL
// Target uC:       TM4C123GH6PM
// System Clock:    -

// Hardware configuration:
// UART Interface:
//   U0TX (PA1) and U0RX (PA0) are connected to the 2nd controller
//   The USB on the 2nd controller enumerates to an ICDI interface and a virtual COM port

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <ctype.h>
#include "tm4c123gh6pm.h"
#include "uart0.h"

// PortA masks
#define UART_TX_MASK 2
#define UART_RX_MASK 1

//-----------------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

// Initialize UART0
void initUart0()
{
    // Configure HW to work with 16 MHz XTAL, PLL enabled, system clock of 40 MHz
    SYSCTL_RCC_R = SYSCTL_RCC_XTAL_16MHZ | SYSCTL_RCC_OSCSRC_MAIN | SYSCTL_RCC_USESYSDIV | (4 << SYSCTL_RCC_SYSDIV_S);

    // Set GPIO ports to use APB (not needed since default configuration -- for clarity)
    SYSCTL_GPIOHBCTL_R = 0;

    // Enable clocks
    SYSCTL_RCGCUART_R |= SYSCTL_RCGCUART_R0;
    SYSCTL_RCGCGPIO_R |= SYSCTL_RCGCGPIO_R0;
    _delay_cycles(3);

    // Configure UART0 pins
    GPIO_PORTA_DIR_R |= UART_TX_MASK;                   // enable output on UART0 TX pin
    GPIO_PORTA_DIR_R &= ~UART_RX_MASK;                   // enable input on UART0 RX pin
    GPIO_PORTA_DR2R_R |= UART_TX_MASK;                  // set drive strength to 2mA (not needed since default configuration -- for clarity)
    GPIO_PORTA_DEN_R |= UART_TX_MASK | UART_RX_MASK;    // enable digital on UART0 pins
    GPIO_PORTA_AFSEL_R |= UART_TX_MASK | UART_RX_MASK;  // use peripheral to drive PA0, PA1
    GPIO_PORTA_PCTL_R &= ~(GPIO_PCTL_PA1_M | GPIO_PCTL_PA0_M); // clear bits 0-7
    GPIO_PORTA_PCTL_R |= GPIO_PCTL_PA1_U0TX | GPIO_PCTL_PA0_U0RX;
                                                        // select UART0 to drive pins PA0 and PA1: default, added for clarity

    // Configure UART0 to 115200 baud, 8N1 format
    UART0_CTL_R = 0;                                    // turn-off UART0 to allow safe programming
    UART0_CC_R = UART_CC_CS_SYSCLK;                     // use system clock (40 MHz)
    UART0_IBRD_R = 21;                                  // r = 40 MHz / (Nx115.2kHz), set floor(r)=21, where N=16
    UART0_FBRD_R = 45;                                  // round(fract(r)*64)=45
    UART0_LCRH_R = UART_LCRH_WLEN_8 | UART_LCRH_FEN;    // configure for 8N1 w/ 16-level FIFO
    UART0_CTL_R = UART_CTL_TXE | UART_CTL_RXE | UART_CTL_UARTEN;
                                                        // enable TX, RX, and module
}

// Set baud rate as function of instruction cycle frequency
void setUart0BaudRate(uint32_t baudRate, uint32_t fcyc)
{
    uint32_t divisorTimes128 = (fcyc * 8) / baudRate;   // calculate divisor (r) in units of 1/128,
                                                        // where r = fcyc / 16 * baudRate
    UART0_IBRD_R = divisorTimes128 >> 7;                 // set integer value to floor(r)
    UART0_FBRD_R = ((divisorTimes128 + 1)) >> 1 & 63;    // set fractional value to round(fract(r)*64)
}

// Blocking function that writes a serial character when the UART buffer is not full
void putcUart0(char c)
{
    while (UART0_FR_R & UART_FR_TXFF);               // wait if uart0 tx fifo full
    UART0_DR_R = c;                                  // write character to fifo
}

// Blocking function that writes a string when the UART buffer is not full
void putsUart0(char* str)
{
    uint8_t i = 0;
    while (str[i] != '\0')
        putcUart0(str[i++]);
}

// Blocking function that returns with serial data once the buffer is not empty
char getcUart0()
{
    while (UART0_FR_R & UART_FR_RXFE);               // wait if uart0 rx fifo empty
    return UART0_DR_R & 0xFF;                        // get character from fifo
}

// Returns the status of the receive buffer
bool kbhitUart0()
{
    return !(UART0_FR_R & UART_FR_RXFE);
}


char* itostring(int n)
{
    int j, sign;
    static char s[20];
    char temp_char;

    if ((sign = n) < 0)  /* record sign */
        n = -n;          /* make n positive */
    j = 0;
    do {       /* generate digits in reverse order */
        s[j++] = n % 10 + '0';   /* get next digit */
    } while ((n /= 10) > 0);     /* delete it */
    if (sign < 0)
        s[j++] = '-';
    s[j] = '\0';
    int i=0;
    j--;

    while(i<j)
    {
       temp_char = s[j];
       s[j] = s[i];
       s[i] = temp_char;

       i++;
       j--;
    }
    return s;
}

bool stringcmp (const char str1[],char str2[])
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

void getsUart0(USER_DATA* data)
{
    uint8_t count = 0;
    char c;
    while(count<MAX_CHARS)
    {
       c = getcUart0();
       if(c == 0x08 || c == 0x7F)
       {
           if (count>0)
           {
               count--;
               continue;
           }
           else
           {
               continue;
           }
       }
       else if(c == 0x0D || c == 0x0A)
       {
           data->buffer[count] = '\0';
           break;
       }
       else if(c >= 0x20)
       {
           data->buffer[count++] = tolower(c);
           if(count == MAX_CHARS)
           {
               data->buffer[count] = '\0';
               break;
           }
           else
           {
               continue;
           }
       }
    }

}

void parseFields(USER_DATA* data)
{
    uint8_t index = 0;
    uint8_t d,b,m = 0;

    while(data->buffer[index] != '\0')
    {
        d = ((data->buffer[index-1]>=32 && data->buffer[index-1]<=47) || (data->buffer[index-1]>=58 && data->buffer[index-1]<=64) || (data->buffer[index-1]>=91 && data->buffer[index-1]<=96) || (data->buffer[index-1]>=123 && data->buffer[index-1]<=127));
        b = ((data->buffer[index]>=97 && data->buffer[index]<=122) || (data->buffer[index]>=48 && data->buffer[index]<=57));

        if ((data->buffer[index]>=32 && data->buffer[index]<=47) || (data->buffer[index]>=58 && data->buffer[index]<=64) || (data->buffer[index]>=91 && data->buffer[index]<=96) || (data->buffer[index]>=123 && data->buffer[index]<=127))
        {
             index++;
             continue;
        }
        else
            {
             if(b)
             {
                 if(index == 0 && b)
                 {
                     data->fieldPosition[m] = index;
                     index++;
                     if(data->buffer[0]>=97 && data->buffer[0]<=122)
                     {
                             data->fieldType[m] = 'a';
                     }
                     else if(data->buffer[0]>=48 && data->buffer[0]<=57)
                     {
                              data->fieldType[m] = 'n';
                     }
                     m++;
                 }

                 data->fieldPosition[m] = index;
                 if(data->buffer[index]>=97 && data->buffer[index]<=122)
                 {
                     data->fieldType[m] = 'a';
                 }
                 else if(data->buffer[index]>=48 && data->buffer[index]<=57)
                 {
                     data->fieldType[m] = 'n';
                 }
                 index++;
                 if( d && b )
                 {
                     data->buffer[index-2] = '\0';
                     m++;
                 }


                 if(data->buffer[index] == '\0')
                 {
                     data->fieldPosition[m] = '\0';
                     data->fieldType[m] = '\0';
                     data->fieldcount = m;
                     itostring(data->fieldcount);
                     putsUart0("\r\n");
                     break;
                 }
              }
            }
    }
}

char* getFieldString(USER_DATA* data, uint8_t fieldNumber)
{
    return &data->buffer[data->fieldPosition[fieldNumber-1]];
}

int32_t getFieldInteger(USER_DATA* data, uint8_t fieldNumber)
{
    return atoi(getFieldString(data,fieldNumber));
}

bool isCommand(USER_DATA* data, const char strCommand[], uint8_t minArguments)
{
    if(stringcmp(strCommand, &data->buffer[data->fieldPosition[0]]) && data->fieldcount >= minArguments)
    {
        return true;
    }
       return false;
}

