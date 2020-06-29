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
ROOT_SRC_DIR  := $(realpath $(app_src_dir)/..)
flowenv_dir        := $(ROOT_SRC_DIR)/flowenv

microc_blocks_dir   := $(flowenv_dir)/me/blocks
microc_libs_dir     := $(flowenv_dir)/me/lib
microc_inc_dir      := $(flowenv_dir)/me/include

include Makefile.nfp.config
include Makefile.templates

PKT_NBI_OFFSET ?= 64

#
# MMIO Application definition
#
$(eval $(call micro_c.compile_with_rtl,mmio_obj,mmio.c))
$(eval $(call micro_c.add_fw_libs,mmio_obj,nfp pkt std net))
$(eval $(call micro_c.add_flags,mmio_obj,-I$(ROOT_SRC_DIR)))
$(eval $(call micro_c.add_flags,mmio_obj,-I$(microc_blocks_dir)/blm))
$(eval $(call micro_c.add_src_lib,mmio_obj,libblm,$(microc_blocks_dir)/blm))
$(eval $(call micro_c.force_include,mmio_obj,config))

#
# MMIO Perf Application definition
#
$(eval $(call fw.add_obj,mmio_perf,mmio_obj,i32.me0))
$(eval $(call fw.link_with_rtsyms,mmio_perf))

#
# Debug
#
-include Makefile.debug
