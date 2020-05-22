#
# Copyright (C) 2015-2018,  Netronome Systems, Inc.  All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# @file         apps/lab_template/Makefile
# @brief        Makefile for a template lab
#

# Define src_dir FIRST - it is the directory that this makefile resides in
# MUST OCCUR BEFORE ANY include's (which will change MAKEFILE_LIST)
app_src_dir     := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
ROOT_SRC_DIR  ?= $(realpath $(app_src_dir)/..)
flowenv_dir        := $(ROOT_SRC_DIR)/flowenv

microc_blocks_dir   := $(flowenv_dir)/me/blocks
microc_libs_dir     := $(flowenv_dir)/me/lib
microc_inc_dir      := $(flowenv_dir)/me/include

include Makefile.nfp.config
include Makefile.templates

PKT_NBI_OFFSET ?= 64

# BLM
$(eval $(call microcode.assemble,blm_obj,$(microc_blocks_dir)/blm,blm_main.uc))
$(eval $(call microcode.add_flags,blm_obj,-DBLM_CUSTOM_CONFIG -DSINGLE_NBI -DPKT_NBI_OFFSET=$(PKT_NBI_OFFSET)))
$(eval $(call microcode.add_flags,blm_obj,-DBLM_BLQ_EMEM_TYPE=emem -DNBII=8 -DBLM_INSTANCE_ID=0))
$(eval $(call microcode.add_flags,blm_obj,-DBLM_INIT_EMU_RINGS))
$(eval $(call microcode.add_flags,blm_obj,-DNFP_LIB_ANY_NFAS_VERSION))
$(eval $(call microcode.add_flags,blm_obj,-I$(microc_blocks_dir)/blm))
$(eval $(call microcode.add_flags,blm_obj,-I$(microc_blocks_dir)/blm/_h))
$(eval $(call microcode.add_flags,blm_obj,-I$(microc_blocks_dir)/blm/_uc))


#
# RX Application definition
#
$(eval $(call micro_c.compile_with_rtl,rx_obj,rx.c))
$(eval $(call micro_c.add_fw_libs,rx_obj,nfp pkt std net))
$(eval $(call micro_c.add_flags,rx_obj,-I$(ROOT_SRC_DIR)))
$(eval $(call micro_c.add_flags,rx_obj,-I$(microc_blocks_dir)/blm))
$(eval $(call micro_c.add_src_lib,rx_obj,libblm,$(microc_blocks_dir)/blm))
$(eval $(call micro_c.force_include,rx_obj,config))

#
# TX Application definition
#
$(eval $(call micro_c.compile_with_rtl,tx_obj,tx.c))
$(eval $(call micro_c.add_fw_libs,tx_obj,nfp pkt std net))
$(eval $(call micro_c.add_flags,tx_obj,-I$(ROOT_SRC_DIR)))
$(eval $(call micro_c.add_flags,tx_obj,-I$(microc_blocks_dir)/blm))
$(eval $(call micro_c.add_src_lib,tx_obj,libblm,$(microc_blocks_dir)/blm))
$(eval $(call micro_c.force_include,tx_obj,config))

#
# Application definition
#
$(eval $(call fw.add_obj,app,rx_obj,i32.me0))
$(eval $(call fw.add_obj,app,tx_obj,i32.me1))
$(eval $(call fw.add_obj,app,blm_obj,i48.me0))
$(eval $(call fw.link_with_rtsyms,app))
$(eval $(call fw.add_ppc,app,i8,$(PICO_CODE)))

#
# Debug
#
-include Makefile.debug
