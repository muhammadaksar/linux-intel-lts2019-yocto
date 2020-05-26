// SPDX-License-Identifier: GPL-2.0+
/* Copyright (C) 2020 Intel Corporation
 *
 * INTEL(R) Ethernet Network Connection GPY
 *
 */

#include <linux/intel_phy.h>
#include <linux/phy.h>

#define GPY_IMASK		0x19	/* interrupt mask */
#define GPY_ISTAT		0x1A	/* interrupt status */
#define GPY_INTR_ANC		BIT(10)	/* Auto-Neg complete */
#define GPY_INTR_DXMC		BIT(2)	/* Duplex mode change */
#define GPY_INTR_LSPC		BIT(1)	/* Link speed change */
#define GPY_INTR_LSTC		BIT(0)	/* Link state change */
#define GPY_INTR_MASK		(GPY_INTR_LSTC | \
				 GPY_INTR_LSPC | \
				 GPY_INTR_DXMC | \
				 GPY_INTR_ANC)

/* GPY VENDOR SPECIFIC 1 */
#define GPY_VSPEC1_SGMII_CTRL	0x8
#define GPY_VSPEC1_SGMII_STS	0x9
#define GPY_SGMII_ANEN		BIT(12)		/* Aneg enable */
#define GPY_SGMII_ANOK		BIT(5)		/* Aneg complete */
#define GPY_SGMII_LS		BIT(2)		/* Link status */
#define GPY_SGMII_DR_MASK	GENMASK(1, 0)	/* Data rate */
#define GPY_SGMII_DR_2500	0x3

/* WA for Q-SPEC GPY115 PHY ID */
#define INTEL_PHY_ID_GPY115_C22		0x67C9DE00

static int gpy_soft_reset(struct phy_device *phydev)
{
	int ret;
	int phy_id;

	ret = phy_read(phydev, MII_PHYSID1);
	phy_id = ret << 16;

	ret = phy_read(phydev, MII_PHYSID2);
	phy_id |= ret;

	/* WA for GPY115 which need a soft reset even after a hard reset */
	if (phy_id == INTEL_PHY_ID_GPY115_C22)
		return genphy_soft_reset(phydev);

	return genphy_no_soft_reset(phydev);
}

static int gpy_config_aneg(struct phy_device *phydev)
{
	bool changed = false;
	u32 adv;
	int ret;

	if (phydev->autoneg == AUTONEG_DISABLE)
		return genphy_c45_pma_setup_forced(phydev);

	ret = genphy_c45_an_config_aneg(phydev);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = true;

	adv = linkmode_adv_to_mii_ctrl1000_t(phydev->advertising);
	ret = phy_modify_changed(phydev, MII_CTRL1000,
				 ADVERTISE_1000FULL | ADVERTISE_1000HALF,
				 adv);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = true;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND1, GPY_VSPEC1_SGMII_STS);

	if (linkmode_test_bit(ETHTOOL_LINK_MODE_2500baseT_Full_BIT,
			      phydev->advertising)) {
		/* For SGMII 2.5Gbps link, MAC side SGMII will turn off
		 * autoneg as not all PHY supports autoneg for 2.5Gbps.
		 * GPY211 will disable autoneg by toggling the BIT12
		 */
		if ((ret & GPY_SGMII_ANOK) && (!(ret & GPY_SGMII_LS))) {
			phy_modify_mmd_changed(phydev, MDIO_MMD_VEND1,
					       GPY_VSPEC1_SGMII_CTRL,
					       GPY_SGMII_ANEN,
					       GPY_SGMII_ANEN);
			phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1,
					   GPY_VSPEC1_SGMII_CTRL,
					   GPY_SGMII_ANEN);

			/* If the SGMII link is not at 2.5Gbps mode,
			 * restart the TPI link by toggling TPI
			 * autoneg bit. As the SGMII buad rate will
			 * follow the TPI link
			 */
			if ((ret & GPY_SGMII_DR_MASK) != GPY_SGMII_DR_2500)
				genphy_c45_an_disable_aneg(phydev);

			changed = true;
		}
	} else {
		/* For SGMII 10/100/1000Mbps link, enable the SGMII
		 * autoneg by toggling the BIT12
		 */
		if (ret & GPY_SGMII_ANOK || (!(ret & GPY_SGMII_LS))) {
			phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1,
					   GPY_VSPEC1_SGMII_CTRL,
					   GPY_SGMII_ANEN);
			phy_modify_mmd_changed(phydev, MDIO_MMD_VEND1,
					       GPY_VSPEC1_SGMII_CTRL,
					       GPY_SGMII_ANEN,
					       GPY_SGMII_ANEN);

			/* If the SGMII link is not at 10/100/1000Mbps,
			 * restart the TPI link by toggling TPI
			 * autoneg bit. As the SGMII buad rate will
			 * follow the TPI link
			 */
			if ((ret & GPY_SGMII_DR_MASK) == GPY_SGMII_DR_2500)
				genphy_c45_an_disable_aneg(phydev);

			changed = true;
		}
	}

	return genphy_c45_check_and_restart_aneg(phydev, changed);
}

static int gpy_ack_interrupt(struct phy_device *phydev)
{
	int reg;

	reg = phy_read(phydev, GPY_ISTAT);
	return (reg < 0) ? reg : 0;
}

static int gpy_did_interrupt(struct phy_device *phydev)
{
	int reg;

	reg = phy_read(phydev, GPY_ISTAT);
	return reg & GPY_INTR_MASK;
}

static int gpy_config_intr(struct phy_device *phydev)
{
	u16 mask = 0;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		mask = GPY_INTR_MASK;

	return phy_write(phydev, GPY_IMASK, mask);
}

static int gpy_read_status(struct phy_device *phydev)
{
	int ret;

	ret = genphy_update_link(phydev);
	if (ret)
		return ret;

	phydev->speed = SPEED_UNKNOWN;
	phydev->duplex = DUPLEX_UNKNOWN;
	phydev->pause = 0;
	phydev->asym_pause = 0;

	if (phydev->autoneg == AUTONEG_ENABLE && phydev->autoneg_complete) {
		/* Read the link partner's 1G advertisement */
		ret = phy_read(phydev, MII_STAT1000);
		if (ret < 0)
			return ret;
		mii_stat1000_mod_linkmode_lpa_t(phydev->lp_advertising, ret);

		/* Read the link partner's base page advertisement */
		ret = phy_read(phydev, MII_LPA);
		if (ret < 0)
			return ret;
		mii_lpa_mod_linkmode_lpa_t(phydev->lp_advertising, ret);

		/* Read the link partner's 10G advertisement */
		ret = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_10GBT_STAT);
		if (ret < 0)
			return ret;
		mii_10gbt_stat_mod_linkmode_lpa_t(phydev->lp_advertising, ret);

		phy_resolve_aneg_linkmode(phydev);
	} else if (phydev->autoneg == AUTONEG_DISABLE) {
		ret = genphy_c45_read_pma(phydev);
	}

	return ret;
}

static struct phy_driver intel_gpy_drivers[] = {
	{
		.phy_id		= INTEL_PHY_ID_GPY,
		.phy_id_mask	= INTEL_PHY_ID_MASK,
		.name		= "INTEL(R) Ethernet Network Connection GPY",
		.get_features	= genphy_c45_pma_read_abilities,
		.aneg_done	= genphy_c45_aneg_done,
		.soft_reset	= gpy_soft_reset,
		.ack_interrupt	= gpy_ack_interrupt,
		.did_interrupt	= gpy_did_interrupt,
		.config_intr	= gpy_config_intr,
		.config_aneg	= gpy_config_aneg,
		.read_status	= gpy_read_status,
		.set_loopback	= genphy_loopback,
	},
};
module_phy_driver(intel_gpy_drivers);

static struct mdio_device_id __maybe_unused intel_gpy[] = {
	{ INTEL_PHY_ID_GPY, INTEL_PHY_ID_MASK },
	{ },
};
MODULE_DEVICE_TABLE(mdio, intel_gpy);

MODULE_DESCRIPTION("INTEL(R) Ethernet Network Connection GPY");
MODULE_LICENSE("GPL");
