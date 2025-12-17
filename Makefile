SOURCE=arcade_buttons.c battery_gauge.c main.c
OUTPUT=GGA
CC=gcc
INCLUDE=-I/usr/include/libevdev-1.0 -levdev -lm
USE_INT=-lgpiod -DGPIO_INT=1

ifneq (,$(wildcard /etc/rpi-issue))
	OS_RPI := true
else
	OS_RPI := false
endif 

all: install

GGA:
ifeq ($(OS_RPI),true)
	$(CC) $(SOURCE) $(INCLUDE) $(USE_INT) -o $(OUTPUT)
else
	$(CC) $(SOURCE) $(INCLUDE) -o $(OUTPUT)
endif

install: GGA
	cp $(OUTPUT) /usr/local/bin/
	cp $(OUTPUT).service /etc/systemd/system/
	echo "Installed $(OUTPUT).service"

clean:
	rm $(OUTPUT)
