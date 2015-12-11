all:
	make -C module
	make -C firmware
	make -C udev

clean:
	make -C udev clean
	make -C firmware clean
	make -C module clean

install:
	make -C firmware install
	make -C udev install
	make -C module install

uninstall:
	make -C firmware uninstall
	make -C udev uninstall
	make -C module uninstall

















