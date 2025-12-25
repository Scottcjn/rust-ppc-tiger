# Makefile for Tiger Kernel Security Extensions
# Requires Xcode 2.5 on PowerPC

CC = /usr/bin/gcc-4.0
SDK = /Developer/SDKs/MacOSX10.4u.sdk
ARCH = -arch ppc

KEXT_CFLAGS = -fno-builtin -fno-common -msoft-float \
              -nostdinc -fno-stack-protector \
              -I$(SDK)/System/Library/Frameworks/Kernel.framework/Headers \
              -isysroot $(SDK) $(ARCH)

KEXT_LDFLAGS = -nostdlib -lkmod -lcc_kext -Xlinker -kext $(ARCH)

all: IOKitSecurity.kext HFSSecurity.kext TCPSecurity.kext

IOKitSecurity.kext:
	mkdir -p IOKitSecurity.kext/Contents/MacOS
	$(CC) $(KEXT_CFLAGS) $(KEXT_LDFLAGS) \
	      -o IOKitSecurity.kext/Contents/MacOS/IOKitSecurity \
	      ../kernel_patches/CVE-2014-4377-IOKit/iokit_security.c
	cp Info-IOKit.plist IOKitSecurity.kext/Contents/Info.plist

HFSSecurity.kext:
	mkdir -p HFSSecurity.kext/Contents/MacOS
	$(CC) $(KEXT_CFLAGS) $(KEXT_LDFLAGS) \
	      -o HFSSecurity.kext/Contents/MacOS/HFSSecurity \
	      ../kernel_patches/CVE-2010-0036-HFS/hfs_security.c
	cp Info-HFS.plist HFSSecurity.kext/Contents/Info.plist

TCPSecurity.kext:
	mkdir -p TCPSecurity.kext/Contents/MacOS
	$(CC) $(KEXT_CFLAGS) $(KEXT_LDFLAGS) \
	      -o TCPSecurity.kext/Contents/MacOS/TCPSecurity \
	      ../kernel_patches/CVE-2009-2414-TCP/tcp_security.c
	cp Info-TCP.plist TCPSecurity.kext/Contents/Info.plist

install:
	sudo cp -R *.kext /System/Library/Extensions/
	sudo chown -R root:wheel /System/Library/Extensions/*.kext
	sudo chmod -R 755 /System/Library/Extensions/*.kext
	sudo touch /System/Library/Extensions

load:
	sudo kextload IOKitSecurity.kext
	sudo kextload HFSSecurity.kext
	sudo kextload TCPSecurity.kext

clean:
	rm -rf *.kext