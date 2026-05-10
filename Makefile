CC = $(CROSS_COMPILE)gcc
SYSROOT ?= $(CURDIR)/sysroot

CFLAGS  = -Wall -Wextra -O2 \
           --sysroot=$(SYSROOT) \
           -I./include \
           -I$(SYSROOT)/usr/include \
           -I$(SYSROOT)/usr/include/arm-linux-gnueabihf

LDFLAGS = --sysroot=$(SYSROOT) \
           -L$(SYSROOT)/usr/lib/arm-linux-gnueabihf \
           -ljson-c -lSDL -lSDL_ttf -lSDL_image -lcurl -lpthread \
           -Wl,-rpath-link,$(SYSROOT)/usr/lib/arm-linux-gnueabihf \
           -Wl,--allow-shlib-undefined

TARGET = romm
SRCS   = $(wildcard src/*.c)
OBJS   = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

# ── Device install (direct SD card mount) ───────────────────────────
INSTALL_DIR = /mnt/SDCARD/App/RomM

install: $(TARGET)
	install -m 755 $(TARGET) $(INSTALL_DIR)/$(TARGET)

# ── Package (produces romm-miyoo.zip for SD card deployment) ────────
DIST_APP = App/RomM

.PHONY: package
package: $(TARGET)
	mkdir -p $(DIST_APP)/covers
	cp $(TARGET) $(DIST_APP)/
	cd App && zip -r ../romm-miyoo.zip RomM/
	@echo ""
	@echo "Created romm-miyoo.zip"
	@echo "Extract to the root of your Miyoo Mini Plus SD card:"
	@echo "  unzip -o romm-miyoo.zip -d /path/to/sdcard"
