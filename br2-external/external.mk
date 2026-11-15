# SPDX-License-Identifier: MIT
# Pulls in the make fragment of every local package in this external tree.
include $(sort $(wildcard $(BR2_EXTERNAL_HVAC_PATH)/package/*/*.mk))
