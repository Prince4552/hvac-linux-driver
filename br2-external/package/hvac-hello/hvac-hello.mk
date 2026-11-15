################################################################################
#
# hvac-hello: minimal learning module from examples/hello_module
#
################################################################################

HVAC_HELLO_VERSION = 1.0
HVAC_HELLO_SITE = $(BR2_EXTERNAL_HVAC_PATH)/../examples/hello_module
HVAC_HELLO_SITE_METHOD = local
HVAC_HELLO_LICENSE = GPL-2.0-only

$(eval $(kernel-module))
$(eval $(generic-package))
