#ifndef _FSL_DCM_H
#define _FSL_DCM_H

/* sysfs commands for 'control' */
#define SYSFS_DCM_CMD_STOP	0
#define SYSFS_DCM_CMD_START	1

#define MAX_NAME_LEN		10
#define MAX_FREQUENCY		48		/* max freq the DCM supports */
#define DATA_ADDR			0x20	/* data address in DCM SRAM */
#define CHAN_MASK			0x1FF	/* 9 channel is enabled */

/* PIXIS register status bits */
#define PX_OCMD_MSG	(1 << 0)
#define PX_OACK_ERR	(1 << 1)
#define PX_OACK_ACK	(1 << 0)	/* OACK is sometimes called MACK */

/* DCM commands */
#define OM_END			0x00
#define OM_SETDLY		0x01
#define OM_RST0			0x02
#define OM_RST1			0x03
#define OM_CHKDLY		0x04
#define OM_PWR			0x05
#define OM_WAKE			0x07
#define OM_GETMEM		0x08
#define OM_SETMEM		0x09
#define OM_SCLR			0x10
#define OM_START		0x11
#define OM_STOP			0x12
#define OM_GET			0x13
#define OM_ENABLE		0x14
#define OM_TIMER		0x15
#define OM_SNAP			0x16
#define OM_GETMEMX		0x18
#define OM_AVGOFF		0x19
#define OM_AVGON		0x1A
#define OM_SETV			0x30
#define OM_INFO			0x31
#define OM_XINFO		0x32

/* Support definitions */
#define CRCTLBIT_ENA        (7)	/* Set to enable collection */
#define CRCTLBIT_IGNORE     (6)	/* Ignore record for voltage mangement */
#define CRCTLBIT_FILTER     (5)	/* Set to enable data filtering */
#define CRCTLBIT_GET_V2     (4)	/* Record type: voltage via INA220 */
#define CRCTLBIT_GET_I2     (3)	/* Record type: current via INA220 */
#define CRCTLBIT_GET_T      (2)	/* Record type: temperature via ADT7461 */
#define CRCTLBIT_GET_I      (1)	/* Record type: current via PMBus device */
#define CRCTLBIT_GET_V      (0)	/* Record type: voltage via PMBus device */

/* Bitmasks */
#define CRCTL_ENA           (1 << CRCTLBIT_ENA)
#define CRCTL_IGNORE        (1 << CRCTLBIT_IGNORE)
#define CRCTL_FILTER        (1 << CRCTLBIT_FILTER)
#define CRCTL_GET_V2        (1 << CRCTLBIT_GET_V2)
#define CRCTL_GET_I2        (1 << CRCTLBIT_GET_I2)
#define CRCTL_GET_T         (1 << CRCTLBIT_GET_T)
#define CRCTL_GET_I         (1 << CRCTLBIT_GET_I)
#define CRCTL_GET_V         (1 << CRCTLBIT_GET_V)

/* Handy definitions */
#define CR_VOLT_PMB         (CRCTL_ENA|CRCTL_FILTER|CRCTL_GET_V)
#define CR_VOLT_PMB_X       (CRCTL_ENA|CRCTL_FILTER|CRCTL_GET_V|CRCTL_IGNORE)
#define CR_CURR_PMB         (CRCTL_ENA|CRCTL_FILTER|CRCTL_GET_I)
#define CR_VOLT_INA         (CRCTL_ENA|CRCTL_GET_V2)
#define CR_CURR_INA         (CRCTL_ENA|CRCTL_GET_I2)
#define CR_TEMP             (CRCTL_ENA|CRCTL_FILTER|CRCTL_GET_T)

struct crecord {
	u8		ctl;			/* enabled, data type */
	u8		i2c_addr;		/* target i2c address */
	u8		port;			/* target port(channel) */
	__be16	curr;			/* most recent sample */
	__be16	max;			/* maximum value */
	__be16	num1;			/* number of samples taken */
	u8		num2;
	__be32	accum;			/* sum of samples */
	__be16	xinfo_addr;		/* pointer to XINFO structure */
} __packed;

struct om_info {
	__be16	ver;			/* DCM version number */
	u8		prescale;		/* prescale value used */
	u8		timer;			/* timer */
	u8		count;			/* number of CRECORDS */
	__be16	addr;			/* address of CRECORD array in SRAM */
	u8		res[3];
} __packed;

struct om_xinfo {
	__be16	name_addr;		/* descriptive name */
	__be16	def;			/* SW-defined default */
	__be16	curr;			/* SW-obtained value */
	__be16	cal;			/* calibration factor */
	u8		flags;
	u8		method;
	__be16	get;			/* get current value */
	__be16	set;			/* set voltage to value */
	__be16	init;			/* initialize device */
} __packed;

struct record {
	char	*name;			/* record name */
	char	*unit;			/* record unit */
};

struct dcm_board {
	struct	om_info info;
	struct	record *rec;	/* per record struct */
	struct	crecord *crec;	/* all the collected records */
	u32		shunt;			/* shunt for ina220 current */
	u32		mask;			/* channel mask */
};

struct fsl_dcm_data {
	void __iomem	*base;	/* PIXIS/QIXIS base address */
	struct device	*dev;
	u8 __iomem		*addr;	/* SRAM address */
	u8 __iomem		*data;	/* SRAM data */
	u8 __iomem		*ocmd;	/* DCM command/status */
	u8 __iomem		*omsg;	/* DCM message */
	u8 __iomem		*mack;	/* DCM acknowledge */
	u8				timer;
	int				running;
	struct dcm_board board;
};

#endif
