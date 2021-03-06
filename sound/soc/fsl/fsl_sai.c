/*
 * Freescale ALSA SoC Digital Audio Interface (SAI) driver.
 *
 * Copyright 2012-2013 Freescale Semiconductor, Inc.
 *
 * This program is free software, you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 2 of the License, or(at your
 * option) any later version.
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>

#include "fsl_sai.h"

static int fsl_sai_set_dai_sysclk_tr(struct snd_soc_dai *cpu_dai,
		int clk_id, unsigned int freq, int fsl_dir)
{
	struct fsl_sai *sai = snd_soc_dai_get_drvdata(cpu_dai);
	u32 val_cr2, reg_cr2;

	if (fsl_dir == FSL_FMT_TRANSMITTER)
		reg_cr2 = FSL_SAI_TCR2;
	else
		reg_cr2 = FSL_SAI_RCR2;

	regmap_read(sai->regmap, reg_cr2, &val_cr2);

	val_cr2 &= ~FSL_SAI_CR2_MSEL_MASK;

	switch (clk_id) {
	case FSL_SAI_CLK_BUS:
		val_cr2 |= FSL_SAI_CR2_MSEL_BUS;
		break;
	case FSL_SAI_CLK_MAST1:
		val_cr2 |= FSL_SAI_CR2_MSEL_MCLK1;
		break;
	case FSL_SAI_CLK_MAST2:
		val_cr2 |= FSL_SAI_CR2_MSEL_MCLK2;
		break;
	case FSL_SAI_CLK_MAST3:
		val_cr2 |= FSL_SAI_CR2_MSEL_MCLK3;
		break;
	default:
		return -EINVAL;
	}

	regmap_write(sai->regmap, reg_cr2, val_cr2);

	return 0;
}

static int fsl_sai_set_dai_sysclk(struct snd_soc_dai *cpu_dai,
		int clk_id, unsigned int freq, int dir)
{
	int ret;

	if (dir == SND_SOC_CLOCK_IN)
		return 0;

	ret = fsl_sai_set_dai_sysclk_tr(cpu_dai, clk_id, freq,
					FSL_FMT_TRANSMITTER);
	if (ret) {
		dev_err(cpu_dai->dev, "Cannot set tx sysclk: %d\n", ret);
		return ret;
	}

	ret = fsl_sai_set_dai_sysclk_tr(cpu_dai, clk_id, freq,
					FSL_FMT_RECEIVER);
	if (ret)
		dev_err(cpu_dai->dev, "Cannot set rx sysclk: %d\n", ret);

	return ret;
}

static int fsl_sai_set_dai_fmt_tr(struct snd_soc_dai *cpu_dai,
				unsigned int fmt, int fsl_dir)
{
	struct fsl_sai *sai = snd_soc_dai_get_drvdata(cpu_dai);
	u32 val_cr2, val_cr4, reg_cr2, reg_cr4;

	if (fsl_dir == FSL_FMT_TRANSMITTER) {
		reg_cr2 = FSL_SAI_TCR2;
		reg_cr4 = FSL_SAI_TCR4;
	} else {
		reg_cr2 = FSL_SAI_RCR2;
		reg_cr4 = FSL_SAI_RCR4;
	}

	regmap_read(sai->regmap, reg_cr2, &val_cr2);
	regmap_read(sai->regmap, reg_cr4, &val_cr4);

	if (sai->big_endian_data)
		val_cr4 &= ~FSL_SAI_CR4_MF;
	else
		val_cr4 |= FSL_SAI_CR4_MF;

	/* DAI mode */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		/*
		 * Frame low, 1clk before data, one word length for frame sync,
		 * frame sync starts one serial clock cycle earlier,
		 * that is, together with the last bit of the previous
		 * data word.
		 */
		val_cr2 &= ~FSL_SAI_CR2_BCP;
		val_cr4 |= FSL_SAI_CR4_FSE | FSL_SAI_CR4_FSP;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		/*
		 * Frame high, one word length for frame sync,
		 * frame sync asserts with the first bit of the frame.
		 */
		val_cr2 &= ~FSL_SAI_CR2_BCP;
		val_cr4 &= ~(FSL_SAI_CR4_FSE | FSL_SAI_CR4_FSP);
		break;
	case SND_SOC_DAIFMT_DSP_A:
		/*
		 * Frame high, 1clk before data, one bit for frame sync,
		 * frame sync starts one serial clock cycle earlier,
		 * that is, together with the last bit of the previous
		 * data word.
		 */
		val_cr2 &= ~FSL_SAI_CR2_BCP;
		val_cr4 &= ~FSL_SAI_CR4_FSP;
		val_cr4 |= FSL_SAI_CR4_FSE;
		sai->is_dsp_mode = true;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		/*
		 * Frame high, one bit for frame sync,
		 * frame sync asserts with the first bit of the frame.
		 */
		val_cr2 &= ~FSL_SAI_CR2_BCP;
		val_cr4 &= ~(FSL_SAI_CR4_FSE | FSL_SAI_CR4_FSP);
		sai->is_dsp_mode = true;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		/* To be done */
	default:
		return -EINVAL;
	}

	/* DAI clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_IB_IF:
		/* Invert both clocks */
		val_cr2 ^= FSL_SAI_CR2_BCP;
		val_cr4 ^= FSL_SAI_CR4_FSP;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		/* Invert bit clock */
		val_cr2 ^= FSL_SAI_CR2_BCP;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		/* Invert frame clock */
		val_cr4 ^= FSL_SAI_CR4_FSP;
		break;
	case SND_SOC_DAIFMT_NB_NF:
		/* Nothing to do for both normal cases */
		break;
	default:
		return -EINVAL;
	}

	/* DAI clock master masks */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		val_cr2 |= FSL_SAI_CR2_BCD_MSTR;
		val_cr4 |= FSL_SAI_CR4_FSD_MSTR;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		val_cr2 &= ~FSL_SAI_CR2_BCD_MSTR;
		val_cr4 &= ~FSL_SAI_CR4_FSD_MSTR;
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		val_cr2 |= FSL_SAI_CR2_BCD_MSTR;
		val_cr4 &= ~FSL_SAI_CR4_FSD_MSTR;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		val_cr2 &= ~FSL_SAI_CR2_BCD_MSTR;
		val_cr4 |= FSL_SAI_CR4_FSD_MSTR;
		break;
	default:
		return -EINVAL;
	}

	regmap_write(sai->regmap, reg_cr2, val_cr2);
	regmap_write(sai->regmap, reg_cr4, val_cr4);

	return 0;
}

static int fsl_sai_set_dai_fmt(struct snd_soc_dai *cpu_dai, unsigned int fmt)
{
	int ret;

	ret = fsl_sai_set_dai_fmt_tr(cpu_dai, fmt, FSL_FMT_TRANSMITTER);
	if (ret) {
		dev_err(cpu_dai->dev, "Cannot set tx format: %d\n", ret);
		return ret;
	}

	ret = fsl_sai_set_dai_fmt_tr(cpu_dai, fmt, FSL_FMT_RECEIVER);
	if (ret)
		dev_err(cpu_dai->dev, "Cannot set rx format: %d\n", ret);

	return ret;
}

static int fsl_sai_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *cpu_dai)
{
	struct fsl_sai *sai = snd_soc_dai_get_drvdata(cpu_dai);
	u32 val_cr4, val_cr5, val_mr, reg_cr4, reg_cr5, reg_mr;
	unsigned int channels = params_channels(params);
	u32 word_width = snd_pcm_format_width(params_format(params));

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		reg_cr4 = FSL_SAI_TCR4;
		reg_cr5 = FSL_SAI_TCR5;
		reg_mr = FSL_SAI_TMR;
	} else {
		reg_cr4 = FSL_SAI_RCR4;
		reg_cr5 = FSL_SAI_RCR5;
		reg_mr = FSL_SAI_RMR;
	}

	regmap_read(sai->regmap, reg_cr4, &val_cr4);
	regmap_read(sai->regmap, reg_cr4, &val_cr5);

	val_cr4 &= ~FSL_SAI_CR4_SYWD_MASK;
	val_cr4 &= ~FSL_SAI_CR4_FRSZ_MASK;

	val_cr5 &= ~FSL_SAI_CR5_WNW_MASK;
	val_cr5 &= ~FSL_SAI_CR5_W0W_MASK;
	val_cr5 &= ~FSL_SAI_CR5_FBT_MASK;

	if (!sai->is_dsp_mode)
		val_cr4 |= FSL_SAI_CR4_SYWD(word_width);

	val_cr5 |= FSL_SAI_CR5_WNW(word_width);
	val_cr5 |= FSL_SAI_CR5_W0W(word_width);

	val_cr5 &= ~FSL_SAI_CR5_FBT_MASK;
	if (sai->big_endian_data)
		val_cr5 |= FSL_SAI_CR5_FBT(0);
	else
		val_cr5 |= FSL_SAI_CR5_FBT(word_width - 1);

	val_cr4 |= FSL_SAI_CR4_FRSZ(channels);
	val_mr = ~0UL - ((1 << channels) - 1);

	regmap_write(sai->regmap, reg_cr4, val_cr4);
	regmap_write(sai->regmap, reg_cr5, val_cr5);
	regmap_write(sai->regmap, reg_mr, val_mr);

	return 0;
}

static int fsl_sai_trigger(struct snd_pcm_substream *substream, int cmd,
		struct snd_soc_dai *cpu_dai)
{
	struct fsl_sai *sai = snd_soc_dai_get_drvdata(cpu_dai);
	u32 tcsr, rcsr;

	/*
	 * The transmitter bit clock and frame sync are to be
	 * used by both the transmitter and receiver.
	 */
	regmap_update_bits(sai->regmap, FSL_SAI_TCR2, FSL_SAI_CR2_SYNC,
			   ~FSL_SAI_CR2_SYNC);
	regmap_update_bits(sai->regmap, FSL_SAI_RCR2, FSL_SAI_CR2_SYNC,
			   FSL_SAI_CR2_SYNC);

	regmap_read(sai->regmap, FSL_SAI_TCSR, &tcsr);
	regmap_read(sai->regmap, FSL_SAI_RCSR, &rcsr);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		tcsr |= FSL_SAI_CSR_FRDE;
		rcsr &= ~FSL_SAI_CSR_FRDE;
	} else {
		rcsr |= FSL_SAI_CSR_FRDE;
		tcsr &= ~FSL_SAI_CSR_FRDE;
	}

	/*
	 * It is recommended that the transmitter is the last enabled
	 * and the first disabled.
	 */
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		tcsr |= FSL_SAI_CSR_TERE;
		rcsr |= FSL_SAI_CSR_TERE;

		regmap_write(sai->regmap, FSL_SAI_RCSR, rcsr);
		regmap_write(sai->regmap, FSL_SAI_TCSR, tcsr);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (!(cpu_dai->playback_active || cpu_dai->capture_active)) {
			tcsr &= ~FSL_SAI_CSR_TERE;
			rcsr &= ~FSL_SAI_CSR_TERE;
		}

		regmap_write(sai->regmap, FSL_SAI_TCSR, tcsr);
		regmap_write(sai->regmap, FSL_SAI_RCSR, rcsr);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int fsl_sai_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *cpu_dai)
{
	struct fsl_sai *sai = snd_soc_dai_get_drvdata(cpu_dai);
	u32 reg;

	if (sai->big_endian_regs)
		substream->data_swapped = 1;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		reg = FSL_SAI_TCR3;
	else
		reg = FSL_SAI_RCR3;

	regmap_update_bits(sai->regmap, reg, FSL_SAI_CR3_TRCE,
			   FSL_SAI_CR3_TRCE);

	return 0;
}

static void fsl_sai_shutdown(struct snd_pcm_substream *substream,
		struct snd_soc_dai *cpu_dai)
{
	struct fsl_sai *sai = snd_soc_dai_get_drvdata(cpu_dai);
	u32 reg;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		reg = FSL_SAI_TCR3;
	else
		reg = FSL_SAI_RCR3;

	regmap_update_bits(sai->regmap, reg, FSL_SAI_CR3_TRCE,
			   ~FSL_SAI_CR3_TRCE);
}

static const struct snd_soc_dai_ops fsl_sai_pcm_dai_ops = {
	.set_sysclk	= fsl_sai_set_dai_sysclk,
	.set_fmt	= fsl_sai_set_dai_fmt,
	.hw_params	= fsl_sai_hw_params,
	.trigger	= fsl_sai_trigger,
	.startup	= fsl_sai_startup,
	.shutdown	= fsl_sai_shutdown,
};

static int fsl_sai_dai_probe(struct snd_soc_dai *cpu_dai)
{
	struct fsl_sai *sai = dev_get_drvdata(cpu_dai->dev);

	regmap_update_bits(sai->regmap, FSL_SAI_TCSR, 0xffffffff, 0x0);
	regmap_update_bits(sai->regmap, FSL_SAI_RCSR, 0xffffffff, 0x0);
	regmap_update_bits(sai->regmap, FSL_SAI_TCR1, FSL_SAI_CR1_RFW_MASK,
			   FSL_SAI_MAXBURST_TX * 2);
	regmap_update_bits(sai->regmap, FSL_SAI_RCR1, FSL_SAI_CR1_RFW_MASK,
			   FSL_SAI_MAXBURST_RX - 1);

	cpu_dai->playback_dma_data = &sai->dma_params_tx;
	cpu_dai->capture_dma_data = &sai->dma_params_rx;

	snd_soc_dai_set_drvdata(cpu_dai, sai);

	return 0;
}

static struct snd_soc_dai_driver fsl_sai_dai = {
	.probe = fsl_sai_dai_probe,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = FSL_SAI_FORMATS,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = FSL_SAI_FORMATS,
	},
	.ops = &fsl_sai_pcm_dai_ops,
};

static const struct snd_soc_component_driver fsl_component = {
	.name           = "fsl-sai",
};

static bool fsl_sai_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case FSL_SAI_TCSR:
	case FSL_SAI_TCR1:
	case FSL_SAI_TCR2:
	case FSL_SAI_TCR3:
	case FSL_SAI_TCR4:
	case FSL_SAI_TCR5:
	case FSL_SAI_TFR:
	case FSL_SAI_TMR:
	case FSL_SAI_RCSR:
	case FSL_SAI_RCR1:
	case FSL_SAI_RCR2:
	case FSL_SAI_RCR3:
	case FSL_SAI_RCR4:
	case FSL_SAI_RCR5:
	case FSL_SAI_RDR:
	case FSL_SAI_RFR:
	case FSL_SAI_RMR:
		return true;
	default:
		return false;
	}
}

static bool fsl_sai_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case FSL_SAI_TCSR:
	case FSL_SAI_TFR:
	case FSL_SAI_TDR:
	case FSL_SAI_RCSR:
	case FSL_SAI_RFR:
	case FSL_SAI_RDR:
		return true;
	default:
		return false;
	}

}

static bool fsl_sai_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case FSL_SAI_TCSR:
	case FSL_SAI_TCR1:
	case FSL_SAI_TCR2:
	case FSL_SAI_TCR3:
	case FSL_SAI_TCR4:
	case FSL_SAI_TCR5:
	case FSL_SAI_TDR:
	case FSL_SAI_TMR:
	case FSL_SAI_RCSR:
	case FSL_SAI_RCR1:
	case FSL_SAI_RCR2:
	case FSL_SAI_RCR3:
	case FSL_SAI_RCR4:
	case FSL_SAI_RCR5:
	case FSL_SAI_RMR:
		return true;
	default:
		return false;
	}
}

static struct regmap_config fsl_sai_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,

	.max_register = FSL_SAI_RMR,
	.readable_reg = fsl_sai_readable_reg,
	.volatile_reg = fsl_sai_volatile_reg,
	.writeable_reg = fsl_sai_writeable_reg,
	.cache_type = REGCACHE_RBTREE,
};

static bool fsl_sai_filter(struct dma_chan *chan, void *param)
{
	struct snd_dmaengine_dai_dma_data *dma_data = param;

	chan->private = dma_data->filter_data;

	return true;
}

static const struct snd_pcm_hardware fsl_sai_pcm_hardware = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_PAUSE |
		SNDRV_PCM_INFO_RESUME,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rate_min = 8000,
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = FSL_SAI_DMABUF_SIZE,
	.period_bytes_min = 128,
	.period_bytes_max = 65535,
	.periods_min = 2,
	.periods_max = 255,
	.fifo_size = 0,
};

static const struct snd_dmaengine_pcm_config fsl_sai_dmaengine_pcm_config = {
	.pcm_hardware = &fsl_sai_pcm_hardware,
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
	.compat_filter_fn = fsl_sai_filter,
	.prealloc_buffer_size = FSL_SAI_DMABUF_SIZE,
};

static int fsl_sai_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct fsl_sai *sai;
	struct resource *res;
	void __iomem *base;
	int ret;

	sai = devm_kzalloc(&pdev->dev, sizeof(*sai), GFP_KERNEL);
	if (!sai)
		return -ENOMEM;

	sai->big_endian_regs = of_property_read_bool(np, "big-endian-regs");
	if (sai->big_endian_regs)
		fsl_sai_regmap_config.val_format_endian = REGMAP_ENDIAN_BIG;

	sai->big_endian_data = of_property_read_bool(np, "big-endian-data");

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	sai->regmap = devm_regmap_init_mmio_clk(&pdev->dev,
			"sai", base, &fsl_sai_regmap_config);
	if (IS_ERR(sai->regmap)) {
		dev_err(&pdev->dev, "regmap init failed\n");
		return PTR_ERR(sai->regmap);
	}

	if (sai->big_endian_regs) {
		sai->dma_params_rx.addr = res->start + FSL_SAI_RDR + 2;
		sai->dma_params_tx.addr = res->start + FSL_SAI_TDR + 2;
	} else {
		sai->dma_params_rx.addr = res->start + FSL_SAI_RDR;
		sai->dma_params_tx.addr = res->start + FSL_SAI_TDR;
	}

	sai->dma_params_rx.maxburst = FSL_SAI_MAXBURST_RX;
	sai->dma_params_tx.maxburst = FSL_SAI_MAXBURST_TX;

	platform_set_drvdata(pdev, sai);

	ret = snd_soc_register_component(&pdev->dev, &fsl_component,
					 &fsl_sai_dai, 1);
	if (ret)
		return ret;

	ret = snd_dmaengine_pcm_register(&pdev->dev,
			&fsl_sai_dmaengine_pcm_config,
			SND_DMAENGINE_PCM_FLAG_NO_RESIDUE);
	if (ret)
		snd_soc_unregister_component(&pdev->dev);

	return ret;
}

static int fsl_sai_remove(struct platform_device *pdev)
{
	snd_dmaengine_pcm_unregister(&pdev->dev);
	snd_soc_unregister_component(&pdev->dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int fsl_sai_suspend(struct device *dev)
{
	struct fsl_sai *sai = dev_get_drvdata(dev);

	regcache_cache_only(sai->regmap, true);
	regcache_mark_dirty(sai->regmap);

	return 0;
}

static int fsl_sai_resume(struct device *dev)
{
	struct fsl_sai *sai = dev_get_drvdata(dev);

	/* Restore all registers */
	regcache_cache_only(sai->regmap, false);
	regcache_sync(sai->regmap);

	return 0;
};
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops fsl_sai_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(fsl_sai_suspend, fsl_sai_resume)
};

static const struct of_device_id fsl_sai_ids[] = {
	{ .compatible = "fsl,vf610-sai", },
	{ /* sentinel */ }
};

static struct platform_driver fsl_sai_driver = {
	.probe = fsl_sai_probe,
	.remove = fsl_sai_remove,
	.driver = {
		.name = "fsl-sai",
		.owner = THIS_MODULE,
		.of_match_table = fsl_sai_ids,
		.pm = &fsl_sai_pm,
	},
};
module_platform_driver(fsl_sai_driver);

MODULE_DESCRIPTION("Freescale Soc SAI Interface");
MODULE_AUTHOR("Xiubo Li, <Li.Xiubo@freescale.com>");
MODULE_ALIAS("platform:fsl-sai");
MODULE_LICENSE("GPL");
