FIRMWARE=../firmware/fx2aex.ihx
FIRMWARE_LINK=fx2aex.ihx
all: $(FIRMWARE)
	ln -s $(FIRMWARE) $(FIRMWARE_LINK)

clean:
	-@rm $(FIRMWARE_LINK)
install: load 33-fxload.rules
	mkdir -p /etc/fx2
	cp load /etc/fx2/
	cp 33-fxload.rules /etc/udev/rules.d/
unistall:
	rm /etc/fx2/load
	rm /etc/udev/rules.d/33-fxload.rules
