idf_component_register(SRCS "remote_control.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES "root_ca.pem" "client.crt" "client.key")
