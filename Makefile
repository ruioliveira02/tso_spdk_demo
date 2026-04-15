#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2020 Intel Corporation.
#  Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES
#  All rights reserved.
#

CC := gcc

COMMON_CFLAGS := -Wall -Wextra -g

DPDK_LIB := $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --libs spdk_env_dpdk)
SPDK_EVENT_LIB := $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --libs spdk_event spdk_event_bdev)
SPDK_DPDK_LIB := $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --libs spdk_event spdk_event_bdev spdk_env_dpdk)
SYS_LIB := $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --libs --static spdk_syslibs)

TARGET := entropy_calculator
CSRC := main.c

# Shows how to compile an external application against the SPDK individual shared objects and dpdk shared objects.
alone_shared_iso:
	$(CC) $(COMMON_CFLAGS) -Wl,-rpath=$(SPDK_LIB_DIR),--no-as-needed -o $(TARGET) $(CSRC) \
	$(SPDK_EVENT_LIB) $(DPDK_LIB) $(SYS_LIB)

