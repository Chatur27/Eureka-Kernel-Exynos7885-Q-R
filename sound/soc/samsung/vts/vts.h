/* sound/soc/samsung/vts/vts.h
 *
 * ALSA SoC - Samsung VTS driver
 *
 * Copyright (c) 2016 Samsung Electronics Co. Ltd.
  *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SND_SOC_VTS_H
#define __SND_SOC_VTS_H

/* SYSREG_VTS */
#define VTS_USER_REG2			(0x0008)
#define VTS_BUS_COMPONENT_DRCG_EN	(0x0200)
#define VTS_DEBUG			(0x1004)
#define VTS_DMIC_CLK_CTRL		(0x1008)
#define VTS_SHARED_MEM_CTRL		(0x100C)
#define VTS_VTS_MEM_CONFIG0		(0x1010)
#define VTS_VTS_MEM_CONFIG1		(0x1014)
#define VTS_VTS_MEM_CONFIG2 		(0x1018)
#define VTS_VTS_MEM_CONFIG3		(0x101C)
#define VTS_HWACG_CM4_STATUS		(0x1020)
#define VTS_HWACG_CM4_CONFIG		(0x1024)
#define VTS_HWACG_CM4_CLKREQ		(0x1028)

/* VTS_USER_REG2 */
#define VTS_ENABLE_CLK_GEN_OFFSET	(0)
#define VTS_ENABLE_CLK_GEN_SIZE		(1)
#define VTS_SEL_EXT_DMIC_CLK_OFFSET	(1)
#define VTS_SEL_EXT_DMIC_CLK_SIZE	(1)
#define VTS_ENABLE_CLK_CLK_GEN_OFFSET	(14)
#define VTS_ENABLE_CLK_CLK_GEN_SIZE	(1)

/* VTS_BUS_COMPONENT_DRCG_EN */
#define VTS_LHS_AXI_D_VTS_OFFSET	(1)
#define VTS_LHS_AXI_D_VTS_SIZE		(1)
#define VTS_LHM_AXI_P_VTS_OFFSET	(0)
#define VTS_LHM_AXI_P_VTS_SIZE		(1)
/* VTS_DEBUG */
#define VTS_NMI_EN_BY_WDT_OFFSET	(0)
#define VTS_NMI_EN_BY_WDT_SIZE		(1)
/* VTS_DMIC_CLK_CTRL */
#define VTS_CG_STATUS_OFFSET		(5)
#define VTS_CG_STATUS_SIZE		(1)
#define VTS_CLK_ENABLE_OFFSET		(4)
#define VTS_CLK_ENABLE_SIZE		(1)
#define VTS_CLK_SEL_OFFSET		(0)
#define VTS_CLK_SEL_SIZE		(1)
/* VTS_SHARED_MEM_CTRL */
#define VTS_MEM_SEL_OFFSET		(0)
#define VTS_MEM_SEL_SIZE		(1)
/* VTS_VTS_MEM_CONFIG0 */
#define VTS_VTS_MEM_SIZE_OFFSET		(0)
#define VTS_VTS_MEM_SIZE_SIZE		(20)
/* VTS_VTS_MEM_CONFIG1 */
#define VTS_VTS_MEM_BA0_OFFSET		(0)
#define VTS_VTS_MEM_BA0_SIZE		(24)
/* VTS_VTS_MEM_CONFIG2 */
#define VTS_VTS_MEM_BA1_OFFSET		(0)
#define VTS_VTS_MEM_BA1_SIZE		(24)
/* VTS_VTS_MEM_CONFIG3 */
#define VTS_VTS_ADDR_RNG_OFFSET		(0)
#define VTS_VTS_ADDR_RNG_SIZE		(20)
/* VTS_HWACG_CM4_STATUS */
#define VTS_STATES_OFFSET		(0)
#define VTS_STATES_SIZE			(3)
/* VTS_HWACG_CM4_CONFIG */
#define VTS_EXPIRE_VALUE_OFFSET		(0)
#define VTS_EXPIRE_VALUE_SIZE		(8)
/* VTS_HWACG_CM4_CLKREQ */
#define VTS_MASK_OFFSET			(0)
#define VTS_MASK_SIZE			(32)

#define VTS_DMIC_ENABLE_DMIC_IF		(0x0000)
#define VTS_DMIC_CONTROL_DMIC_IF	(0x0004)
/* VTS_DMIC_ENABLE_DMIC_IF */
#define VTS_DMIC_ENABLE_DMIC_IF_OFFSET	(31)
#define VTS_DMIC_ENABLE_DMIC_IF_SIZE	(1)
#define VTS_DMIC_PERIOD_DATA2REQ_OFFSET	(16)
#define VTS_DMIC_PERIOD_DATA2REQ_SIZE	(2)
/* VTS_DMIC_CONTROL_DMIC_IF */
#define VTS_DMIC_HPF_EN_OFFSET		(31)
#define VTS_DMIC_HPF_EN_SIZE		(1)
#define VTS_DMIC_HPF_SEL_OFFSET		(28)
#define VTS_DMIC_HPF_SEL_SIZE		(1)
#define VTS_DMIC_CPS_SEL_OFFSET		(27)
#define VTS_DMIC_CPS_SEL_SIZE		(1)
#define VTS_DMIC_GAIN_OFFSET		(24)
#define VTS_DMIC_GAIN_SIZE		(3)
#define VTS_DMIC_DMIC_SEL_OFFSET	(18)
#define VTS_DMIC_DMIC_SEL_SIZE		(1)
#define VTS_DMIC_RCH_EN_OFFSET		(17)
#define VTS_DMIC_RCH_EN_SIZE		(1)
#define VTS_DMIC_LCH_EN_OFFSET		(16)
#define VTS_DMIC_LCH_EN_SIZE		(1)
#define VTS_DMIC_SYS_SEL_OFFSET		(12)
#define VTS_DMIC_SYS_SEL_SIZE		(2)
#define VTS_DMIC_POLARITY_CLK_OFFSET	(10)
#define VTS_DMIC_POLARITY_CLK_SIZE	(1)
#define VTS_DMIC_POLARITY_OUTPUT_OFFSET	(9)
#define VTS_DMIC_POLARITY_OUTPUT_SIZE	(1)
#define VTS_DMIC_POLARITY_INPUT_OFFSET	(8)
#define VTS_DMIC_POLARITY_INPUT_SIZE	(1)
#define VTS_DMIC_OVFW_CTRL_OFFSET	(4)
#define VTS_DMIC_OVFW_CTRL_SIZE		(1)
#define VTS_DMIC_CIC_SEL_OFFSET		(0)
#define VTS_DMIC_CIC_SEL_SIZE		(1)

#define VTS_IRQ_VTS_ERROR		(0)
#define VTS_IRQ_VTS_BOOT_COMPLETED	(1)
#define VTS_IRQ_VTS_IPC_RECEIVED	(2)
#define VTS_IRQ_VTS_VOICE_TRIGGERED	(3)
#define VTS_IRQ_VTS_PERIOD_ELAPSED	(4)
#define VTS_IRQ_VTS_REC_PERIOD_ELAPSED	(5)

#define VTS_IRQ_AP_IPC_RECEIVED		(16)
#define VTS_IRQ_AP_SET_DRAM_BUFFER	(17)
#define VTS_IRQ_AP_START_RECOGNITION	(18)
#define VTS_IRQ_AP_STOP_RECOGNITION	(19)
#define VTS_IRQ_AP_START_COPY		(20)
#define VTS_IRQ_AP_STOP_COPY		(21)
#define VTS_IRQ_AP_SET_MODE		(22)
#define VTS_IRQ_AP_TARGET_SIZE		(24)
#define VTS_IRQ_AP_SET_REC_BUFFER	(25)
#define VTS_IRQ_AP_START_REC		(26)
#define VTS_IRQ_AP_STOP_REC		(27)

#define VTS_IRQ_LIMIT			(32)

#define VTS_BAAW_BASE			(0x60000000)

#define BUFFER_BYTES_MAX (0xa0000)
#define PERIOD_BYTES_MIN (SZ_4)
#define PERIOD_BYTES_MAX (BUFFER_BYTES_MAX / 2)

#define SOUND_MODEL_SIZE_MAX (SZ_32K)
#define SOUND_MODEL_COUNT (3)

/* net & grammar binary sizes defined in firmware */
#define SOUND_MODEL_NET_SIZE_MAX (0x8000)
#define SOUND_MODEL_GRAMMAR_SIZE_MAX (0x300)
enum ipc_state {
	IDLE,
	SEND_MSG,
	SEND_MSG_OK,
	SEND_MSG_FAIL,
};

enum trigger {
	TRIGGER_NONE	= -1,
	TRIGGER_MCD	= 0,
	TRIGGER_SENSORY	= 1,
	TRIGGER_GOOGLE	= 2,
	TRIGGER_COUNT,
};

enum vts_platform_type {
	PLATFORM_VTS_NORMAL_RECORD,
	PLATFORM_VTS_TRIGGER_RECORD,
};

enum executionmode {
	VTS_OFF_MODE		= 0,	//default is off
	VTS_VOICE_TRIGGER_MODE	= 1,	//voice-trig-mode:Both LPSD & Trigger are enabled
	VTS_SOUND_DETECT_MODE	= 2,	//sound-detect-mode: Low Power sound Detect
	VTS_VT_ALWAYS_ON_MODE	= 3,	//vt-always-mode: key phrase Detection only(Trigger)
	VTS_MODE_COUNT,
};

struct vts_ipc_msg {
	int msg;
	u32 values[3];
};

struct vts_data {
	struct platform_device *pdev;
	void __iomem *sfr_base;
	void __iomem *sram_base;
	void __iomem *dmic_base;
	size_t sram_size;
	struct regmap *regmap_dmic;
	struct clk *clk_rco;
	struct clk *clk_dmic;
	struct clk *clk_dmic_if;
	struct clk *clk_dmic_sync;
	struct pinctrl *pinctrl;
	const struct firmware *firmware;
	unsigned int vtsdma_count;
	struct platform_device *pdev_mailbox;
	struct platform_device *pdev_vtsdma[2];
	struct proc_dir_entry *proc_dir_entry;
	int irq[VTS_IRQ_LIMIT];
	volatile enum ipc_state ipc_state_ap;
	wait_queue_head_t ipc_wait_queue;
	spinlock_t ipc_spinlock;
	struct mutex ipc_mutex;
	u32 dma_area_vts;
	struct snd_dma_buffer dmab;
	struct snd_dma_buffer dmab_rec;
	u32 target_size;
	volatile enum trigger active_trigger;
	u32 voicerecog_start;
	enum executionmode exec_mode;
	bool vts_ready;
	volatile unsigned long sram_acquired;
	volatile bool enabled;
	struct snd_soc_card *card;
	int micclk_init_cnt;
};

struct vts_platform_data {
	unsigned int id;
	struct platform_device *pdev_vts;
	struct vts_data *vts_data;
	struct snd_pcm_substream *substream;
	enum vts_platform_type type;
	volatile unsigned int pointer;
};

extern int vts_start_ipc_transaction(struct device *dev, struct vts_data *data,
		int msg, u32 (*values)[3], int atomic, int sync);

extern void vts_register_dma(struct platform_device *pdev_vts,
		struct platform_device *pdev_vts_dma, unsigned int id);
extern void vts_set_dmicctrl(struct platform_device *pdev, bool enable);
#endif /* __SND_SOC_VTS_H */
