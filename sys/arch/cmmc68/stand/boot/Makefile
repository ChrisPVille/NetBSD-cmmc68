CROSS=	m68k-elf
AS=	${CROSS}-as
LD=	${CROSS}-ld
OBJCOPY=${CROSS}-objcopy

boot.bin: boot.o
	${LD} -m m68kelf -T boot.ld -o boot.elf boot.o
	${OBJCOPY} -O binary boot.elf boot.bin

boot.o: boot.s
	${AS} -m68010 -g -o boot.o boot.s

clean:
	rm -f boot.o boot.elf boot.bin
