################################################################################
#
# hvacd: the thermostat daemon, built with CMake from the repository's thermostat/ folder
#
################################################################################

HVACD_VERSION = 1.0
HVACD_SITE = $(BR2_EXTERNAL_HVAC_PATH)/../thermostat
HVACD_SITE_METHOD = local
HVACD_LICENSE = MIT
HVACD_DEPENDENCIES = host-pkgconf libmodbus
# Unit tests and the simulator run on the host (make test-host), not on the board.
HVACD_CONF_OPTS = -DHVAC_BUILD_TESTS=OFF

# Default configuration: one source file (thermostat/config/hvacd.conf) for the repo and the board.
define HVACD_INSTALL_DEFAULT_CONFIG
	$(INSTALL) -D -m 0644 $(@D)/config/hvacd.conf $(TARGET_DIR)/etc/hvacd.conf
endef
HVACD_POST_INSTALL_TARGET_HOOKS += HVACD_INSTALL_DEFAULT_CONFIG

$(eval $(cmake-package))
