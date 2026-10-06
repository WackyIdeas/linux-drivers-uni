#ifndef UTILS_H
#define UTILS_H

#include "common.h"

/*
 * SetInternalPullUpDown function
 *  Parameters:
 *   regs      - virtual address where the physical GPIO address is mapped
 *   pin       - number of GPIO pin;
 *   pull      - set internal pull up/down/none if PULL_UP/PULL_DOWN/PULL_NONE selected
 *  Operation:
 *   Sets to use internal pull-up or pull-down resistor, or not to use it if pull-none
 *   selected for desired GPIO pin.
 */
void SetInternalPullUpDown(void __iomem *regs, u8 pin, PUD pull);

/*
 * SetGpioPinDirection function
 *  Parameters:
 *   regs      - virtual address where the physical GPIO address is mapped
 *   pin       - number of GPIO pin;
 *   direction - GPIO_DIRECTION_IN or GPIO_DIRECTION_OUT
 *  Operation:
 *   Sets the desired GPIO pin to be used as input or output based on the direcation value.
 */
void SetGpioPinDirection(void __iomem *regs, u8 pin, DIRECTION direction);

/*
 * SetGpioPin function
 *  Parameters:
 *   regs      - virtual address where the physical GPIO address is mapped
 *   pin       - number of GPIO pin;
 *  Operation:
 *   Sets the desired GPIO pin to HIGH level. The pin should previously be defined as output.
 */
void SetGpioPin(void __iomem *regs, u8 pin);

/*
 * ClearGpioPin function
 *  Parameters:
 *   regs      - virtual address where the physical GPIO address is mapped
 *   pin       - number of GPIO pin;
 *  Operation:
 *   Sets the desired GPIO pin to LOW level. The pin should previously be defined as output.
 */
void ClearGpioPin(void __iomem *regs, u8 pin);

/*
 * GetGpioPinValue function
 *  Parameters:
 *   regs      - virtual address where the physical GPIO address is mapped
 *   pin       - number of GPIO pin;
 *
 *   return    - the level read from desired GPIO pin
 *  Operation:
 *   Reads the level from the desired GPIO pin and returns the read value.
 */
u8 GetGpioPinValue(void __iomem *regs, u8 pin);

#endif

