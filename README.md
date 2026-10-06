# linux-drivers-uni
Collection of Linux kernel drivers written during uni studies, written for the Raspberry Pi 2b and occasional specific hardware.

List of projects:

- uart_timer: Kernel driver that writes a character periodically directly to the PL011 UART driver 
- toupper_tolower: Character device that converts written inputs to lowercase/uppercase characters, depending on the ioctl configuration
- nunchuck: Driver reading input data from a nunchuck game controller, including button inputs, joystick inputs, and motion sensors
- nunchuck_led: Driver controlling LED found on the Pi 2 with the nunchuck controller 
