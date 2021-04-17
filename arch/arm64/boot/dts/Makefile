# SPDX-License-Identifier: GPL-2.0
dtb-y += exynos/dtbo/exynos7885.dtb

dtb-$(CONFIG_MACH_EXYNOS7885_A10_OPEN) := \
	exynos/dtbo/exynos7884B-a10_eur_open_00.dtbo \
	exynos/dtbo/exynos7884B-a10_eur_open_01.dtbo \
	exynos/dtbo/exynos7884B-a10_kor_open_00.dtbo \
	exynos/dtbo/exynos7884B-a10_kor_open_03.dtbo \
	exynos/dtbo/exynos7884B-a10_kor_open_05.dtbo \
	exynos/dtbo/exynos7884B-a10_sea_open_00.dtbo \
	exynos/dtbo/exynos7884B-a10_swa_open_00.dtbo

dtb-$(CONFIG_MACH_EXYNOS7885_A20e_OPEN) := \
	exynos/dtbo/exynos7884B-a20e_eur_open_00.dtbo \
	exynos/dtbo/exynos7884B-a20e_eur_open_03.dtbo

always         := $(dtb-y) $(dtbo-y)
clean-files    := *.dtb *.dtbo
