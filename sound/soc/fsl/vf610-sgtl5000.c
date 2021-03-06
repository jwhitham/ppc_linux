/*
 * Freescale ALSA SoC Audio using SGTL5000 as codec.
 *
 * Copyright 2012-2014 Freescale Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 */

#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>

#include "../codecs/sgtl5000.h"
#include "fsl_sai.h"

static unsigned int sysclk_rate;

static int vf610_sgtl5000_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret;
	struct device *dev = rtd->card->dev;

	ret = snd_soc_dai_set_sysclk(rtd->codec_dai, SGTL5000_SYSCLK,
				     sysclk_rate, SND_SOC_CLOCK_IN);
	if (ret) {
		dev_err(dev, "could not set codec driver clock params :%d\n",
				ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(rtd->cpu_dai, FSL_SAI_CLK_BUS,
				     sysclk_rate, SND_SOC_CLOCK_OUT);
	if (ret) {
		dev_err(dev, "could not set cpu dai driver clock params :%d\n",
				ret);
		return ret;
	}

	return 0;
}

static struct snd_soc_dai_link vf610_sgtl5000_dai = {
	.name = "HiFi",
	.stream_name = "HiFi",
	.codec_dai_name = "sgtl5000",
	.init = &vf610_sgtl5000_dai_init,
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBM_CFM,
};

static const struct snd_soc_dapm_widget vf610_sgtl5000_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("Microphone Jack", NULL),
	SND_SOC_DAPM_LINE("Line In Jack", NULL),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_SPK("Speaker Ext", NULL),
};

static struct snd_soc_card vf610_sgtl5000_card = {
	.owner = THIS_MODULE,
	.num_links = 1,
	.dai_link = &vf610_sgtl5000_dai,
	.dapm_widgets = vf610_sgtl5000_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(vf610_sgtl5000_dapm_widgets),
};

static int vf610_sgtl5000_parse_dt(struct platform_device *pdev)
{
	int ret;
	struct device_node *sai_np, *codec_np;
	struct clk *codec_clk;
	struct i2c_client *codec_dev;
	struct device_node *np = pdev->dev.of_node;

	ret = snd_soc_of_parse_card_name(&vf610_sgtl5000_card,
					 "simple-audio-card,name");
	if (ret)
		return ret;

	ret = snd_soc_of_parse_audio_routing(&vf610_sgtl5000_card,
					     "simple-audio-card,routing");
	if (ret)
		return ret;

	sai_np = of_parse_phandle(np, "simple-audio-card,cpu", 0);
	if (!sai_np) {
		dev_err(&pdev->dev, "parsing \"saif-controller\" error\n");
		return -EINVAL;
	}
	vf610_sgtl5000_dai.cpu_of_node = sai_np;
	vf610_sgtl5000_dai.platform_of_node = sai_np;

	codec_np = of_parse_phandle(np, "simple-audio-card,codec", 0);
	if (!codec_np) {
		dev_err(&pdev->dev, "parsing \"audio-codec\" error\n");
		ret = -EINVAL;
		goto sai_np_fail;
	}
	vf610_sgtl5000_dai.codec_of_node = codec_np;

	codec_dev = of_find_i2c_device_by_node(codec_np);
	if (!codec_dev) {
		dev_err(&pdev->dev, "failed to find codec platform device\n");
		ret = PTR_ERR(codec_dev);
		goto codec_np_fail;
	}

	codec_clk = devm_clk_get(&codec_dev->dev, NULL);
	if (IS_ERR(codec_clk)) {
		dev_err(&pdev->dev, "failed to get codec clock\n");
		ret = PTR_ERR(codec_clk);
		goto codec_np_fail;
	}

	sysclk_rate = clk_get_rate(codec_clk);

codec_np_fail:
	of_node_put(codec_np);
sai_np_fail:
	of_node_put(sai_np);

	return ret;
}

static int vf610_sgtl5000_probe(struct platform_device *pdev)
{
	int ret;

	vf610_sgtl5000_card.dev = &pdev->dev;

	ret = vf610_sgtl5000_parse_dt(pdev);
	if (ret) {
		dev_err(&pdev->dev, "parse sgtl5000 device tree failed :%d\n",
				ret);
		return ret;
	}

	ret = snd_soc_register_card(&vf610_sgtl5000_card);
	if (ret) {
		dev_err(&pdev->dev, "TWR-AUDIO-SGTL board required :%d\n",
				ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id vf610_sgtl5000_dt_ids[] = {
	{ .compatible = "fsl,vf610-sgtl5000", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, vf610_sgtl5000_dt_ids);

static struct platform_driver vf610_sgtl5000_driver = {
	.probe = vf610_sgtl5000_probe,
	.driver = {
		.name = "vf610-sgtl5000",
		.owner = THIS_MODULE,
		.of_match_table = vf610_sgtl5000_dt_ids,
	},
};
module_platform_driver(vf610_sgtl5000_driver);

MODULE_DESCRIPTION("Freescale SGTL5000 ASoC driver");
MODULE_LICENSE("GPL");
