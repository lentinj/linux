/*
 * Netgear WNR854T
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

/*
 * PCI setup
 */
static int __init wnr854t_pci_map_irq(const struct pci_dev *dev, u8 slot,
	u8 pin)
{
	int irq;

	/*
	 * Check for devices with hard-wired IRQs.
	 */
	irq = orion5x_pci_map_irq(dev, slot, pin);
	if (irq != -1)
		return irq;

	/*
	 * Mini-PCI slot.
	 */
	if (slot == 7)
		return gpio_to_irq(4);

	return -1;
}

static struct hw_pci wnr854t_pci __initdata = {
	.nr_controllers	= 2,
	.setup		= orion5x_pci_sys_setup,
	.scan		= orion5x_pci_sys_scan_bus,
	.map_irq	= wnr854t_pci_map_irq,
};

static int __init wnr854t_pci_init(void)
{
	if (machine_is_wnr854t())
		pci_common_init(&wnr854t_pci);

	return 0;
}
subsys_initcall(wnr854t_pci_init);

