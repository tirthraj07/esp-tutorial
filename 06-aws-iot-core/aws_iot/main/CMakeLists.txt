idf_component_register(SRCS "aws_iot.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES "root_ca.pem" "client.crt" "client.key")
