// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 * Rockchip CANFD driver
 */

#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/pinctrl/consumer.h>
#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>
#include <linux/can/led.h>
#include <linux/reset.h>
#include <linux/pm_runtime.h>

/* registers definition */
enum rockchip_canfd_reg {
	CAN_MODE = 0x00,
	CAN_CMD = 0x04,
	CAN_STATE = 0x08,
	CAN_INT = 0x0c,
	CAN_INT_MASK = 0x10,
	CAN_LOSTARB_CODE = 0x28,
	CAN_ERR_CODE = 0x2c,
	CAN_RX_ERR_CNT = 0x34,
	CAN_TX_ERR_CNT = 0x38,
	CAN_IDCODE = 0x3c,
	CAN_IDMASK = 0x40,
	CAN_NBTP = 0x100,
	CAN_DBTP = 0x104,
	CAN_TDCR = 0x108,
	CAN_TSCC = 0x10c,
	CAN_TSCV = 0x110,
	CAN_TXEFC = 0x114,
	CAN_RXFC = 0x118,
	CAN_AFC = 0x11c,
	CAN_IDCODE0 = 0x120,
	CAN_IDMASK0 = 0x124,
	CAN_IDCODE1 = 0x128,
	CAN_IDMASK1 = 0x12c,
	CAN_IDCODE2 = 0x130,
	CAN_IDMASK2 = 0x134,
	CAN_IDCODE3 = 0x138,
	CAN_IDMASK3 = 0x13c,
	CAN_IDCODE4 = 0x140,
	CAN_IDMASK4 = 0x144,
	CAN_TXFIC = 0x200,
	CAN_TXID = 0x204,
	CAN_TXDAT0 = 0x208,
	CAN_TXDAT1 = 0x20c,
	CAN_TXDAT2 = 0x210,
	CAN_TXDAT3 = 0x214,
	CAN_TXDAT4 = 0x218,
	CAN_TXDAT5 = 0x21c,
	CAN_TXDAT6 = 0x220,
	CAN_TXDAT7 = 0x224,
	CAN_TXDAT8 = 0x228,
	CAN_TXDAT9 = 0x22c,
	CAN_TXDAT10 = 0x230,
	CAN_TXDAT11 = 0x234,
	CAN_TXDAT12 = 0x238,
	CAN_TXDAT13 = 0x23c,
	CAN_TXDAT14 = 0x240,
	CAN_TXDAT15 = 0x244,
	CAN_RXFIC = 0x300,
	CAN_RXID = 0x304,
	CAN_RXTS = 0x308,
	CAN_RXDAT0 = 0x30c,
	CAN_RXDAT1 = 0x310,
	CAN_RXDAT2 = 0x314,
	CAN_RXDAT3 = 0x318,
	CAN_RXDAT4 = 0x31c,
	CAN_RXDAT5 = 0x320,
	CAN_RXDAT6 = 0x324,
	CAN_RXDAT7 = 0x328,
	CAN_RXDAT8 = 0x32c,
	CAN_RXDAT9 = 0x330,
	CAN_RXDAT10 = 0x334,
	CAN_RXDAT11 = 0x338,
	CAN_RXDAT12 = 0x33c,
	CAN_RXDAT13 = 0x340,
	CAN_RXDAT14 = 0x344,
	CAN_RXDAT15 = 0x348,
	CAN_RXFRD = 0x400,
	CAN_TXEFRD = 0x500,
};

#define DATE_LENGTH_12_BYTE	(0x9)
#define DATE_LENGTH_16_BYTE	(0xa)
#define DATE_LENGTH_20_BYTE	(0xb)
#define DATE_LENGTH_24_BYTE	(0xc)
#define DATE_LENGTH_32_BYTE	(0xd)
#define DATE_LENGTH_48_BYTE	(0xe)
#define DATE_LENGTH_64_BYTE	(0xf)

#define CAN_TX0_REQ		BIT(0)
#define CAN_TX1_REQ		BIT(1)
#define CAN_TX_REQ_FULL		((CAN_TX0_REQ) | (CAN_TX1_REQ))

#define MODE_FDOE		BIT(15)
#define MODE_BRSD		BIT(13)
#define MODE_SPACE_RX		BIT(12)
#define MODE_AUTO_RETX		BIT(10)
#define MODE_RXSORT		BIT(7)
#define MODE_TXORDER		BIT(6)
#define MODE_RXSTX		BIT(5)
#define MODE_LBACK		BIT(4)
#define MODE_SILENT		BIT(3)
#define MODE_SELF_TEST		BIT(2)
#define MODE_SLEEP		BIT(1)
#define RESET_MODE		0
#define WORK_MODE		BIT(0)

#define RX_FINISH_INT		BIT(0)
#define TX_FINISH_INT		BIT(1)
#define ERR_WARN_INT		BIT(2)
#define RX_BUF_OV_INT		BIT(3)
#define PASSIVE_ERR_INT		BIT(4)
#define TX_LOSTARB_INT		BIT(5)
#define BUS_ERR_INT		BIT(6)
#define RX_FIFO_FULL_INT	BIT(7)
#define RX_FIFO_OV_INT		BIT(8)
#define BUS_OFF_INT		BIT(9)
#define BUS_OFF_RECOVERY_INT	BIT(10)
#define TSC_OV_INT		BIT(11)
#define TXE_FIFO_OV_INT		BIT(12)
#define TXE_FIFO_FULL_INT	BIT(13)
#define WAKEUP_INT		BIT(14)

#define ERR_TYPE_MASK		GENMASK(28, 26)
#define ERR_TYPE_SHIFT		26
#define BIT_ERR			0
#define STUFF_ERR		1
#define FORM_ERR		2
#define ACK_ERR			3
#define CRC_ERR			4
#define ERR_DIR_RX		BIT(25)
#define ERR_LOC_MASK		GENMASK(15, 0)

/* Nominal Bit Timing & Prescaler Register (NBTP) */
#define NBTP_MODE_3_SAMPLES	BIT(31)
#define NBTP_NSJW_SHIFT		24
#define NBTP_NSJW_MASK		(0x7f << NBTP_NSJW_SHIFT)
#define NBTP_NBRP_SHIFT		16
#define NBTP_NBRP_MASK		(0xff << NBTP_NBRP_SHIFT)
#define NBTP_NTSEG2_SHIFT	8
#define NBTP_NTSEG2_MASK	(0x7f << NBTP_NTSEG2_SHIFT)
#define NBTP_NTSEG1_SHIFT	0
#define NBTP_NTSEG1_MASK	(0x7f << NBTP_NTSEG1_SHIFT)

/* Data Bit Timing & Prescaler Register (DBTP) */
#define DBTP_MODE_3_SAMPLES	BIT(21)
#define DBTP_DSJW_SHIFT		17
#define DBTP_DSJW_MASK		(0xf << DBTP_DSJW_SHIFT)
#define DBTP_DBRP_SHIFT		9
#define DBTP_DBRP_MASK		(0xff << DBTP_DBRP_SHIFT)
#define DBTP_DTSEG2_SHIFT	5
#define DBTP_DTSEG2_MASK	(0xf << DBTP_DTSEG2_SHIFT)
#define DBTP_DTSEG1_SHIFT	0
#define DBTP_DTSEG1_MASK	(0x1f << DBTP_DTSEG1_SHIFT)

/* Transmitter Delay Compensation Register (TDCR) */
#define TDCR_TDCO_SHIFT		1
#define TDCR_TDCO_MASK		(0x3f << TDCR_TDCO_SHIFT)
#define TDCR_TDC_ENABLE		BIT(0)

#define TX_FD_ENABLE		BIT(5)
#define TX_FD_BRS_ENABLE	BIT(4)

#define FIFO_ENABLE		BIT(0)

#define FORMAT_SHIFT		7
#define FORMAT_MASK		(0x1 << FORMAT_SHIFT)
#define RTR_SHIFT		6
#define RTR_MASK		(0x1 << RTR_SHIFT)
#define FDF_SHIFT		5
#define FDF_MASK		(0x1 << FDF_SHIFT)
#define BRS_SHIFT		4
#define BRS_MASK		(0x1 << BRS_SHIFT)
#define DLC_SHIFT		0
#define DLC_MASK		(0xF << DLC_SHIFT)

#define CAN_RF_SIZE		0x48
#define CAN_TEF_SIZE		0x8
#define CAN_TXEFRD_OFFSET(n)	(CAN_TXEFRD + CAN_TEF_SIZE * (n))
#define CAN_RXFRD_OFFSET(n)	(CAN_RXFRD + CAN_RF_SIZE * (n))

#define DRV_NAME	"rockchip_canfd"

/* rockchip_canfd private data structure */

struct rockchip_canfd {
	struct can_priv can;
	struct device *dev;
	struct clk_bulk_data *clks;
	int num_clks;
	struct reset_control *reset;
	void __iomem *base;
	u32 irqstatus;
};

static inline u32 rockchip_canfd_read(const struct rockchip_canfd *priv,
				      enum rockchip_canfd_reg reg)
{
	return readl(priv->base + reg);
}

static inline void rockchip_canfd_write(const struct rockchip_canfd *priv,
					enum rockchip_canfd_reg reg, u32 val)
{
	writel(val, priv->base + reg);
}

static const struct can_bittiming_const rockchip_canfd_bittiming_const = {
	.name = DRV_NAME,
	.tseg1_min = 1,
	.tseg1_max = 128,
	.tseg2_min = 1,
	.tseg2_max = 128,
	.sjw_max = 128,
	.brp_min = 1,
	.brp_max = 256,
	.brp_inc = 2,
};

static const struct can_bittiming_const rockchip_canfd_data_bittiming_const = {
	.name = DRV_NAME,
	.tseg1_min = 1,
	.tseg1_max = 32,
	.tseg2_min = 1,
	.tseg2_max = 16,
	.sjw_max = 16,
	.brp_min = 1,
	.brp_max = 256,
	.brp_inc = 2,
};

static int set_reset_mode(struct net_device *ndev)
{
	struct rockchip_canfd *rcan = netdev_priv(ndev);

	reset_control_assert(rcan->reset);
	udelay(2);
	reset_control_deassert(rcan->reset);

	rockchip_canfd_write(rcan, CAN_MODE, 0);

	netdev_dbg(ndev, "%s MODE=0x%08x\n", __func__,
		   rockchip_canfd_read(rcan, CAN_MODE));

	return 0;
}

static int set_normal_mode(struct net_device *ndev)
{
	struct rockchip_canfd *rcan = netdev_priv(ndev);
	u32 val;

	val = rockchip_canfd_read(rcan, CAN_MODE);
	val |= WORK_MODE;
	rockchip_canfd_write(rcan, CAN_MODE, val);

	netdev_dbg(ndev, "%s MODE=0x%08x\n", __func__,
		   rockchip_canfd_read(rcan, CAN_MODE));
	return 0;
}

/* bittiming is called in reset_mode only */
static int rockchip_canfd_set_bittiming(struct net_device *ndev)
{
	struct rockchip_canfd *rcan = netdev_priv(ndev);
	const struct can_bittiming *bt = &rcan->can.bittiming;
	const struct can_bittiming *dbt = &rcan->can.data_bittiming;
	u16 brp, sjw, tseg1, tseg2;
	u32 reg_btp;

	brp = (bt->brp >> 1) - 1;
	sjw = bt->sjw - 1;
	tseg1 = bt->prop_seg + bt->phase_seg1 - 1;
	tseg2 = bt->phase_seg2 - 1;
	reg_btp = (brp << NBTP_NBRP_SHIFT) | (sjw << NBTP_NSJW_SHIFT) |
		  (tseg1 << NBTP_NTSEG1_SHIFT) |
		  (tseg2 << NBTP_NTSEG2_SHIFT);

	if (rcan->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES)
		reg_btp |= NBTP_MODE_3_SAMPLES;

	rockchip_canfd_write(rcan, CAN_NBTP, reg_btp);

	if (rcan->can.ctrlmode & CAN_CTRLMODE_FD) {
		reg_btp = 0;
		brp = (dbt->brp >> 1) - 1;
		sjw = dbt->sjw - 1;
		tseg1 = dbt->prop_seg + dbt->phase_seg1 - 1;
		tseg2 = dbt->phase_seg2 - 1;

		if (dbt->bitrate > 2200000) {
			u32 tdco;

			/* Equation based on Bosch's ROCKCHIP_CAN User Manual's
			 * Transmitter Delay Compensation Section
			 */
			tdco = (rcan->can.clock.freq / dbt->bitrate) * 2 / 3;
			/* Max valid TDCO value is 63 */
			if (tdco > 63)
				tdco = 63;

			rockchip_canfd_write(rcan, CAN_TDCR,
					     (tdco << TDCR_TDCO_SHIFT) |
					     TDCR_TDC_ENABLE);
		}

		reg_btp |= (brp << DBTP_DBRP_SHIFT) |
			   (sjw << DBTP_DSJW_SHIFT) |
			   (tseg1 << DBTP_DTSEG1_SHIFT) |
			   (tseg2 << DBTP_DTSEG2_SHIFT);

		if (rcan->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES)
			reg_btp |= DBTP_MODE_3_SAMPLES;

		rockchip_canfd_write(rcan, CAN_DBTP, reg_btp);
	}

	netdev_dbg(ndev, "%s NBTP=0x%08x, DBTP=0x%08x, TDCR=0x%08x\n", __func__,
		   rockchip_canfd_read(rcan, CAN_NBTP),
		   rockchip_canfd_read(rcan, CAN_DBTP),
		   rockchip_canfd_read(rcan, CAN_TDCR));
	return 0;
}

static int rockchip_canfd_get_berr_counter(const struct net_device *ndev,
					   struct can_berr_counter *bec)
{
	struct rockchip_canfd *rcan = netdev_priv(ndev);
	int err;

	err = pm_runtime_get_sync(rcan->dev);
	if (err < 0) {
		netdev_err(ndev, "%s: pm_runtime_get failed(%d)\n",
			   __func__, err);
		return err;
	}

	bec->rxerr = rockchip_canfd_read(rcan, CAN_RX_ERR_CNT);
	bec->txerr = rockchip_canfd_read(rcan, CAN_TX_ERR_CNT);

	pm_runtime_put(rcan->dev);

	netdev_dbg(ndev, "%s RX_ERR_CNT=0x%08x, TX_ERR_CNT=0x%08x\n", __func__,
		   rockchip_canfd_read(rcan, CAN_RX_ERR_CNT),
		   rockchip_canfd_read(rcan, CAN_TX_ERR_CNT));

	return 0;
}

static int rockchip_canfd_start(struct net_device *ndev)
{
	struct rockchip_canfd *rcan = netdev_priv(ndev);
	u32 val;

	/* we need to enter the reset mode */
	set_reset_mode(ndev);

	rockchip_canfd_write(rcan, CAN_INT_MASK, 0);

	/* RECEIVING FILTER, accept all */
	rockchip_canfd_write(rcan, CAN_IDCODE, 0);
	rockchip_canfd_write(rcan, CAN_IDMASK, 0xfffffff);
	rockchip_canfd_write(rcan, CAN_IDCODE0, 0);
	rockchip_canfd_write(rcan, CAN_IDMASK0, 0xfffffff);
	rockchip_canfd_write(rcan, CAN_IDCODE1, 0);
	rockchip_canfd_write(rcan, CAN_IDMASK1, 0xfffffff);
	rockchip_canfd_write(rcan, CAN_IDCODE2, 0);
	rockchip_canfd_write(rcan, CAN_IDMASK2, 0xfffffff);
	rockchip_canfd_write(rcan, CAN_IDCODE3, 0);
	rockchip_canfd_write(rcan, CAN_IDMASK3, 0xfffffff);
	rockchip_canfd_write(rcan, CAN_IDCODE4, 0);
	rockchip_canfd_write(rcan, CAN_IDMASK4, 0xfffffff);

	/* set mode */
	val = rockchip_canfd_read(rcan, CAN_MODE);

	/* rx fifo enable */
	rockchip_canfd_write(rcan, CAN_RXFC,
			     rockchip_canfd_read(rcan, CAN_RXFC) | FIFO_ENABLE);

	/* Canfd Mode */
	if (rcan->can.ctrlmode & CAN_CTRLMODE_FD) {
		val |= MODE_FDOE;
		rockchip_canfd_write(rcan, CAN_TXFIC,
				     rockchip_canfd_read(rcan, CAN_TXFIC) |
				     TX_FD_ENABLE);
	}

	/* Loopback Mode */
	if (rcan->can.ctrlmode & CAN_CTRLMODE_LOOPBACK)
		val |= MODE_SELF_TEST | MODE_LBACK;

	val |= MODE_AUTO_RETX;

	rockchip_canfd_write(rcan, CAN_MODE, val);

	rockchip_canfd_set_bittiming(ndev);

	set_normal_mode(ndev);

	rcan->can.state = CAN_STATE_ERROR_ACTIVE;

	netdev_dbg(ndev, "%s MODE=0x%08x, INT_MASK=0x%08x\n", __func__,
		   rockchip_canfd_read(rcan, CAN_MODE),
		   rockchip_canfd_read(rcan, CAN_INT_MASK));

	return 0;
}

static int rockchip_canfd_stop(struct net_device *ndev)
{
	struct rockchip_canfd *rcan = netdev_priv(ndev);

	rcan->can.state = CAN_STATE_STOPPED;
	/* we need to enter reset mode */
	set_reset_mode(ndev);

	/* disable all interrupts */
	rockchip_canfd_write(rcan, CAN_INT_MASK, 0xffff);

	netdev_dbg(ndev, "%s MODE=0x%08x, INT_MASK=0x%08x\n", __func__,
		   rockchip_canfd_read(rcan, CAN_MODE),
		   rockchip_canfd_read(rcan, CAN_INT_MASK));
	return 0;
}

static int rockchip_canfd_set_mode(struct net_device *ndev,
				   enum can_mode mode)
{
	int err;

	switch (mode) {
	case CAN_MODE_START:
		err = rockchip_canfd_start(ndev);
		if (err) {
			netdev_err(ndev, "starting CAN controller failed!\n");
			return err;
		}
		if (netif_queue_stopped(ndev))
			netif_wake_queue(ndev);
		break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

/* transmit a CAN message
 * message layout in the sk_buff should be like this:
 * xx xx xx xx         ff         ll 00 11 22 33 44 55 66 77
 * [ can_id ] [flags] [len] [can data (up to 8 bytes]
 */
static int rockchip_canfd_start_xmit(struct sk_buff *skb,
				     struct net_device *ndev)
{
	struct rockchip_canfd *rcan = netdev_priv(ndev);
	struct canfd_frame *cf = (struct canfd_frame *)skb->data;
	u32 id, dlc;
	u32 cmd = CAN_TX0_REQ;
	int i;

	if (can_dropped_invalid_skb(ndev, skb))
		return NETDEV_TX_OK;

	netif_stop_queue(ndev);

	if (rockchip_canfd_read(rcan, CAN_CMD) & CAN_TX0_REQ)
		cmd = CAN_TX1_REQ;

	/* Watch carefully on the bit sequence */
	if (cf->can_id & CAN_EFF_FLAG) {
		/* Extended CAN ID format */
		id = cf->can_id & CAN_EFF_MASK;
		dlc = can_len2dlc(cf->len) & DLC_MASK;
		dlc |= FORMAT_MASK;

		/* Extended frames remote TX request */
		if (cf->can_id & CAN_RTR_FLAG)
			dlc |= RTR_MASK;
	} else {
		/* Standard CAN ID format */
		id = cf->can_id & CAN_SFF_MASK;
		dlc = can_len2dlc(cf->len) & DLC_MASK;

		/* Standard frames remote TX request */
		if (cf->can_id & CAN_RTR_FLAG)
			dlc |= RTR_MASK;
	}

	if ((rcan->can.ctrlmode & CAN_CTRLMODE_FD) && can_is_canfd_skb(skb)) {
		dlc |= TX_FD_ENABLE;
		if (cf->flags & CANFD_BRS)
			dlc |= TX_FD_BRS_ENABLE;
	}

	rockchip_canfd_write(rcan, CAN_TXID, id);
	rockchip_canfd_write(rcan, CAN_TXFIC, dlc);

	for (i = 0; i < cf->len; i += 4)
		rockchip_canfd_write(rcan, CAN_TXDAT0 + i,
				     *(u32 *)(cf->data + i));

	can_put_echo_skb(skb, ndev, 0);

	rockchip_canfd_write(rcan, CAN_CMD, cmd);

	return NETDEV_TX_OK;
}

static int rockchip_canfd_rx(struct net_device *ndev)
{
	struct rockchip_canfd *rcan = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	struct canfd_frame *cf;
	struct sk_buff *skb;
	u32 id_rockchip_canfd, dlc;
	int i = 0;
	u32 __maybe_unused ts, ret;
	u32 data[16] = {0};

	dlc = rockchip_canfd_read(rcan, CAN_RXFRD);
	id_rockchip_canfd = rockchip_canfd_read(rcan, CAN_RXFRD);
	ts = rockchip_canfd_read(rcan, CAN_RXFRD);
	for (i = 0; i < 16; i++)
		data[i] = rockchip_canfd_read(rcan, CAN_RXFRD);

	/* create zero'ed CAN frame buffer */
	if (dlc & FDF_MASK)
		skb = alloc_canfd_skb(ndev, &cf);
	else
		skb = alloc_can_skb(ndev, (struct can_frame **)&cf);
	if (!skb) {
		stats->rx_dropped++;
		return 0;
	}

	/* Change CAN data length format to socketCAN data format */
	if (dlc & FDF_MASK)
		cf->len = can_dlc2len(dlc & DLC_MASK);
	else
		cf->len = get_can_dlc(dlc & DLC_MASK);

	/* Change CAN ID format to socketCAN ID format */
	if (dlc & FORMAT_MASK) {
		/* The received frame is an Extended format frame */
		cf->can_id = id_rockchip_canfd;
		cf->can_id |= CAN_EFF_FLAG;
		if (dlc & RTR_MASK)
			cf->can_id |= CAN_RTR_FLAG;
	} else {
		/* The received frame is a standard format frame */
		cf->can_id = id_rockchip_canfd;
		if (dlc & RTR_MASK)
			cf->can_id |= CAN_RTR_FLAG;
	}

	if (dlc & BRS_MASK)
		cf->flags |= CANFD_BRS;

	if (!(cf->can_id & CAN_RTR_FLAG)) {
		/* Change CAN data format to socketCAN data format */
		for (i = 0; i < cf->len; i += 4)
			*(u32 *)(cf->data + i) = data[i / 4];
	}

	stats->rx_packets++;
	stats->rx_bytes += cf->len;
	netif_rx(skb);

	can_led_event(ndev, CAN_LED_EVENT_RX);

	return 1;
}

static int rockchip_canfd_err(struct net_device *ndev, u8 isr)
{
	struct rockchip_canfd *rcan = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	enum can_state state = rcan->can.state;
	enum can_state rx_state, tx_state;
	struct can_frame *cf;
	struct sk_buff *skb;
	unsigned int rxerr, txerr;
	u32 ecc, alc;
	u32 sta_reg;

	skb = alloc_can_err_skb(ndev, &cf);

	rxerr = rockchip_canfd_read(rcan, CAN_RX_ERR_CNT);
	txerr = rockchip_canfd_read(rcan, CAN_TX_ERR_CNT);
	sta_reg = rockchip_canfd_read(rcan, CAN_STATE);

	if (skb) {
		cf->data[6] = txerr;
		cf->data[7] = rxerr;
	}

	if (isr & RX_BUF_OV_INT) {
		/* data overrun interrupt */
		netdev_dbg(ndev, "data overrun interrupt\n");
		if (likely(skb)) {
			cf->can_id |= CAN_ERR_CRTL;
			cf->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;
		}
		stats->rx_over_errors++;
		stats->rx_errors++;

		/* reset the CAN IP by entering reset mode
		 * ignoring timeout error
		 */
		set_reset_mode(ndev);
		set_normal_mode(ndev);
	}

	if (isr & ERR_WARN_INT) {
		/* error warning interrupt */
		netdev_dbg(ndev, "error warning interrupt\n");

		if (sta_reg & BUS_OFF_INT)
			state = CAN_STATE_BUS_OFF;
		else if (sta_reg & ERR_WARN_INT)
			state = CAN_STATE_ERROR_WARNING;
		else
			state = CAN_STATE_ERROR_ACTIVE;
	}

	if (isr & BUS_ERR_INT) {
		/* bus error interrupt */
		netdev_dbg(ndev, "bus error interrupt, retry it.\n");
		rcan->can.can_stats.bus_error++;
		stats->rx_errors++;

		if (likely(skb)) {
			ecc = rockchip_canfd_read(rcan, CAN_ERR_CODE);

			cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;

			switch ((ecc & ERR_TYPE_MASK) >> ERR_TYPE_SHIFT) {
			case BIT_ERR:
				cf->data[2] |= CAN_ERR_PROT_BIT;
				break;
			case FORM_ERR:
				cf->data[2] |= CAN_ERR_PROT_FORM;
				break;
			case STUFF_ERR:
				cf->data[2] |= CAN_ERR_PROT_STUFF;
				break;
			default:
				cf->data[3] = ecc & ERR_LOC_MASK;
				break;
			}
			/* error occurred during transmission? */
			if ((ecc & ERR_DIR_RX) == 0)
				cf->data[2] |= CAN_ERR_PROT_TX;
		}
	}

	if (isr & PASSIVE_ERR_INT) {
		/* error passive interrupt */
		netdev_dbg(ndev, "error passive interrupt\n");
		if (state == CAN_STATE_ERROR_PASSIVE)
			state = CAN_STATE_ERROR_WARNING;
		else
			state = CAN_STATE_ERROR_PASSIVE;
	}
	if (isr & TX_LOSTARB_INT) {
		/* arbitration lost interrupt */
		netdev_dbg(ndev, "arbitration lost interrupt\n");
		alc = rockchip_canfd_read(rcan, CAN_LOSTARB_CODE);
		rcan->can.can_stats.arbitration_lost++;
		stats->tx_errors++;
		if (likely(skb)) {
			cf->can_id |= CAN_ERR_LOSTARB;
			cf->data[0] = alc;
		}
	}

	if (state != rcan->can.state) {
		tx_state = txerr >= rxerr ? state : 0;
		rx_state = txerr <= rxerr ? state : 0;

		if (likely(skb))
			can_change_state(ndev, cf, tx_state, rx_state);
		else
			rcan->can.state = state;
		if (state == CAN_STATE_BUS_OFF)
			can_bus_off(ndev);
	}

	if (likely(skb)) {
		stats->rx_packets++;
		stats->rx_bytes += cf->can_dlc;
		netif_rx(skb);
	} else {
		return -ENOMEM;
	}

	return 0;
}

static irqreturn_t rockchip_canfd_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = (struct net_device *)dev_id;
	struct rockchip_canfd *rcan = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	u8 err_int = ERR_WARN_INT | RX_BUF_OV_INT | PASSIVE_ERR_INT |
		     TX_LOSTARB_INT | BUS_ERR_INT;
	u8 isr;
	u32 dlc = 0;

	isr = rockchip_canfd_read(rcan, CAN_INT);
	if (isr & TX_FINISH_INT) {
		dlc = rockchip_canfd_read(rcan, CAN_TXFIC);
		/* transmission complete interrupt */
		if (dlc & FDF_MASK)
			stats->tx_bytes += can_dlc2len(dlc & DLC_MASK);
		else
			stats->tx_bytes += (dlc & DLC_MASK);
		stats->tx_packets++;
		rockchip_canfd_write(rcan, CAN_CMD, 0);
		can_get_echo_skb(ndev, 0);
		netif_wake_queue(ndev);
		can_led_event(ndev, CAN_LED_EVENT_TX);
	}

	if (isr & RX_FINISH_INT)
		rockchip_canfd_rx(ndev);

	if (isr & err_int) {
		/* error interrupt */
		if (rockchip_canfd_err(ndev, isr))
			netdev_err(ndev, "can't allocate buffer - clearing pending interrupts\n");
	}

	rockchip_canfd_write(rcan, CAN_INT, isr);
	return IRQ_HANDLED;
}

static int rockchip_canfd_open(struct net_device *ndev)
{
	struct rockchip_canfd *rcan = netdev_priv(ndev);
	int err;

	/* common open */
	err = open_candev(ndev);
	if (err)
		return err;

	err = pm_runtime_get_sync(rcan->dev);
	if (err < 0) {
		netdev_err(ndev, "%s: pm_runtime_get failed(%d)\n",
			   __func__, err);
		goto exit;
	}

	err = rockchip_canfd_start(ndev);
	if (err) {
		netdev_err(ndev, "could not start CAN peripheral\n");
		goto exit_can_start;
	}

	can_led_event(ndev, CAN_LED_EVENT_OPEN);
	netif_start_queue(ndev);

	netdev_dbg(ndev, "%s\n", __func__);
	return 0;

exit_can_start:
	pm_runtime_put(rcan->dev);
exit:
	close_candev(ndev);
	return err;
}

static int rockchip_canfd_close(struct net_device *ndev)
{
	struct rockchip_canfd *rcan = netdev_priv(ndev);

	netif_stop_queue(ndev);
	rockchip_canfd_stop(ndev);
	close_candev(ndev);
	can_led_event(ndev, CAN_LED_EVENT_STOP);
	pm_runtime_put(rcan->dev);

	netdev_dbg(ndev, "%s\n", __func__);
	return 0;
}

static const struct net_device_ops rockchip_canfd_netdev_ops = {
	.ndo_open = rockchip_canfd_open,
	.ndo_stop = rockchip_canfd_close,
	.ndo_start_xmit = rockchip_canfd_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

/**
 * rockchip_canfd_suspend - Suspend method for the driver
 * @dev:	Address of the device structure
 *
 * Put the driver into low power mode.
 * Return: 0 on success and failure value on error
 */
static int __maybe_unused rockchip_canfd_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);

	if (netif_running(ndev)) {
		netif_stop_queue(ndev);
		netif_device_detach(ndev);
		rockchip_canfd_stop(ndev);
	}

	return pm_runtime_force_suspend(dev);
}

/**
 * rockchip_canfd_resume - Resume from suspend
 * @dev:	Address of the device structure
 *
 * Resume operation after suspend.
 * Return: 0 on success and failure value on error
 */
static int __maybe_unused rockchip_canfd_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_force_resume(dev);
	if (ret) {
		dev_err(dev, "pm_runtime_force_resume failed on resume\n");
		return ret;
	}

	if (netif_running(ndev)) {
		ret = rockchip_canfd_start(ndev);
		if (ret) {
			dev_err(dev, "rockchip_canfd_chip_start failed on resume\n");
			return ret;
		}

		netif_device_attach(ndev);
		netif_start_queue(ndev);
	}

	return 0;
}

/**
 * rockchip_canfd_runtime_suspend - Runtime suspend method for the driver
 * @dev:	Address of the device structure
 *
 * Put the driver into low power mode.
 * Return: 0 always
 */
static int __maybe_unused rockchip_canfd_runtime_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct rockchip_canfd *rcan = netdev_priv(ndev);

	clk_bulk_disable_unprepare(rcan->num_clks, rcan->clks);

	return 0;
}

/**
 * rockchip_canfd_runtime_resume - Runtime resume from suspend
 * @dev:	Address of the device structure
 *
 * Resume operation after suspend.
 * Return: 0 on success and failure value on error
 */
static int __maybe_unused rockchip_canfd_runtime_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct rockchip_canfd *rcan = netdev_priv(ndev);
	int ret;

	ret = clk_bulk_prepare_enable(rcan->num_clks, rcan->clks);
	if (ret) {
		dev_err(dev, "Cannot enable clock.\n");
		return ret;
	}

	return 0;
}

static const struct dev_pm_ops rockchip_canfd_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(rockchip_canfd_suspend, rockchip_canfd_resume)
	SET_RUNTIME_PM_OPS(rockchip_canfd_runtime_suspend,
			   rockchip_canfd_runtime_resume, NULL)
};

static const struct of_device_id rockchip_canfd_of_match[] = {
	{ .compatible = "rockchip,canfd-1.0" },
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_canfd_of_match);

static int rockchip_canfd_probe(struct platform_device *pdev)
{
	struct net_device *ndev;
	struct rockchip_canfd *rcan;
	struct resource *res;
	void __iomem *addr;
	int err, irq;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "could not get a valid irq\n");
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(addr))
		return -EBUSY;

	ndev = alloc_candev(sizeof(struct rockchip_canfd), 1);
	if (!ndev) {
		dev_err(&pdev->dev, "could not allocate memory for CANFD device\n");
		return -ENOMEM;
	}
	rcan = netdev_priv(ndev);

	/* register interrupt handler */
	err = devm_request_irq(&pdev->dev, irq, rockchip_canfd_interrupt,
			       0, ndev->name, ndev);
	if (err) {
		dev_err(&pdev->dev, "request_irq err: %d\n", err);
		return err;
	}

	rcan->reset = devm_reset_control_array_get(&pdev->dev, false, false);
	if (IS_ERR(rcan->reset)) {
		if (PTR_ERR(rcan->reset) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "failed to get canfd reset lines\n");
		return PTR_ERR(rcan->reset);
	}
	rcan->num_clks = devm_clk_bulk_get_all(&pdev->dev, &rcan->clks);
	if (rcan->num_clks < 1)
		return -ENODEV;

	rcan->base = addr;
	rcan->can.clock.freq = clk_get_rate(rcan->clks[0].clk);
	rcan->dev = &pdev->dev;
	rcan->can.state = CAN_STATE_STOPPED;
	rcan->can.bittiming_const = &rockchip_canfd_bittiming_const;
	rcan->can.data_bittiming_const = &rockchip_canfd_data_bittiming_const;
	rcan->can.do_set_mode = rockchip_canfd_set_mode;
	rcan->can.do_get_berr_counter = rockchip_canfd_get_berr_counter;
	rcan->can.do_set_bittiming = rockchip_canfd_set_bittiming;
	rcan->can.do_set_data_bittiming = rockchip_canfd_set_bittiming;
	rcan->can.ctrlmode = CAN_CTRLMODE_FD;
	/* IFI CANFD can do both Bosch FD and ISO FD */
	rcan->can.ctrlmode_supported = CAN_CTRLMODE_LOOPBACK |
				       CAN_CTRLMODE_FD;

	ndev->netdev_ops = &rockchip_canfd_netdev_ops;
	ndev->irq = irq;
	ndev->flags |= IFF_ECHO;

	platform_set_drvdata(pdev, ndev);
	SET_NETDEV_DEV(ndev, &pdev->dev);

	pm_runtime_enable(&pdev->dev);
	err = pm_runtime_get_sync(&pdev->dev);
	if (err < 0) {
		dev_err(&pdev->dev, "%s: pm_runtime_get failed(%d)\n",
			__func__, err);
		goto err_pmdisable;
	}

	err = register_candev(ndev);
	if (err) {
		dev_err(&pdev->dev, "registering %s failed (err=%d)\n",
			DRV_NAME, err);
		goto err_disableclks;
	}

	devm_can_led_init(ndev);

	return 0;

err_disableclks:
	pm_runtime_put(&pdev->dev);
err_pmdisable:
	pm_runtime_disable(&pdev->dev);
	free_candev(ndev);

	return err;
}

static int rockchip_canfd_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);

	unregister_netdev(ndev);
	pm_runtime_disable(&pdev->dev);
	free_candev(ndev);

	return 0;
}

static struct platform_driver rockchip_canfd_driver = {
	.driver = {
		.name = DRV_NAME,
		.pm = &rockchip_canfd_dev_pm_ops,
		.of_match_table = rockchip_canfd_of_match,
	},
	.probe = rockchip_canfd_probe,
	.remove = rockchip_canfd_remove,
};
module_platform_driver(rockchip_canfd_driver);

MODULE_AUTHOR("Elaine Zhang <zhangqing@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip CANFD Drivers");