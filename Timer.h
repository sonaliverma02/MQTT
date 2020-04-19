/*
 * Timer.h
 *
 *  Created on: 14-Feb-2020
 *      Author: chaya kumar gowda
 */

#ifndef TIMER_H_
#define TIMER_H_

#include<stdint.h>
#include<stdbool.h>

typedef void(*_callback)();

//uint8_t count = 0;
//............................................................................................................................................
//Subroutines
//............................................................................................................................................

void initimer();
bool startOneshotTimer(_callback callback,uint32_t seconds);
bool startPeriodicTimer(_callback callback,uint32_t seconds);
bool stopTimer(_callback callback);
bool restartTimer(_callback callback);
void tickIsr();

#endif /* TIMER_H_ */
