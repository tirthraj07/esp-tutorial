idf_component_register(SRCS "device_shadow.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES "root_ca.pem" "client.crt" "client.key")


# --- Environment Variable Injection ---
# Check if the terminal has an environment variable called ESP_NODE_ID
set(ENV_NODE_ID $ENV{ESP_NODE_ID})

if(ENV_NODE_ID)
    message(STATUS "Found ESP_NODE_ID Environment Variable! Injecting NODE_ID: ${ENV_NODE_ID}")
    # Inject it into the C code as a string definition
    target_compile_definitions(${COMPONENT_LIB} PRIVATE NODE_ID=\"${ENV_NODE_ID}\")
else()
    message(WARNING "No ESP_NODE_ID found in environment. Defaulting to ESP_NODE_001")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE NODE_ID=\"ESP_NODE_001\")
endif()