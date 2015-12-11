COMP=sdcc
COMP_OPTIONS=-mmcs51
SRC=fx2aex.c
TARGET=fx2aex.ihx
OBJS=fx2aex.asm fx2aex.cdb fx2aex.lk fx2aex.lst fx2aex.map fx2aex.mem fx2aex.omf fx2aex.rel fx2aex.rst fx2aex.sym

all: fx2aex.c  fx2regs.h
	$(COMP) $(COMP_OPTIONS) $(SRC)
	@md5sum $(TARGET) > $(TARGET).md5

clean:
	-@rm $(TARGET)
	-@rm $(TARGET).md5
	-@rm $(OBJS)

install: $(TARGET)
	mkdir -p /etc/fx2
	cp $(TARGET) /etc/fx2/

uninstall:
	-rm -f /etc/fx2/$(TARGET)
