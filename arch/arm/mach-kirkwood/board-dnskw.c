/*
 * Copyright 2012 (C), Jamie Lentin <jm@lentin.co.uk>
 *
 * arch/arm/mach-kirkwood/board-dnskw.c
 *
 * D-link DNS-320 & DNS-325 NAS Init for drivers not converted to
 * flattened device tree yet.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mv643xx_eth.h>
#include <linux/gpio.h>
#include "common.h"

static struct mv643xx_eth_platform_data dnskw_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

static void dnskw_power_off(void)
{
	gpio_set_value(36, 1);
}

/* Register any GPIO for output and set the value */
static void __init dnskw_gpio_register(unsigned gpio, char *name, int def)
{
	if (gpio_request(gpio, name) == 0 &&
	    gpio_direction_output(gpio, 0) == 0) {
		gpio_set_value(gpio, def);
		if (gpio_export(gpio, 0) != 0)
			pr_err("dnskw: Failed to export GPIO %s\n", name);
	} else
		pr_err("dnskw: Failed to register %s\n", name);
}

void __init dnskw_init(void)
{
	kirkwood_ehci_init();
	kirkwood_ge00_init(&dnskw_ge00_data);

	/* Register power-off GPIO. */
	if (gpio_request(36, "dnskw:power:off") == 0
	    && gpio_direction_output(36, 0) == 0)
		pm_power_off = dnskw_power_off;
	else
		pr_err("dnskw: failed to configure power-off GPIO\n");

	/* Ensure power is supplied to both HDDs */
	dnskw_gpio_register(39, "dnskw:power:sata0", 1);
	dnskw_gpio_register(40, "dnskw:power:sata1", 1);

	/* Set NAS to turn back on after a power failure */
	dnskw_gpio_register(37, "dnskw:power:recover", 1);
}
