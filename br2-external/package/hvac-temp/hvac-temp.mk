################################################################################
#
# hvac-temp: the hvac_temp kernel module, built from the repository's driver/ folder
#
################################################################################

HVAC_TEMP_VERSION = 1.0
HVAC_TEMP_SITE = $(BR2_EXTERNAL_HVAC_PATH)/../driver
HVAC_TEMP_SITE_METHOD = local
HVAC_TEMP_LICENSE = GPL-2.0-only

$(eval $(kernel-module))
$(eval $(generic-package))
