/*
 * Copyright 2013 Freescale Semiconductor, Inc.
 *
 * Author: Minghuan Lian <Minghuan.Lian@freescale.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#ifndef _FSL_PCI_EP_H
#define _FSL_PCI_EP_H

#include <linux/fsl_pci_ep_vfio.h>

#define MULTI_FUNCTION_NUM 2
#define PCI_EP_PF_OFFSET 0x2000
#define FSL_PCI_EP_BAR_NUM 4
#define FSL_PCI_EP_OW_NUM 5
#define FSL_PCI_EP_MSIX_OW_SIZE (4 * 1024) /* 4k */
#define FSL_PCI_EP_MSIX_IW_SIZE (8 * 1024) /* 8k */
#define FSL_PCI_MSIX_IW_ATTR 0x80f44000

#define PCI_EP_WIN_INDEX_SHIFT 36
#define PCI_EP_WIN_INDEX_MASK 0xf
#define PCI_EP_WIN_TYPE_SHIFT (PCI_EP_WIN_INDEX_SHIFT + 4)

#define PCI_EP_OFFSET_TO_INDEX(off) \
		(((off) >> PCI_EP_WIN_INDEX_SHIFT) & PCI_EP_WIN_INDEX_MASK)
#define PCI_EP_OFFSET_TO_TYPE(off) ((off) >> PCI_EP_WIN_TYPE_SHIFT)
#define PCI_EP_OFFSET_MASK	(((u64)(1) << PCI_EP_WIN_INDEX_SHIFT) - 1)


struct pci_ep_dev {
	struct list_head	node;
	struct pci_dev		*pdev;
	struct pci_pf_dev	*pf;
	struct device		dev;
	int			idx;
	int			devfn;
	int			type;
	spinlock_t		irqlock;
	int			irq_type;
	u8			iw_num;
	u8			ow_num;
	atomic_t		refcnt;
	int			registered;
};

struct pci_pf_dev {
	struct list_head	node;
	struct list_head	ep_list;
	struct pci_controller	*host;
	struct pci_bus		*pbus;
	struct pci_dev		*pdev;
	struct device		*parent;
	spinlock_t		lock;
	int			idx;
	struct resource		regs_rs;
	struct ccsr_pci __iomem	*regs;
	u8			iw_num;
	u8			ow_num;
	bool			msix_enable;
	resource_size_t mem_offset[3];
	struct resource mem_resources[3];
	/* VF info */
	int			vf_pos;
	u8			vf_enabled;
	u8			vf_iw_num;
	u8			vf_ow_num;
	u16			vf_total; /* total VFs associated with the PF */
	u16			vf_initial; /* initial VFs */
	u16			vf_num; /* number of VFs available */
	u16			vf_offset; /* first VF Routing ID offset */
	u16			vf_stride; /* following VF stride */
	struct vf_atmu_regs __iomem *vf_regs;
};

int fsl_pci_ep_get_win(struct pci_ep_dev *ep, struct pci_ep_win *win);
int fsl_pci_ep_set_win(struct pci_ep_dev *ep, struct pci_ep_win *win);
int fsl_pci_ep_vfio_init(struct pci_ep_dev *ep);
void fsl_pci_ep_vfio_remove(struct pci_ep_dev *ep);

#endif
