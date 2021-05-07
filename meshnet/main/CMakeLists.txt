idf_component_register(SRCS "quick_meshnet.c"
                    INCLUDE_DIRS "."
                    REQUIRES mcommon mconfig mwifi mlink mesh_mqtt_handle
)