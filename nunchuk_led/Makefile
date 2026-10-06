ifneq ($(KERNELRELEASE),)
obj-m := nunchuck.o
nunchuck-objs := nunchuck_led.o utils.o
else
KDIR := ../../../../src/linux
all:
	$(MAKE) -C $(KDIR) M=$$PWD
clean:
	$(MAKE) -C $(KDIR) M=$$PWD clean
cleanall:
	$(MAKE) clean
	@rm -f *~
endif
