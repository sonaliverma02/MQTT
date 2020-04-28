/*
 * EEPROM.h
 *
 *  Created on: 27-Apr-2020
 *      Author: AMIT VARMA
 */

#ifndef EEPROM_H_
#define EEPROM_H_

#include "tm4c123gh6pm.h"
#include <stdint.h>

void initEeprom();
void writeEeprom(uint16_t add, uint32_t eedata);
uint32_t readEeprom(uint16_t add);


#endif /* EEPROM_H_ */
