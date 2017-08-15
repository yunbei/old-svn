#define FN_IR			1
#define	FN_SBUS			2
#define FN_SER			4
#define FN_USB			8
#define FN_POWERON		16
#define FN_EXT_SW		32
#define FN_CALIBRATE	64
#define FN_SOFTID		128
#define FN_EEPROM		256
#define FN_TRANSL		512
#define FN_HWCARR		1024
#define FN_DUALRCV		2048
#define FN_SBUS_UART	4096
#define FN_FLASH128		8192
#define FN_DUALSND		16384
#define FN_DISP1		32768
#define FN_DISP2		0x10000
#define FN_DISP3		0x20000
#define FN_CLOCK		0x40000
#define FN_DEBOUNCE		0x80000
#define FN_BOOTLOADER	0x100000
#define FN_DUALPOWERON	0x200000
#define FN_USBWAKEUP	0x400000
#define FN_NOSCROLL		0x800000
#define FN_LAN			0x1000000
#define FN_IRDB			0x2000000
#define FN_LARGECPU		0x4000000
#define FN_MULTIRELAY4	0x8000000
#define FN_MULTISEND4	0x10000000
#define FN_MULTISEND8	0x20000000
#define FN_MULTISEND2	0x40000000
#define FN_HTML			0x80000000

#define FN2_FUNCTIONVAL2	1
#define FN2_EXT_RCV			2
#define FN2_ONEWIRE			4
#define FN2_PULSE200		8
#define FN2_V38				16
#define FN2_RECS80			32
#define FN2_PULSELEN_18		64
#define	FN2_SW_RCV_SEL		128
#define FN2_RTS_CTS			256
#define FN2_MULTIRELAY2		512
#define FN2_DUTYCYCLE		1024
#define FN2_SBUS_SEND		2048

#define FN3_RCV_LOW_56			1
#define FN3_RCV_LOW_38			2
#define FN3_RCV_LOW_38_PLAS		3
#define FN3_RCV_LOW_455			4
#define FN3_RCV_LOW_455_PLAS	5
#define FN3_RCV_LOW_CODELEARN	6
#define FN3_RCV_LOW_ACTIVE		8

#define FN3_RCV_HI_56			16
#define FN3_RCV_HI_38			32
#define FN3_RCV_HI_38_PLAS		48
#define FN3_RCV_HI_455			64
#define FN3_RCV_HI_455_PLAS		80
#define FN3_RCV_HI_CODELEARN	96
#define FN3_RCV_HI_ACTIVE		128


#define FN_DISPMASK	0x38000
#define FUNCTION_FLASH_MASK  ~(FN_SBUS_UART | FN_CLOCK | FN_DEBOUNCE | FN_DUALPOWERON | FN_USBWAKEUP | FN_NOSCROLL | FN_TRANSL | FN_CALIBRATE | FN_POWERON)
