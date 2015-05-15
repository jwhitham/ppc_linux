/*
 * CAAM control-plane driver backend
 * Controller-level driver, kernel property detection, initialization
 *
 * Copyright 2008-2012 Freescale Semiconductor, Inc.
 */

#include "compat.h"
#include "regs.h"
#include "intern.h"
#include "jr.h"
#include "desc_constr.h"
#include "error.h"

#ifdef CONFIG_CAAM_QI
#include "qi.h"
#endif

/*
 * Descriptor to instantiate RNG State Handle 0 in normal mode and
 * load the JDKEK, TDKEK and TDSK registers
 */
static void build_instantiation_desc(u32 *desc, int handle, int do_sk)
{
	u32 *jump_cmd, op_flags;

	init_job_desc(desc, 0);

	op_flags = OP_TYPE_CLASS1_ALG | OP_ALG_ALGSEL_RNG |
			(handle << OP_ALG_AAI_SHIFT) | OP_ALG_AS_INIT;

	/* INIT RNG in non-test mode */
	append_operation(desc, op_flags);

	if (!handle && do_sk) {
		/*
		 * For SH0, Secure Keys must be generated as well
		 */

		/* wait for done */
		jump_cmd = append_jump(desc, JUMP_CLASS_CLASS1);
		set_jump_tgt_here(desc, jump_cmd);

		/*
		 * load 1 to clear written reg:
		 * resets the done interrrupt and returns the RNG to idle.
		 */
		append_load_imm_u32(desc, 1, LDST_SRCDST_WORD_CLRW);

		/* Initialize State Handle  */
		append_operation(desc, OP_TYPE_CLASS1_ALG | OP_ALG_ALGSEL_RNG |
				 OP_ALG_AAI_RNG4_SK);
	}

	append_jump(desc, JUMP_CLASS_CLASS1 | JUMP_TYPE_HALT);
}

/* Descriptor for deinstantiation of State Handle 0 of the RNG block. */
static void build_deinstantiation_desc(u32 *desc, int handle)
{
	init_job_desc(desc, 0);

	/* Uninstantiate State Handle 0 */
	append_operation(desc, OP_TYPE_CLASS1_ALG | OP_ALG_ALGSEL_RNG |
			 (handle << OP_ALG_AAI_SHIFT) | OP_ALG_AS_INITFINAL);

	append_jump(desc, JUMP_CLASS_CLASS1 | JUMP_TYPE_HALT);
}

/*
 * run_descriptor_deco0 - runs a descriptor on DECO0, under direct control of
 *			  the software (no JR/QI used).
 * @ctrldev - pointer to device
 * @status - descriptor status, after being run
 *
 * Return: - 0 if no error occurred
 *	   - -ENODEV if the DECO couldn't be acquired
 *	   - -EAGAIN if an error occurred while executing the descriptor
 *	   - -EINVAL if the descriptor length is incorrect
 */
static inline int run_descriptor_deco0(struct device *ctrldev, u32 *desc,
					u32 *status)
{
	struct caam_drv_private *ctrlpriv = dev_get_drvdata(ctrldev);
	struct caam_ctrl __iomem *ctrl = ctrlpriv->ctrl;
	struct caam_deco __iomem *deco = ctrlpriv->deco;
	unsigned int timeout = 100000;
	u32 deco_dbg_reg, flags;
	int i, ret, dlen;


	if (ctrlpriv->virt_en == 1) {
		setbits32(&ctrl->deco_rsr, DECORSR_JR0);

		while (!(rd_reg32(&ctrl->deco_rsr) & DECORSR_VALID) &&
		       --timeout)
			cpu_relax();

		timeout = 100000;
	}

	setbits32(&ctrl->deco_rq, DECORR_RQD0ENABLE);

	while (!(rd_reg32(&ctrl->deco_rq) & DECORR_DEN0) &&
								 --timeout)
		cpu_relax();

	if (!timeout) {
		dev_err(ctrldev, "failed to acquire DECO 0\n");
		ret = -ENODEV;
		goto out_err;
	}

	dlen = desc_len(desc);
	if (dlen > MAX_CAAM_DESCSIZE) {
		dev_err(ctrldev, "invalid descriptor length\n");
		ret = -EINVAL;
		goto out_err;
	}

	for (i = 0; i < dlen; i++)
		wr_reg32(&deco->descbuf[i], *(desc + i));

	flags = DECO_JQCR_WHL;
	/*
	 * If the descriptor length is longer than 4 words, then the
	 * FOUR bit in JRCTRL register must be set.
	 */
	if (dlen >= 4)
		flags |= DECO_JQCR_FOUR;

	/* Instruct the DECO to execute it */
	wr_reg32(&deco->jr_ctl_hi, flags);

	timeout = 10000000;
	do {
		deco_dbg_reg = rd_reg32(&deco->desc_dbg);
		/*
		 * If an error occured in the descriptor, then
		 * the DECO status field will be set to 0x0D
		 */
		if ((deco_dbg_reg & DESC_DBG_DECO_STAT_MASK) ==
		    DESC_DBG_DECO_STAT_HOST_ERR)
			break;
		cpu_relax();
	} while ((deco_dbg_reg & DESC_DBG_DECO_STAT_VALID) && --timeout);

	*status = rd_reg32(&deco->op_status_hi) &
		  DECO_OP_STATUS_HI_ERR_MASK;

	if (ctrlpriv->virt_en == 1)
		clrbits32(&ctrl->deco_rsr, DECORSR_JR0);

	ret = timeout ? 0 : -EAGAIN;

out_err:
	/* Mark the DECO as free */
	clrbits32(&ctrl->deco_rq, DECORR_RQD0ENABLE);
	return ret;
}

/*
 * instantiate_rng - builds and executes a descriptor on DECO0,
 *		     which initializes the RNG block.
 * @ctrldev - pointer to device
 * @state_handle_mask - bitmask containing the instantiation status
 *			for the RNG4 state handles which exist in
 *			the RNG4 block: 1 if it's been instantiated
 *			by an external entry, 0 otherwise.
 * @gen_sk  - generate data to be loaded into the JDKEK, TDKEK and TDSK;
 *	      Caution: this can be done only once; if the keys need to be
 *	      regenerated, a POR is required
 *
 * Return: - 0 if no error occurred
 *	   - -ENOMEM if there isn't enough memory to allocate the descriptor
 *	   - -ENODEV if DECO0 couldn't be acquired
 *	   - -EAGAIN if an error occurred when executing the descriptor
 *	      f.i. there was a RNG hardware error due to not "good enough"
 *	      entropy being aquired.
 *	   - -EINVAL if the descriptor length is incorrect
 */
static int instantiate_rng(struct device *ctrldev, int state_handle_mask,
			   int gen_sk)
{
	struct caam_drv_private *ctrlpriv = dev_get_drvdata(ctrldev);
	struct caam_ctrl __iomem *ctrl;
	u32 *desc, status, rdsta_val;
	int ret = 0, sh_idx;

	ctrl = (struct caam_ctrl __iomem *)ctrlpriv->ctrl;
	desc = kmalloc(CAAM_CMD_SZ * 7, GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	for (sh_idx = 0; sh_idx < RNG4_MAX_HANDLES; sh_idx++) {
		/*
		 * If the corresponding bit is set, this state handle
		 * was initialized by somebody else, so it's left alone.
		 */
		if ((1 << sh_idx) & state_handle_mask)
			continue;

		/* Create the descriptor for instantiating RNG State Handle */
		build_instantiation_desc(desc, sh_idx, gen_sk);

		/* Try to run it through DECO0 */
		ret = run_descriptor_deco0(ctrldev, desc, &status);
		if (ret)
			break;
		/*
		 * If ret is not 0, or descriptor status is not 0, then
		 * something went wrong. No need to try the next state
		 * handle (if available), bail out here.
		 * Also, if for some reason, the State Handle didn't get
		 * instantiated although the descriptor has finished
		 * without any error (HW optimizations for later
		 * CAAM eras), then try again.
		 */
		rdsta_val =
			rd_reg32(&ctrl->r4tst[0].rdsta) & RDSTA_IFMASK;
		if (status || !(rdsta_val & (1 << sh_idx)))
			ret = -EAGAIN;
		if (ret)
			break;
		dev_info(ctrldev, "Instantiated RNG4 SH%d\n", sh_idx);
		/* Clear the contents before recreating the descriptor */
		memset(desc, 0x00, CAAM_CMD_SZ * 7);
	}

	kfree(desc);

	return ret;
}

/*
 * deinstantiate_rng - builds and executes a descriptor on DECO0,
 *		       which deinitializes the RNG block.
 * @ctrldev - pointer to device
 * @state_handle_mask - bitmask containing the instantiation status
 *			for the RNG4 state handles which exist in
 *			the RNG4 block: 1 if it's been instantiated
 *
 * Return: - 0 if no error occurred
 *	   - -ENOMEM if there isn't enough memory to allocate the descriptor
 *	   - -ENODEV if DECO0 couldn't be acquired
 *	   - -EAGAIN if an error occurred when executing the descriptor
 *	   - -EINVAL if the descriptor length is incorrect
 */
static int deinstantiate_rng(struct device *ctrldev, int state_handle_mask)
{
	u32 *desc, status;
	int sh_idx, ret = 0;

	desc = kmalloc(CAAM_CMD_SZ * 3, GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	for (sh_idx = 0; sh_idx < RNG4_MAX_HANDLES; sh_idx++) {
		/*
		 * If the corresponding bit is set, then it means the state
		 * handle was initialized by us, and thus it needs to be
		 * deintialized as well
		 */
		if ((1 << sh_idx) & state_handle_mask) {
			/*
			 * Create the descriptor for deinstantating this state
			 * handle
			 */
			build_deinstantiation_desc(desc, sh_idx);

			/* Try to run it through DECO0 */
			ret = run_descriptor_deco0(ctrldev, desc, &status);

			if (ret || status) {
				dev_err(ctrldev,
					"Failed to deinstantiate RNG4 SH%d\n",
					sh_idx);
				break;
			}
			dev_info(ctrldev, "Deinstantiated RNG4 SH%d\n", sh_idx);
		}
	}

	kfree(desc);

	return ret;
}

static int caam_remove(struct platform_device *pdev)
{
	struct device *ctrldev;
	struct caam_drv_private *ctrlpriv;
	struct caam_ctrl __iomem *ctrl;
	int ring, ret = 0;

	ctrldev = &pdev->dev;
	ctrlpriv = dev_get_drvdata(ctrldev);
	ctrl = (struct caam_ctrl __iomem *)ctrlpriv->ctrl;

	/* Remove platform devices for JobRs */
	for (ring = 0; ring < ctrlpriv->total_jobrs; ring++) {
		if (ctrlpriv->jrpdev[ring])
			of_device_unregister(ctrlpriv->jrpdev[ring]);
	}

#ifdef CONFIG_CAAM_QI
	if (ctrlpriv->qidev)
		caam_qi_shutdown(ctrlpriv->qidev);
#endif
	/* De-initialize RNG state handles initialized by this driver. */
	if (ctrlpriv->rng4_sh_init)
		deinstantiate_rng(ctrldev, ctrlpriv->rng4_sh_init);

	/* Shut down debug views */
#ifdef CONFIG_DEBUG_FS
	debugfs_remove_recursive(ctrlpriv->dfs_root);
#endif

	/* Unmap controller region */
	iounmap(&ctrl);

	kfree(ctrlpriv->jrpdev);
	kfree(ctrlpriv);

	return ret;
}

/*
 * kick_trng - sets the various parameters for enabling the initialization
 *	       of the RNG4 block in CAAM
 * @pdev - pointer to the caam device
 * @ent_delay - Defines the length (in system clocks) of each entropy sample.
 */
static void kick_trng(struct device *dev, int ent_delay)
{
	struct caam_drv_private *ctrlpriv = dev_get_drvdata(dev);
	struct caam_ctrl __iomem *ctrl;
	struct rng4tst __iomem *r4tst;
	u32 val;

	ctrl = (struct caam_ctrl __iomem *)ctrlpriv->ctrl;
	r4tst = &ctrl->r4tst[0];

	/* put RNG4 into program mode */
	setbits32(&r4tst->rtmctl, RTMCTL_PRGM);

	/*
	 * Performance-wise, it does not make sense to
	 * set the delay to a value that is lower
	 * than the last one that worked (i.e. the state handles
	 * were instantiated properly. Thus, instead of wasting
	 * time trying to set the values controlling the sample
	 * frequency, the function simply returns.
	 */
	val = (rd_reg32(&r4tst->rtsdctl) & RTSDCTL_ENT_DLY_MASK)
	      >> RTSDCTL_ENT_DLY_SHIFT;
	if (ent_delay <= val) {
		/* put RNG4 into run mode */
		clrbits32(&r4tst->rtmctl, RTMCTL_PRGM);
		return;
	}

	val = rd_reg32(&r4tst->rtsdctl);
	val = (val & ~RTSDCTL_ENT_DLY_MASK) |
	      (ent_delay << RTSDCTL_ENT_DLY_SHIFT);
	wr_reg32(&r4tst->rtsdctl, val);
	/* min. freq. count, equal to 1/4 of the entropy sample length */
	wr_reg32(&r4tst->rtfrqmin, ent_delay >> 2);
	/* disable maximum frequency count */
	wr_reg32(&r4tst->rtfrqmax, RTFRQMAX_DISABLE);
	/* read the control register */
	val = rd_reg32(&r4tst->rtmctl);
	/*
	 * select raw sampling in both entropy shifter
	 * and statistical checker
	 */
	setbits32(&val, RTMCTL_SAMP_MODE_RAW_ES_SC);
	/* put RNG4 into run mode */
	clrbits32(&val, RTMCTL_PRGM);
	/* write back the control register */
	wr_reg32(&r4tst->rtmctl, val);
}

/**
 * caam_get_era() - Return the ERA of the SEC on SoC, based
 * on "sec-era" propery in the DTS. This property is updated by u-boot.
 **/
int caam_get_era(void)
{
	struct device_node *caam_node;
	for_each_compatible_node(caam_node, NULL, "fsl,sec-v4.0") {
		const uint32_t *prop = (uint32_t *)of_get_property(caam_node,
				"fsl,sec-era",
				NULL);
		return prop ? *prop : -ENOTSUPP;
	}

	return -ENOTSUPP;
}
EXPORT_SYMBOL(caam_get_era);

static int caam_rng_init(struct device *dev)
{
	int gen_sk, ent_delay = RTSDCTL_ENT_DLY_MIN;
	struct caam_drv_private *ctrlpriv = dev_get_drvdata(dev);
	u64 cha_vid_ls;
	struct caam_ctrl __iomem *ctrl;
	int ret = 0;

	ctrl = (struct caam_ctrl __iomem *)ctrlpriv->ctrl;
	cha_vid_ls = rd_reg32(&ctrl->perfmon.cha_id_ls);
	/*
	 * If SEC has RNG version >= 4 and RNG state handle has not been
	 * already instantiated, do RNG instantiation
	 */
	if ((cha_vid_ls & CHA_ID_LS_RNG_MASK) >> CHA_ID_LS_RNG_SHIFT >= 4) {
		ctrlpriv->rng4_sh_init =
			rd_reg32(&ctrl->r4tst[0].rdsta);
		/*
		 * If the secure keys (TDKEK, JDKEK, TDSK), were already
		 * generated, signal this to the function that is instantiating
		 * the state handles. An error would occur if RNG4 attempts
		 * to regenerate these keys before the next POR.
		 */
		gen_sk = ctrlpriv->rng4_sh_init & RDSTA_SKVN ? 0 : 1;
		ctrlpriv->rng4_sh_init &= RDSTA_IFMASK;
		do {
			int inst_handles =
				rd_reg32(&ctrl->r4tst[0].rdsta) &
								RDSTA_IFMASK;
			/*
			 * If either SH were instantiated by somebody else
			 * (e.g. u-boot) then it is assumed that the entropy
			 * parameters are properly set and thus the function
			 * setting these (kick_trng(...)) is skipped.
			 * Also, if a handle was instantiated, do not change
			 * the TRNG parameters.
			 */
			if (!(ctrlpriv->rng4_sh_init || inst_handles)) {
					dev_info(dev,
						 "Entropy delay = %u\n",
						 ent_delay);
				kick_trng(dev, ent_delay);
				ent_delay += 400;
			}
			/*
			 * if instantiate_rng(...) fails, the loop will rerun
			 * and the kick_trng(...) function will modfiy the
			 * upper and lower limits of the entropy sampling
			 * interval, leading to a sucessful initialization of
			 * the RNG.
			 */
			ret = instantiate_rng(dev, inst_handles,
					      gen_sk);
			if (ret == -EAGAIN)
				/*
				 * if here, the loop will rerun,
				 * so don't hog the CPU
				 */
				cpu_relax();
		} while ((ret == -EAGAIN) && (ent_delay < RTSDCTL_ENT_DLY_MAX));
		if (ret) {
			dev_err(dev, "failed to instantiate RNG");
			return ret;
		}
		/*
		 * Set handles init'ed by this module as the complement of the
		 * already initialized ones
		 */
		ctrlpriv->rng4_sh_init = ~ctrlpriv->rng4_sh_init & RDSTA_IFMASK;

		/* Enable RDB bit so that RNG works faster */
		setbits32(&ctrl->scfgr, SCFGR_RDBENABLE);
	}
	return 0;
}

/* Probe routine for CAAM top (controller) level */
static int caam_probe(struct platform_device *pdev)
{
	int ret, ring, rspec;
	u64 caam_id;
	u32 caam_id_ms;
	struct device *dev;
	struct device_node *nprop, *np;
	struct caam_ctrl __iomem *ctrl;
	struct caam_drv_private *ctrlpriv;
#ifdef CONFIG_DEBUG_FS
	struct caam_perfmon *perfmon;
#endif
	u32 scfgr, comp_params;
	int pg_size;
	int BLOCK_OFFSET = 0;

	ctrlpriv = kzalloc(sizeof(struct caam_drv_private), GFP_KERNEL);
	if (!ctrlpriv)
		return -ENOMEM;

	dev = &pdev->dev;
	dev_set_drvdata(dev, ctrlpriv);
	ctrlpriv->pdev = pdev;
	nprop = pdev->dev.of_node;

	/* Get configuration properties from device tree */
	/* First, get register page */
	ctrl = of_iomap(nprop, 0);
	if (ctrl == NULL) {
		dev_err(dev, "caam: of_iomap() failed\n");
		return -ENOMEM;
	}
	/* Finding the page size for using the CTPR_MS register */
	comp_params = rd_reg32(&ctrl->perfmon.comp_parms_ms);
	pg_size = (comp_params & CTPR_MS_PG_SZ_MASK) >> CTPR_MS_PG_SZ_SHIFT;

	/* Allocating the BLOCK_OFFSET based on the supported page size on
	 * the platform
	 */
	if (pg_size == 0)
		BLOCK_OFFSET = PG_SIZE_4K;
	else
		BLOCK_OFFSET = PG_SIZE_64K;

	ctrlpriv->ctrl = (struct caam_ctrl __force *)ctrl;
	ctrlpriv->assure = (struct caam_assurance __force *)
			   ((uint8_t *)ctrl +
			    BLOCK_OFFSET * ASSURE_BLOCK_NUMBER
			   );
	ctrlpriv->deco = (struct caam_deco __force *)
			 ((uint8_t *)ctrl +
			 BLOCK_OFFSET * DECO_BLOCK_NUMBER
			 );

	/* Get the IRQ of the controller (for security violations only) */
	ctrlpriv->secvio_irq = of_irq_to_resource(nprop, 0, NULL);

	/*
	 * Enable DECO watchdogs and, if this is a PHYS_ADDR_T_64BIT kernel,
	 * long pointers in master configuration register
	 */
	setbits32(&ctrl->mcr, MCFGR_WDENABLE |
		  (sizeof(dma_addr_t) == sizeof(u64) ? MCFGR_LONG_PTR : 0));

	/*
	 *  Read the Compile Time paramters and SCFGR to determine
	 * if Virtualization is enabled for this platform
	 */
	scfgr = rd_reg32(&ctrl->scfgr);

	ctrlpriv->virt_en = 0;
	if (comp_params & CTPR_MS_VIRT_EN_INCL) {
		/* VIRT_EN_INCL = 1 & VIRT_EN_POR = 1 or
		 * VIRT_EN_INCL = 1 & VIRT_EN_POR = 0 & SCFGR_VIRT_EN = 1
		 */
		if ((comp_params & CTPR_MS_VIRT_EN_POR) ||
		    (!(comp_params & CTPR_MS_VIRT_EN_POR) &&
		       (scfgr & SCFGR_VIRT_EN)))
				ctrlpriv->virt_en = 1;
	} else {
		/* VIRT_EN_INCL = 0 && VIRT_EN_POR_VALUE = 1 */
		if (comp_params & CTPR_MS_VIRT_EN_POR)
				ctrlpriv->virt_en = 1;
	}

	if (ctrlpriv->virt_en == 1)
		setbits32(&ctrl->jrstart, JRSTART_JR0_START |
			  JRSTART_JR1_START | JRSTART_JR2_START |
			  JRSTART_JR3_START);

	if (sizeof(dma_addr_t) == sizeof(u64))
		if (of_device_is_compatible(nprop, "fsl,sec-v5.0"))
			dma_set_mask(dev, DMA_BIT_MASK(40));
		else
			dma_set_mask(dev, DMA_BIT_MASK(36));
	else
		dma_set_mask(dev, DMA_BIT_MASK(32));

	/*
	 * Detect and enable JobRs
	 * First, find out how many ring spec'ed, allocate references
	 * for all, then go probe each one.
	 */
	rspec = 0;
	for_each_compatible_node(np, NULL, "fsl,sec-v4.0-job-ring")
		rspec++;
	if (!rspec) {
		/* for backward compatible with device trees */
		for_each_compatible_node(np, NULL, "fsl,sec4.0-job-ring")
			rspec++;
	}

	ctrlpriv->jrpdev = kzalloc(sizeof(struct platform_device *) * rspec,
								GFP_KERNEL);
	if (ctrlpriv->jrpdev == NULL) {
		iounmap(&ctrl);
		return -ENOMEM;
	}

	ring = 0;
	ctrlpriv->total_jobrs = 0;
	for_each_compatible_node(np, NULL, "fsl,sec-v4.0-job-ring") {
		ctrlpriv->jrpdev[ring] =
				of_platform_device_create(np, NULL, dev);
		if (!ctrlpriv->jrpdev[ring]) {
			pr_warn("JR%d Platform device creation error\n", ring);
			continue;
		}
		ctrlpriv->jr[ring] = (struct caam_job_ring __force *)
				     ((uint8_t *)ctrl +
				     (ring + JR_BLOCK_NUMBER) *
				      BLOCK_OFFSET
				     );
		ctrlpriv->total_jobrs++;
		ring++;
	}
	if (!ring) {
		for_each_compatible_node(np, NULL, "fsl,sec4.0-job-ring") {
			ctrlpriv->jrpdev[ring] =
				of_platform_device_create(np, NULL, dev);
			if (!ctrlpriv->jrpdev[ring]) {
				pr_warn("JR%d Platform device creation error\n",
					ring);
				continue;
			}
			ctrlpriv->total_jobrs++;
			ring++;
		}
	}

	/* Check to see if QI present. If so, enable */
	ctrlpriv->qi_present =
			!!(rd_reg32(&ctrl->perfmon.comp_parms_ms) &
			   CTPR_MS_QI_MASK);
	if (ctrlpriv->qi_present) {
		ctrlpriv->qi = (struct caam_queue_if __force *)
			       ((uint8_t *)ctrl +
				BLOCK_OFFSET * QI_BLOCK_NUMBER
			       );
		/* This is all that's required to physically enable QI */
		wr_reg32(&ctrlpriv->qi->qi_control_lo, QICTL_DQEN);

		/* If QMAN driver is present, init CAAM-QI backend */
#ifdef CONFIG_CAAM_QI
		if (caam_qi_init(pdev, nprop))
			dev_err(dev, "caam qi i/f init failed\n");
#endif
	}

	/* If no QI and no rings specified, quit and go home */
	if ((!ctrlpriv->qi_present) && (!ctrlpriv->total_jobrs)) {
		dev_err(dev, "no queues configured, terminating\n");
		caam_remove(pdev);
		return -ENOMEM;
	}

	ret = caam_rng_init(dev);
	if (ret) {
		caam_remove(pdev);
		return ret;
	}

	/* NOTE: RTIC detection ought to go here, around Si time */

	ctrlpriv->era = caam_get_era();

	caam_id_ms = rd_reg32(&ctrl->perfmon.caam_id_ms);
	if (caam_id_ms == SEC_ID_MS_T1040)
		ctrlpriv->errata |= SEC_ERRATUM_A_006899;

	caam_id = (u64)caam_id_ms << 32 |
		  (u64)rd_reg32(&ctrl->perfmon.caam_id_ls);

	/* Report "alive" for developer to see */
	dev_info(dev, "device ID = 0x%016llx (Era %d)\n", caam_id,
		 ctrlpriv->era);
	dev_info(dev, "job rings = %d, qi = %d\n",
		 ctrlpriv->total_jobrs, ctrlpriv->qi_present);

#ifdef CONFIG_DEBUG_FS
	/*
	 * FIXME: needs better naming distinction, as some amalgamation of
	 * "caam" and nprop->full_name. The OF name isn't distinctive,
	 * but does separate instances
	 */
	perfmon = (struct caam_perfmon __force *)&ctrl->perfmon;

	ctrlpriv->dfs_root = debugfs_create_dir("caam", NULL);
	ctrlpriv->ctl = debugfs_create_dir("ctl", ctrlpriv->dfs_root);

	/* Controller-level - performance monitor counters */
	ctrlpriv->ctl_rq_dequeued =
		debugfs_create_u64("rq_dequeued",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   ctrlpriv->ctl, &perfmon->req_dequeued);
	ctrlpriv->ctl_ob_enc_req =
		debugfs_create_u64("ob_rq_encrypted",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   ctrlpriv->ctl, &perfmon->ob_enc_req);
	ctrlpriv->ctl_ib_dec_req =
		debugfs_create_u64("ib_rq_decrypted",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   ctrlpriv->ctl, &perfmon->ib_dec_req);
	ctrlpriv->ctl_ob_enc_bytes =
		debugfs_create_u64("ob_bytes_encrypted",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   ctrlpriv->ctl, &perfmon->ob_enc_bytes);
	ctrlpriv->ctl_ob_prot_bytes =
		debugfs_create_u64("ob_bytes_protected",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   ctrlpriv->ctl, &perfmon->ob_prot_bytes);
	ctrlpriv->ctl_ib_dec_bytes =
		debugfs_create_u64("ib_bytes_decrypted",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   ctrlpriv->ctl, &perfmon->ib_dec_bytes);
	ctrlpriv->ctl_ib_valid_bytes =
		debugfs_create_u64("ib_bytes_validated",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   ctrlpriv->ctl, &perfmon->ib_valid_bytes);

	/* Controller level - global status values */
	ctrlpriv->ctl_faultaddr =
		debugfs_create_u64("fault_addr",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   ctrlpriv->ctl, &perfmon->faultaddr);
	ctrlpriv->ctl_faultdetail =
		debugfs_create_u32("fault_detail",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   ctrlpriv->ctl, &perfmon->faultdetail);
	ctrlpriv->ctl_faultstatus =
		debugfs_create_u32("fault_status",
				   S_IRUSR | S_IRGRP | S_IROTH,
				   ctrlpriv->ctl, &perfmon->status);

	/* Internal covering keys (useful in non-secure mode only) */
	ctrlpriv->ctl_kek_wrap.data = &ctrlpriv->ctrl->kek[0];
	ctrlpriv->ctl_kek_wrap.size = KEK_KEY_SIZE * sizeof(u32);
	ctrlpriv->ctl_kek = debugfs_create_blob("kek",
						S_IRUSR |
						S_IRGRP | S_IROTH,
						ctrlpriv->ctl,
						&ctrlpriv->ctl_kek_wrap);

	ctrlpriv->ctl_tkek_wrap.data = &ctrlpriv->ctrl->tkek[0];
	ctrlpriv->ctl_tkek_wrap.size = KEK_KEY_SIZE * sizeof(u32);
	ctrlpriv->ctl_tkek = debugfs_create_blob("tkek",
						 S_IRUSR |
						 S_IRGRP | S_IROTH,
						 ctrlpriv->ctl,
						 &ctrlpriv->ctl_tkek_wrap);

	ctrlpriv->ctl_tdsk_wrap.data = &ctrlpriv->ctrl->tdsk[0];
	ctrlpriv->ctl_tdsk_wrap.size = KEK_KEY_SIZE * sizeof(u32);
	ctrlpriv->ctl_tdsk = debugfs_create_blob("tdsk",
						 S_IRUSR |
						 S_IRGRP | S_IROTH,
						 ctrlpriv->ctl,
						 &ctrlpriv->ctl_tdsk_wrap);
#endif
	return 0;
}

#ifdef CONFIG_PM
static int caam_stop_qi(struct caam_queue_if __iomem *qi)
{
	int qi_stopped, loop = 0;

	setbits32(&qi->qi_control_lo, QICTL_STOP);

	/*
	 * Wait till QI Job's in Holding tank/deco are completed.
	 * No dequeue from QI will be happen till QI interface is
	 * reenabled.
	 */
	while (loop <= 100000) {
		qi_stopped = rd_reg32(&qi->qi_status) &
			 QISTA_STOPD;
		if (qi_stopped) {
			wr_reg32(&qi->qi_control_lo,
				 QICTL_STOP);
			return 0;
		}
		loop++;
	}

	/* Failed to stop QI interface. Reenable QI Interface */
	wr_reg32(&qi->qi_control_lo, QICTL_DQEN);
	return -EBUSY;
}

/* Suspend handler for caam device */
static int caam_suspend(struct device *dev)
{
	struct caam_drv_private *caam_priv;
	struct caam_ctrl __iomem *ctrl;
	struct caam_queue_if __iomem *qi;
	int ret = 0;

	caam_priv = dev_get_drvdata(dev);
	ctrl = caam_priv->ctrl;
	qi = caam_priv->qi;

	/* QI Interface graceful stoppping during suspend */
	if (caam_priv->qi_present) {
		int qi_dqen;

		qi_dqen = rd_reg32(&qi->qi_control_lo) &
			QICTL_DQEN;
		if (qi_dqen)
			ret = caam_stop_qi(qi);
	}

	return ret;
}

/* Resume handler for caam device */
static int caam_resume(struct device *dev)
{
	struct caam_drv_private *caam_priv;
	struct caam_ctrl __iomem *ctrl;
	struct caam_queue_if __iomem *qi;
	int ret;

	caam_priv = dev_get_drvdata(dev);
	ctrl = caam_priv->ctrl;
	qi = caam_priv->qi;
	/*
	 * Enable DECO watchdogs and, if this is a PHYS_ADDR_T_64BIT kernel,
	 * long pointers in master configuration register
	 */
	setbits32(&ctrl->mcr, MCFGR_WDENABLE |
		  (sizeof(dma_addr_t) == sizeof(u64) ? MCFGR_LONG_PTR : 0));

	/* Enable QI interface of SEC */
	if (caam_priv->qi_present)
		wr_reg32(&qi->qi_control_lo, QICTL_DQEN);

	ret = caam_rng_init(dev);

	return ret;
}

const struct dev_pm_ops caam_pm_ops = {
	.suspend = caam_suspend,
	.resume = caam_resume,
};
#endif /* CONFIG_PM */

static struct of_device_id caam_match[] = {
	{
		.compatible = "fsl,sec-v4.0",
	},
	{
		.compatible = "fsl,sec4.0",
	},
	{},
};
MODULE_DEVICE_TABLE(of, caam_match);

static struct platform_driver caam_driver = {
	.driver = {
		.name = "caam",
		.owner = THIS_MODULE,
		.of_match_table = caam_match,
#ifdef CONFIG_PM
		.pm = &caam_pm_ops,
#endif
	},
	.probe       = caam_probe,
	.remove      = caam_remove,
};

module_platform_driver(caam_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FSL CAAM request backend");
MODULE_AUTHOR("Freescale Semiconductor - NMG/STC");
