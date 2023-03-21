/*
 * pcm5102a.c
 *
 * (C) Copyright 2017-2018
 * Seeed Technology Co., Ltd. <www.seeedstudio.com>
 *
 * (C) Copyright 2023
 * Ryoma.Tanase <gmail.com>
 *
 * pcm5102a codec driver used with X-Powers AC108
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

/* #undef PCM5102A_DBG
 * use 'make DEBUG=1' to enable debugging
 */
#include <linux/module.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/workqueue.h>
#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/regmap.h>
#include <linux/input.h>
#include <linux/delay.h>
#include "ac108.h"
#include "ac10x.h"


struct pll_div {
	unsigned int freq_in;
	unsigned int freq_out;
	unsigned int m1;
	unsigned int m2;
	unsigned int n;
	unsigned int k1;
	unsigned int k2;
};

struct real_val_to_reg_val {
	unsigned int real_val;
	unsigned int reg_val;
};

static const struct real_val_to_reg_val ac108_sample_rate[] = {
	{ 8000,  0 },
	{ 11025, 1 },
	{ 12000, 2 },
	{ 16000, 3 },
	{ 22050, 4 },
	{ 24000, 5 },
	{ 32000, 6 },
	{ 44100, 7 },
	{ 48000, 8 },
	{ 96000, 9 },
};

/* Sample resolution */
static const struct real_val_to_reg_val ac108_samp_res[] = {
	{ 8,  1 },
	{ 12, 2 },
	{ 16, 3 },
	{ 20, 4 },
	{ 24, 5 },
	{ 28, 6 },
	{ 32, 7 },
};

static const unsigned ac108_bclkdivs[] = {
	 0,   1,   2,   4,
	 6,   8,  12,  16,
	24,  32,  48,  64,
	96, 128, 176, 192,
};

/* FOUT =(FIN * N) / [(M1+1) * (M2+1)*(K1+1)*(K2+1)] ;	M1[0,31],  M2[0,1],  N[0,1023],  K1[0,31],  K2[0,1] */
static const struct pll_div ac108_pll_div_list[] = {
	{ 400000,   _FREQ_24_576K, 0,  0, 614, 4, 1 },
	{ 512000,   _FREQ_24_576K, 0,  0, 960, 9, 1 }, //_FREQ_24_576K/48
	{ 768000,   _FREQ_24_576K, 0,  0, 640, 9, 1 }, //_FREQ_24_576K/32
	{ 800000,   _FREQ_24_576K, 0,  0, 614, 9, 1 },
	{ 1024000,  _FREQ_24_576K, 0,  0, 480, 9, 1 }, //_FREQ_24_576K/24
	{ 1600000,  _FREQ_24_576K, 0,  0, 307, 9, 1 },
	{ 2048000,  _FREQ_24_576K, 0,  0, 240, 9, 1 }, /* accurate,  8000 * 256 */
	{ 3072000,  _FREQ_24_576K, 0,  0, 160, 9, 1 }, /* accurate, 12000 * 256 */
	{ 4096000,  _FREQ_24_576K, 2,  0, 360, 9, 1 }, /* accurate, 16000 * 256 */
	{ 6000000,  _FREQ_24_576K, 4,  0, 410, 9, 1 },
	{ 12000000, _FREQ_24_576K, 9,  0, 410, 9, 1 },
	{ 13000000, _FREQ_24_576K, 8,  0, 340, 9, 1 },
	{ 15360000, _FREQ_24_576K, 12, 0, 415, 9, 1 },
	{ 16000000, _FREQ_24_576K, 12, 0, 400, 9, 1 },
	{ 19200000, _FREQ_24_576K, 15, 0, 410, 9, 1 },
	{ 19680000, _FREQ_24_576K, 15, 0, 400, 9, 1 },
	{ 24000000, _FREQ_24_576K, 4,  0, 128,24, 0 }, // accurate, 24M -> 24.576M */

	{ 400000,   _FREQ_22_579K, 0,  0, 566, 4, 1 },
	{ 512000,   _FREQ_22_579K, 0,  0, 880, 9, 1 },
	{ 768000,   _FREQ_22_579K, 0,  0, 587, 9, 1 },
	{ 800000,   _FREQ_22_579K, 0,  0, 567, 9, 1 },
	{ 1024000,  _FREQ_22_579K, 0,  0, 440, 9, 1 },
	{ 1600000,  _FREQ_22_579K, 1,  0, 567, 9, 1 },
	{ 2048000,  _FREQ_22_579K, 0,  0, 220, 9, 1 },
	{ 3072000,  _FREQ_22_579K, 0,  0, 148, 9, 1 },
	{ 4096000,  _FREQ_22_579K, 2,  0, 330, 9, 1 },
	{ 6000000,  _FREQ_22_579K, 2,  0, 227, 9, 1 },
	{ 12000000, _FREQ_22_579K, 8,  0, 340, 9, 1 },
	{ 13000000, _FREQ_22_579K, 9,  0, 350, 9, 1 },
	{ 15360000, _FREQ_22_579K, 10, 0, 325, 9, 1 },
	{ 16000000, _FREQ_22_579K, 11, 0, 340, 9, 1 },
	{ 19200000, _FREQ_22_579K, 13, 0, 330, 9, 1 },
	{ 19680000, _FREQ_22_579K, 14, 0, 345, 9, 1 },
	{ 24000000, _FREQ_22_579K, 24, 0, 588,24, 0 }, // accurate, 24M -> 22.5792M */


	{ _FREQ_24_576K / 1,   _FREQ_24_576K, 9,  0, 200, 9, 1 }, //_FREQ_24_576K
	{ _FREQ_24_576K / 2,   _FREQ_24_576K, 9,  0, 400, 9, 1 }, /*12288000,accurate, 48000 * 256 */
	{ _FREQ_24_576K / 4,   _FREQ_24_576K, 4,  0, 400, 9, 1 }, /*6144000, accurate, 24000 * 256 */
	{ _FREQ_24_576K / 16,  _FREQ_24_576K, 0,  0, 320, 9, 1 }, //1536000
	{ _FREQ_24_576K / 64,  _FREQ_24_576K, 0,  0, 640, 4, 1 }, //384000
	{ _FREQ_24_576K / 96,  _FREQ_24_576K, 0,  0, 960, 4, 1 }, //256000
	{ _FREQ_24_576K / 128, _FREQ_24_576K, 0,  0, 512, 1, 1 }, //192000
	{ _FREQ_24_576K / 176, _FREQ_24_576K, 0,  0, 880, 4, 0 }, //140000
	{ _FREQ_24_576K / 192, _FREQ_24_576K, 0,  0, 960, 4, 0 }, //128000

	{ _FREQ_22_579K / 1,   _FREQ_22_579K, 9,  0, 200, 9, 1 }, //_FREQ_22_579K
	{ _FREQ_22_579K / 2,   _FREQ_22_579K, 9,  0, 400, 9, 1 }, /*11289600,accurate, 44100 * 256 */
	{ _FREQ_22_579K / 4,   _FREQ_22_579K, 4,  0, 400, 9, 1 }, /*5644800, accurate, 22050 * 256 */
	{ _FREQ_22_579K / 16,  _FREQ_22_579K, 0,  0, 320, 9, 1 }, //1411200
	{ _FREQ_22_579K / 64,  _FREQ_22_579K, 0,  0, 640, 4, 1 }, //352800
	{ _FREQ_22_579K / 96,  _FREQ_22_579K, 0,  0, 960, 4, 1 }, //235200
	{ _FREQ_22_579K / 128, _FREQ_22_579K, 0,  0, 512, 1, 1 }, //176400
	{ _FREQ_22_579K / 176, _FREQ_22_579K, 0,  0, 880, 4, 0 }, //128290
	{ _FREQ_22_579K / 192, _FREQ_22_579K, 0,  0, 960, 4, 0 }, //117600

	{ _FREQ_22_579K / 6,   _FREQ_22_579K, 2,  0, 360, 9, 1 }, //3763200
	{ _FREQ_22_579K / 8,   _FREQ_22_579K, 0,  0, 160, 9, 1 }, /*2822400, accurate, 11025 * 256 */
	{ _FREQ_22_579K / 12,  _FREQ_22_579K, 0,  0, 240, 9, 1 }, //1881600
	{ _FREQ_22_579K / 24,  _FREQ_22_579K, 0,  0, 480, 9, 1 }, //940800
	{ _FREQ_22_579K / 32,  _FREQ_22_579K, 0,  0, 640, 9, 1 }, //705600
	{ _FREQ_22_579K / 48,  _FREQ_22_579K, 0,  0, 960, 9, 1 }, //470400
};

/*
 * *** To sync channels ***
 *
 * 1. disable clock in codec   hw_params()
 * 2. clear   fifo  in bcm2835 hw_params()
 * 3. clear   fifo  in bcm2385 prepare()
 * 4. enable  RX    in bcm2835 trigger()
 * 5. enable  clock in machine trigger()
 */
static struct ac10x_priv* static_ac10x;

int pcm5102a_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params,
	struct snd_soc_dai *dai)
{
	unsigned int i, channels, samp_res, rate, div;
	struct snd_soc_codec *codec = dai->codec;
	struct ac10x_priv *ac10x = snd_soc_codec_get_drvdata(codec);
	unsigned bclkdiv;
	int current_bclkdiv;
	int ret = 0;
	u8 reg;
	u8 v;

	PCM5102A_DBG("+++\n");

	// if (pcm5102a_sysclk_started()) {
	// 	/* not configure hw_param twice if stream is playback, tell the caller it's started */
	// 	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
	// 		return 1;
	// 	}
	// }

	if ((substream->stream == SNDRV_PCM_STREAM_CAPTURE && dai->stream_active[SNDRV_PCM_STREAM_PLAYBACK])
	 || (substream->stream == SNDRV_PCM_STREAM_PLAYBACK && dai->stream_active[SNDRV_PCM_STREAM_CAPTURE])) {
		/* If playback is performed after capture, it is need to reset only the LRCK appropriately. */
		channels = params_channels(params);

		switch (params_format(params)) {
		case SNDRV_PCM_FORMAT_S8:
			samp_res = 0;
			break;
		case SNDRV_PCM_FORMAT_S16_LE:
			samp_res = 2;
			break;
		case SNDRV_PCM_FORMAT_S20_3LE:
			samp_res = 3;
			break;
		case SNDRV_PCM_FORMAT_S24_LE:
			samp_res = 4;
			break;
		case SNDRV_PCM_FORMAT_S32_LE:
			samp_res = 6;
			break;
		default:
			pr_err("AC108 don't supported the sample resolution: %u\n", params_format(params));
			return -EINVAL;
		}

		for (i = 0; i < ARRAY_SIZE(ac108_sample_rate); i++) {
			if (ac108_sample_rate[i].real_val == params_rate(params) / (ac10x->data_protocol + 1UL)) {
				rate = i;
				break;
			}
		}
		if (i >= ARRAY_SIZE(ac108_sample_rate)) {
			return -EINVAL;
		}

		if (channels == 8 && ac108_sample_rate[rate].real_val == 96000) {
			/* 24.576M bit clock is not support by ac108 */
			return -EINVAL;
		}

		/**
		* 0x33: 
		*  The 8-Low bit of LRCK period value. It is used to program
		*  the number of BCLKs per channel of sample frame. This value
		*  is interpreted as follow:
		*  The 8-Low bit of LRCK period value. It is used to program
		*  the number of BCLKs per channel of sample frame. This value
		*  is interpreted as follow: PCM mode: Number of BCLKs within
		*  (Left + Right) channel width I2S / Left-Justified /
		*  Right-Justified mode: Number of BCLKs within each individual
		*  channel width (Left or Right) N+1
		*  For example:
		*  n = 7: 8 BCLK width
		*  …
		*  n = 1023: 1024 BCLKs width
		*  0X32[0:1]:
		*  The 2-High bit of LRCK period value. 
		*/
		if (ac10x->i2s_mode != PCM_FORMAT) {
			if (ac10x->data_protocol) {
				ac108_multi_write(I2S_LRCK_CTRL2, ac108_samp_res[samp_res].real_val - 1, ac10x);
				/*encoding mode, the max LRCK period value < 32,so the 2-High bit is zero*/
				ac108_multi_update_bits(I2S_LRCK_CTRL1, 0x03 << 0, 0x00, ac10x);
			} else {
				/*TDM mode or normal mode*/
				ac108_multi_update_bits(I2S_LRCK_CTRL1, 0x03 << 0, 0x00, ac10x);
			}
		} else {
			/*TDM mode or normal mode*/
			div = ac108_samp_res[samp_res].real_val * channels - 1;
			ac108_multi_write(I2S_LRCK_CTRL2, (div & 0xFF), ac10x);
			ac108_multi_update_bits(I2S_LRCK_CTRL1, 0x03 << 0, (div >> 8) << 0, ac10x);
		}

		/*read the register and if the settings are not matched to the request, reset the register*/
		ac10x_read(I2S_BCLK_CTRL, &reg, ac10x->i2cmap[_MASTER_INDEX]);
		current_bclkdiv = ac108_bclkdivs[(reg & 0x0f) ];

		if (ac10x->mclk/current_bclkdiv/params_rate(params)/params_channels(params)/ac108_samp_res[samp_res].real_val != 1) {
			return -EINVAL;
		}
		else{
			div = ac108_samp_res[samp_res].real_val * channels - 1;
			ac108_multi_write(I2S_LRCK_CTRL2, (div & 0xFF), ac10x);
			ac108_multi_update_bits(I2S_LRCK_CTRL1, 0x03 << 0, (div >> 8) << 0, ac10x);
		}

		if (reg & (0x01 << LRCK_IOEN)) {
			ret = ret || ac10x_update_bits(I2S_CTRL, 0x03 << LRCK_IOEN, 0x01 << BCLK_IOEN, ac10x->i2cmap[_MASTER_INDEX]);
		}
		if (!ret) {
			ac10x->sysclk_en = 0UL;
		}

		return 0;
	}
	else {
		channels = params_channels(params);

		/* Master mode, to clear cpu_dai fifos, output bclk without lrck */
		ac10x_read(I2S_CTRL, &v, ac10x->i2cmap[_MASTER_INDEX]);
		if (v & (0x01 << BCLK_IOEN)) {
			ac10x_update_bits(I2S_CTRL, 0x1 << LRCK_IOEN, 0x0 << LRCK_IOEN, ac10x->i2cmap[_MASTER_INDEX]);
		}

		switch (params_format(params)) {
		case SNDRV_PCM_FORMAT_S8:
			samp_res = 0;
			break;
		case SNDRV_PCM_FORMAT_S16_LE:
			samp_res = 2;
			break;
		case SNDRV_PCM_FORMAT_S20_3LE:
			samp_res = 3;
			break;
		case SNDRV_PCM_FORMAT_S24_LE:
			samp_res = 4;
			break;
		case SNDRV_PCM_FORMAT_S32_LE:
			samp_res = 6;
			break;
		default:
			pr_err("AC108 don't supported the sample resolution: %u\n", params_format(params));
			return -EINVAL;
		}

		for (i = 0; i < ARRAY_SIZE(ac108_sample_rate); i++) {
			if (ac108_sample_rate[i].real_val == params_rate(params) / (ac10x->data_protocol + 1UL)) {
				rate = i;
				break;
			}
		}
		if (i >= ARRAY_SIZE(ac108_sample_rate)) {
			return -EINVAL;
		}

		if (channels == 8 && ac108_sample_rate[rate].real_val == 96000) {
			/* 24.576M bit clock is not support by ac108 */
			return -EINVAL;
		}

		dev_dbg(dai->dev, "rate: %d , channels: %d , samp_res: %d",
				ac108_sample_rate[rate].real_val,
				channels,
				ac108_samp_res[samp_res].real_val);

		/**
		* 0x33: 
		*  The 8-Low bit of LRCK period value. It is used to program
		*  the number of BCLKs per channel of sample frame. This value
		*  is interpreted as follow:
		*  The 8-Low bit of LRCK period value. It is used to program
		*  the number of BCLKs per channel of sample frame. This value
		*  is interpreted as follow: PCM mode: Number of BCLKs within
		*  (Left + Right) channel width I2S / Left-Justified /
		*  Right-Justified mode: Number of BCLKs within each individual
		*  channel width (Left or Right) N+1
		*  For example:
		*  n = 7: 8 BCLK width
		*  …
		*  n = 1023: 1024 BCLKs width
		*  0X32[0:1]:
		*  The 2-High bit of LRCK period value. 
		*/
		if (ac10x->i2s_mode != PCM_FORMAT) {
			if (ac10x->data_protocol) {
				ac108_multi_write(I2S_LRCK_CTRL2, ac108_samp_res[samp_res].real_val - 1, ac10x);
				/*encoding mode, the max LRCK period value < 32,so the 2-High bit is zero*/
				ac108_multi_update_bits(I2S_LRCK_CTRL1, 0x03 << 0, 0x00, ac10x);
			} else {
				/*TDM mode or normal mode*/
				ac108_multi_update_bits(I2S_LRCK_CTRL1, 0x03 << 0, 0x00, ac10x);
			}

		} else {
			/*TDM mode or normal mode*/
			div = ac108_samp_res[samp_res].real_val * channels - 1;
			ac108_multi_write(I2S_LRCK_CTRL2, (div & 0xFF), ac10x);
			ac108_multi_update_bits(I2S_LRCK_CTRL1, 0x03 << 0, (div >> 8) << 0, ac10x);
		}

		/**
		* 0x35: 
		* TX Encoding mode will add  4bits to mark channel number 
		* TODO: need a chat to explain this 
		*/
		ac108_multi_update_bits(I2S_FMT_CTRL2, 0x07 << SAMPLE_RESOLUTION | 0x07 << SLOT_WIDTH_SEL,
							ac108_samp_res[samp_res].reg_val << SAMPLE_RESOLUTION
							| ac108_samp_res[samp_res].reg_val << SLOT_WIDTH_SEL, ac10x);

		/**
		* 0x60: 
		* ADC Sample Rate synchronised with I2S1 clock zone 
		*/
		ac108_multi_update_bits(ADC_SPRC, 0x0f << ADC_FS_I2S1, ac108_sample_rate[rate].reg_val << ADC_FS_I2S1, ac10x);
		ac108_multi_write(HPF_EN, 0x0F, ac10x);

		ac108_config_pll(ac10x, ac108_sample_rate[rate].real_val, ac108_samp_res[samp_res].real_val * channels);

		/*
		* master mode only
		*/
		bclkdiv = ac10x->mclk / (ac108_sample_rate[rate].real_val * channels * ac108_samp_res[samp_res].real_val);
		for (i = 0; i < ARRAY_SIZE(ac108_bclkdivs) - 1; i++) {
			if (ac108_bclkdivs[i] >= bclkdiv) {
				break;
			}
		}
		ac108_multi_update_bits(I2S_BCLK_CTRL, 0x0F << BCLKDIV, i << BCLKDIV, ac10x);

		/*0x21: Module clock enable<I2S, ADC digital, MIC offset Calibration, ADC analog>*/
		ac108_multi_write(MOD_CLK_EN, 1 << I2S | 1 << ADC_DIGITAL | 1 << MIC_OFFSET_CALIBRATION | 1 << ADC_ANALOG, ac10x);
		/*0x22: Module reset de-asserted<I2S, ADC digital, MIC offset Calibration, ADC analog>*/
		ac108_multi_write(MOD_RST_CTRL, 1 << I2S | 1 << ADC_DIGITAL | 1 << MIC_OFFSET_CALIBRATION | 1 << ADC_ANALOG, ac10x);


		dev_dbg(dai->dev, "%s() stream=%s ---\n", __func__,
				snd_pcm_stream_str(substream));

		return 0;
	}

	ac10x->dac_enable = 1;

	PCM5102A_DBG("rate: %d , channels: %d , samp_res: %d",
		params_rate(params), channels, aif1_slot_size);

	PCM5102A_DBG("---\n");
	return 0;
}

int pcm5102a_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	unsigned char tx_offset, lrck_polarity, brck_polarity;
	struct ac10x_priv *ac10x = dev_get_drvdata(dai->dev);

	PCM5102A_DBG();
	dev_dbg(dai->dev, "%s\n", __FUNCTION__);

	if ((dai->stream_active[SNDRV_PCM_STREAM_PLAYBACK] && dai->stream_active[SNDRV_PCM_STREAM_CAPTURE])) {
	}
	else{
		switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
		case SND_SOC_DAIFMT_CBM_CFM:    /*AC108 Master*/
			dev_dbg(dai->dev, "AC108 set to work as Master\n");
			/**
			* 0x30:chip is master mode ,BCLK & LRCK output
			*/
			ac108_multi_update_bits(I2S_CTRL, 0x03 << LRCK_IOEN | 0x03 << SDO1_EN | 0x1 << TXEN | 0x1 << GEN,
							0x00 << LRCK_IOEN | 0x03 << SDO1_EN | 0x1 << TXEN | 0x1 << GEN, ac10x);
			/* multi_chips: only one chip set as Master, and the others also need to set as Slave */
			ac10x_update_bits(I2S_CTRL, 0x3 << LRCK_IOEN, 0x01 << BCLK_IOEN, ac10x->i2cmap[_MASTER_INDEX]);
			break;
			fallthrough;
		case SND_SOC_DAIFMT_CBS_CFS:    /*AC108 Slave*/
			dev_dbg(dai->dev, "AC108 set to work as Slave\n");
			/**
			* 0x30:chip is slave mode, BCLK & LRCK input,enable SDO1_EN and 
			*  SDO2_EN, Transmitter Block Enable, Globe Enable
			*/
			ac108_multi_update_bits(I2S_CTRL, 0x03 << LRCK_IOEN | 0x03 << SDO1_EN | 0x1 << TXEN | 0x1 << GEN,
							0x00 << LRCK_IOEN | 0x03 << SDO1_EN | 0x0 << TXEN | 0x0 << GEN, ac10x);
			break;
		default:
			pr_err("AC108 Master/Slave mode config error:%u\n\n", (fmt & SND_SOC_DAIFMT_MASTER_MASK) >> 12);
			return -EINVAL;
		}

		/*AC108 config I2S/LJ/RJ/PCM format*/
		switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
		case SND_SOC_DAIFMT_I2S:
			dev_dbg(dai->dev, "AC108 config I2S format\n");
			ac10x->i2s_mode = LEFT_JUSTIFIED_FORMAT;
			tx_offset = 1;
			break;
		case SND_SOC_DAIFMT_RIGHT_J:
			dev_dbg(dai->dev, "AC108 config RIGHT-JUSTIFIED format\n");
			ac10x->i2s_mode = RIGHT_JUSTIFIED_FORMAT;
			tx_offset = 0;
			break;
		case SND_SOC_DAIFMT_LEFT_J:
			dev_dbg(dai->dev, "AC108 config LEFT-JUSTIFIED format\n");
			ac10x->i2s_mode = LEFT_JUSTIFIED_FORMAT;
			tx_offset = 0;
			break;
		case SND_SOC_DAIFMT_DSP_A:
			dev_dbg(dai->dev, "AC108 config PCM-A format\n");
			ac10x->i2s_mode = PCM_FORMAT;
			tx_offset = 1;
			break;
		case SND_SOC_DAIFMT_DSP_B:
			dev_dbg(dai->dev, "AC108 config PCM-B format\n");
			ac10x->i2s_mode = PCM_FORMAT;
			tx_offset = 0;
			break;
		default:
			pr_err("AC108 I2S format config error:%u\n\n", fmt & SND_SOC_DAIFMT_FORMAT_MASK);
			return -EINVAL;
		}

		/*AC108 config BCLK&LRCK polarity*/
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			dev_dbg(dai->dev, "AC108 config BCLK&LRCK polarity: BCLK_normal,LRCK_normal\n");
			brck_polarity = BCLK_NORMAL_DRIVE_N_SAMPLE_P;
			lrck_polarity = LRCK_LEFT_HIGH_RIGHT_LOW;
			break;
		case SND_SOC_DAIFMT_NB_IF:
			dev_dbg(dai->dev, "AC108 config BCLK&LRCK polarity: BCLK_normal,LRCK_invert\n");
			brck_polarity = BCLK_NORMAL_DRIVE_N_SAMPLE_P;
			lrck_polarity = LRCK_LEFT_LOW_RIGHT_HIGH;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			dev_dbg(dai->dev, "AC108 config BCLK&LRCK polarity: BCLK_invert,LRCK_normal\n");
			brck_polarity = BCLK_INVERT_DRIVE_P_SAMPLE_N;
			lrck_polarity = LRCK_LEFT_HIGH_RIGHT_LOW;
			break;
		case SND_SOC_DAIFMT_IB_IF:
			dev_dbg(dai->dev, "AC108 config BCLK&LRCK polarity: BCLK_invert,LRCK_invert\n");
			brck_polarity = BCLK_INVERT_DRIVE_P_SAMPLE_N;
			lrck_polarity = LRCK_LEFT_LOW_RIGHT_HIGH;
			break;
		default:
			pr_err("AC108 config BCLK/LRCLK polarity error:%u\n\n", (fmt & SND_SOC_DAIFMT_INV_MASK) >> 8);
			return -EINVAL;
		}

		ac108_configure_power(ac10x);

		/**
		*0x31: 0: normal mode, negative edge drive and positive edge sample
			1: invert mode, positive edge drive and negative edge sample
		*/
		ac108_multi_update_bits(I2S_BCLK_CTRL,  0x01 << BCLK_POLARITY, brck_polarity << BCLK_POLARITY, ac10x);
		/**
		* 0x32: same as 0x31
		*/
		ac108_multi_update_bits(I2S_LRCK_CTRL1, 0x01 << LRCK_POLARITY, lrck_polarity << LRCK_POLARITY, ac10x);
		/**
		* 0x34:Encoding Mode Selection,Mode 
		* Selection,data is offset by 1 BCLKs to LRCK 
		* normal mode for the last half cycle of BCLK in the slot ?
		* turn to hi-z state (TDM) when not transferring slot ?
		*/
		ac108_multi_update_bits(I2S_FMT_CTRL1,	0x01 << ENCD_SEL | 0x03 << MODE_SEL | 0x01 << TX2_OFFSET |
							0x01 << TX1_OFFSET | 0x01 << TX_SLOT_HIZ | 0x01 << TX_STATE,
									ac10x->data_protocol << ENCD_SEL 	|
									ac10x->i2s_mode << MODE_SEL 		|
									tx_offset << TX2_OFFSET 			|
									tx_offset << TX1_OFFSET 			|
									0x00 << TX_SLOT_HIZ 				|
									0x01 << TX_STATE, ac10x);

		/**
		* 0x60: 
		* MSB / LSB First Select: This driver only support MSB First Select . 
		* OUT2_MUTE,OUT1_MUTE shoule be set in widget. 
		* LRCK = 1 BCLK width 
		* Linear PCM 
		*  
		* TODO:pcm mode, bit[0:1] and bit[2] is special
		*/
		ac108_multi_update_bits(I2S_FMT_CTRL3,	0x01 << TX_MLS | 0x03 << SEXT  | 0x01 << LRCK_WIDTH | 0x03 << TX_PDM,
							0x00 << TX_MLS | 0x03 << SEXT  | 0x00 << LRCK_WIDTH | 0x00 << TX_PDM, ac10x);

		ac108_multi_write(HPF_EN, 0x00, ac10x);
	}
	return 0;
}

int pcm5102a_audio_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *codec_dai)
{
	// struct snd_soc_codec *codec = codec_dai->codec;

	PCM5102A_DBG("\n\n\n");

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
	}
	return 0;
}

int pcm5102a_trigger(struct snd_pcm_substream *substream, int cmd,
	 	  struct snd_soc_dai *dai)
{
	//struct snd_soc_codec *codec = dai->codec;
	//struct ac10x_priv *ac10x = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	PCM5102A_DBG("stream=%s  cmd=%d\n",
		snd_pcm_stream_str(substream),
		cmd);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		break;
	default:
		ret = -EINVAL;
	}
	PCM5102A_DBG("stream=%s  cmd=%d;finished %d\n",
		snd_pcm_stream_str(substream),
		cmd, ret);
	return ret;
}

static void codec_resume_work(struct work_struct *work)
{
	//struct ac10x_priv *ac10x = container_of(work, struct ac10x_priv, codec_resume);
	//struct snd_soc_codec *codec = ac10x->codec;

	PCM5102A_DBG("+++\n");
	PCM5102A_DBG("---\n");
	return;
}

int pcm5102a_set_bias_level(struct snd_soc_codec *codec, enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		PCM5102A_DBG("SND_SOC_BIAS_ON\n");
		break;
	case SND_SOC_BIAS_PREPARE:
		PCM5102A_DBG("SND_SOC_BIAS_PREPARE\n");
		break;
	case SND_SOC_BIAS_STANDBY:
		PCM5102A_DBG("SND_SOC_BIAS_STANDBY\n");
		break;
	case SND_SOC_BIAS_OFF:
		PCM5102A_DBG("SND_SOC_BIAS_OFF\n");
		break;
	}
	snd_soc_codec_get_dapm(codec)->bias_level = level;
	return 0;
}

int pcm5102a_codec_probe(struct snd_soc_codec *codec)
{
	struct ac10x_priv *ac10x;

	ac10x = dev_get_drvdata(codec->dev);
	if (ac10x == NULL) {
		PCM5102A_DBG("not set client data!\n");
		return -ENOMEM;
	}
	ac10x->codec = codec;
	
	// INIT_DELAYED_WORK(&ac10x->dlywork, ac10x_work_aif_play);
	INIT_WORK(&ac10x->codec_resume, codec_resume_work);
	ac10x->dac_enable = 0;
	
	return 0;
}

/* power down chip */
int pcm5102a_codec_remove(struct snd_soc_codec *codec)
{
	return 0;
}

int pcm5102a_codec_suspend(struct snd_soc_codec *codec)
{
	struct ac10x_priv *ac10x = snd_soc_codec_get_drvdata(codec);

	PCM5102A_DBG("[codec]:suspend\n");
	return 0;
}

int pcm5102a_codec_resume(struct snd_soc_codec *codec)
{
	struct ac10x_priv *ac10x = snd_soc_codec_get_drvdata(codec);

	PCM5102A_DBG("[codec]:resume");

	schedule_work(&ac10x->codec_resume);
	return 0;
}


/************************************************************/

/* Sync reg_cache from the hardware */
int ac10x_fill_regcache(struct device* dev, struct regmap* map) {
	int r, i, n;
	int v;

	n = regmap_get_max_register(map);
	for (i = 0; i < n; i++) {
		regcache_cache_bypass(map, true);
		r = regmap_read(map, i, &v);
		if (r) {
			dev_err(dev, "failed to read register %d\n", i);
			continue;
		}
		regcache_cache_bypass(map, false);

		regcache_cache_only(map, true);
		r = regmap_write(map, i, v);
		regcache_cache_only(map, false);
	}
	regcache_cache_bypass(map, false);
	regcache_cache_only(map, false);

	return 0;
}

int pcm5102a_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	struct ac10x_priv *ac10x = i2c_get_clientdata(i2c);

	PCM5102A_DBG();

	static_ac10x = ac10x;

	ac108_i2c_probe(i2c, id);

	return 0;
}

void pcm5102a_shutdown(struct i2c_client *i2c)
{
	struct ac10x_priv *ac10x = i2c_get_clientdata(i2c);
	struct snd_soc_codec *codec = ac10x->codec;

	if (codec == NULL) {
		pr_err(": no sound card.\n");
		return;
	}

	return;
}

int pcm5102a_remove(struct i2c_client *i2c)
{
	//sysfs_remove_group(&i2c->dev.kobj, &audio_debug_attr_group);
	return 0;
}

MODULE_DESCRIPTION("pcm5102a with ASoC ac108 driver");
MODULE_AUTHOR("Ryoma Tanase");
