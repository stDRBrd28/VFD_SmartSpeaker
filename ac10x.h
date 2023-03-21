/*
 * ac10x.h
 *
 * (C) Copyright 2017-2018
 * Seeed Technology Co., Ltd. <www.seeedstudio.com>
 *
 * PeterYang <linsheng.yang@seeed.cc>
 *
 * (C) Copyright 2010-2017
 * Reuuimlla Technology Co., Ltd. <www.reuuimllatech.com>
 * huangxin <huangxin@reuuimllatech.com>
 *
 * some simple description for this code
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */
#ifndef __AC10X_H__
#define __AC10X_H__

#define _USE_CAPTURE	1
#define _MASTER_AC108		0
#define _MASTER_MULTI_CODEC	_MASTER_AC108
#define _MASTER_INDEX       _MASTER_AC108

/* enable headset detecting & headset button pressing */
#define CONFIG_AC101_SWITCH_DETECT

/* obsolete */
#define CONFIG_AC10X_TRIG_LOCK	0


#ifdef PCM5102A_DEBG
    #define PCM5102A_DBG(format,args...)  printk("[PCM5102A] %s() L%d " format, __func__, __LINE__, ##args)
#else
    #define PCM5102A_DBG(...)
#endif


#include "sound-compatible-4.18.h"

#ifdef CONFIG_AC101_SWITCH_DETECT
enum headphone_mode_u {
	HEADPHONE_IDLE,
	FOUR_HEADPHONE_PLUGIN,
	THREE_HEADPHONE_PLUGIN,
};
#endif

struct ac10x_priv {
	struct i2c_client *i2c[4];
	struct regmap* i2cmap[4];
	int codec_cnt;
	unsigned sysclk;
#define _FREQ_24_576K		24576000
#define _FREQ_22_579K		22579200
	unsigned mclk;	/* master clock or aif_clock/aclk */
	int clk_id;
	unsigned char i2s_mode;
	unsigned char data_protocol;
	// struct delayed_work dlywork;
	int tdm_chips_cnt;
	int sysclk_en;
	int dac_enable;
	spinlock_t lock;

	/* member for DAC .begin */
	struct snd_soc_codec *codec;

	struct work_struct codec_resume;

	// struct input_dev* inpdev;
	/* member for DAC .end */
};


/* AC101 DAI operations */
int pcm5102a_audio_startup(struct snd_pcm_substream *substream, struct snd_soc_dai *codec_dai);
void pcm5102a_aif_shutdown(struct snd_pcm_substream *substream, struct snd_soc_dai *codec_dai);
int pcm5102a_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt);
int pcm5102a_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params,
	struct snd_soc_dai *codec_dai);
int pcm5102a_trigger(struct snd_pcm_substream *substream, int cmd,
	 	  struct snd_soc_dai *dai);
int pcm5102a_aif_mute(struct snd_soc_dai *codec_dai, int mute);

int ac108_multi_write(u8 reg, u8 val, struct ac10x_priv *ac10x);
int ac108_multi_update_bits(u8 reg, u8 mask, u8 val, struct ac10x_priv *ac10x);
int ac10x_read(u8 reg, u8* rt_val, struct regmap* i2cm);
int ac10x_write(u8 reg, u8 val, struct regmap* i2cm);
int ac10x_update_bits(u8 reg, u8 mask, u8 val, struct regmap* i2cm);
int ac108_config_pll(struct ac10x_priv *ac10x, unsigned rate, unsigned lrck_ratio);
int ac108_i2c_probe(struct i2c_client *i2c, const struct i2c_device_id *i2c_id);
void ac108_configure_power(struct ac10x_priv *ac10x);

/* codec driver specific */
int pcm5102a_codec_probe(struct snd_soc_codec *codec);
int pcm5102a_codec_remove(struct snd_soc_codec *codec);
int pcm5102a_codec_suspend(struct snd_soc_codec *codec);
int pcm5102a_codec_resume(struct snd_soc_codec *codec);
int pcm5102a_set_bias_level(struct snd_soc_codec *codec, enum snd_soc_bias_level level);

/* i2c device specific */
int pcm5102a_probe(struct i2c_client *i2c, const struct i2c_device_id *id);
void pcm5102a_shutdown(struct i2c_client *i2c);
int pcm5102a_remove(struct i2c_client *i2c);

/* seeed voice card export */
int seeed_voice_card_register_set_clock(int stream, int (*set_clock)(int, struct snd_pcm_substream *, int, struct snd_soc_dai *));

int ac10x_fill_regcache(struct device* dev, struct regmap* map);

#endif//__AC10X_H__
