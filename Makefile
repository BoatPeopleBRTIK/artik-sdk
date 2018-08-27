############################################################################
# external/artik-sdk/Makefile
#
#   Copyright (C) 2007, 2008, 2011-2015 Gregory Nutt. All rights reserved.
#   Author: Gregory Nutt <gnutt@nuttx.org>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the
#    distribution.
# 3. Neither the name NuttX nor the names of its contributors may be
#    used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
# COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
# OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
# AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
# ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
############################################################################


#EXTDIR = ${shell pwd}
-include $(TOPDIR)/Make.defs

DELIM ?= $(strip /)

DEPPATH = --dep-path .
ASRCS =
CSRCS =
VPATH = .

ARTIK_SDK_DIR = $(TOPDIR)/../external/artik-sdk
CFLAGS += -DLIB_VERSION_MAJOR=1
CFLAGS += -DLIB_VERSION_MINOR=8
CFLAGS += -DLIB_VERSION_PATCH=2
CFLAGS += -DEXPORT_API=
CFLAGS += -DCONFIG_RELEASE=
CFLAGS += -I$(ARTIK_SDK_DIR)/inc/base
CFLAGS += -I$(ARTIK_SDK_DIR)/inc/systemio
CFLAGS += -I$(ARTIK_SDK_DIR)/inc/sensor
CFLAGS += -I$(ARTIK_SDK_DIR)/inc/wifi
CFLAGS += -I$(ARTIK_SDK_DIR)/inc/connectivity
CFLAGS += -I$(ARTIK_SDK_DIR)/inc/lwm2m
CFLAGS += -I$(ARTIK_SDK_DIR)/inc/mqtt
CFLAGS += -I$(TOPDIR)/../apps/include/netutils
CFLAGS += -D__TIZENRT__

CSRCS += $(ARTIK_SDK_DIR)/src/modules/base/log/artik_log.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/base/log/tizenrt_log.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/base/module/artik_module.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/base/module/tizenrt_module.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/base/time/artik_time.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/base/time/tizenrt_time.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/base/security/artik_security.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/base/security/tizenrt_security.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/base/security/tizenrt/mbedtls_pkcs7_parser.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/base/utils/artik_utils.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/base/utils/tizenrt_utils.c

CSRCS += $(ARTIK_SDK_DIR)/src/modules/systemio/adc/artik_adc.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/systemio/adc/tizenrt_adc.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/systemio/gpio/artik_gpio.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/systemio/gpio/tizenrt_gpio.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/systemio/i2c/artik_i2c.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/systemio/i2c/tizenrt_i2c.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/systemio/pwm/artik_pwm.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/systemio/pwm/tizenrt_pwm.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/systemio/spi/artik_spi.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/systemio/spi/tizenrt_spi.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/systemio/serial/artik_serial.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/systemio/serial/tizenrt_serial.c

CSRCS += $(ARTIK_SDK_DIR)/src/modules/connectivity/network/artik_network.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/connectivity/network/common_network.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/connectivity/network/tizenrt_network.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/connectivity/network/tizenrt/dns_utils.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/connectivity/http/artik_http.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/connectivity/http/common_http.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/connectivity/http/tizenrt_http.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/connectivity/http/tizenrt/webclient.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/connectivity/websocket/artik_websocket.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/connectivity/websocket/tizenrt_websocket.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/connectivity/cloud/artik_cloud.c

CSRCS += $(ARTIK_SDK_DIR)/src/modules/wifi/artik_wifi.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/wifi/tizenrt/tizenrt_wifi.c

CFLAGS += -I$(TOPDIR)/../apps/netutils/mqtt/lib
CSRCS += $(ARTIK_SDK_DIR)/src/modules/mqtt/artik_mqtt.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/mqtt/os_mqtt.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/mqtt/tizenrt/mqtt_client.c

CFLAGS += -I$(TOPDIR)/../external/wakaama-client/lwm2mclient
CSRCS += $(ARTIK_SDK_DIR)/src/modules/lwm2m/artik_lwm2m.c
CSRCS += $(ARTIK_SDK_DIR)/src/modules/lwm2m/tizenrt_lwm2m.c

AOBJS = $(ASRCS:.S=$(OBJEXT))
COBJS = $(CSRCS:.c=$(OBJEXT))

SRCS = $(ASRCS) $(CSRCS)
OBJS = $(AOBJS) $(COBJS)

BIN	= libartik-sdk$(LIBEXT)

all:	$(BIN)

$(AOBJS): %$(OBJEXT): %.S
	$(call ASSEMBLE, $<, $@)

$(COBJS): %$(OBJEXT): %.c
	$(call COMPILE, $<, $@)

$(BIN): $(OBJS)
	$(call ARCHIVE, $@, $(OBJS))

.depend: Makefile $(SRCS)
	$(Q) $(MKDEP) $(DEPPATH) "$(CC)" -- $(CFLAGS) -- $(SRCS) >Make.dep
	$(Q) touch $@

depend: .depend

custom_clean:
	$(foreach OBJFILE, $(OBJS), rm -f $(OBJFILE))

clean: custom_clean
	$(call DELFILE, $(BIN))
	$(call CLEAN)

distclean: clean
	$(call DELFILE, Make.dep)
	$(call DELFILE, .depend)

-include Make.dep
