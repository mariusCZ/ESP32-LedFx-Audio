set(component_srcs "main.c" "mcp3202.c")

idf_component_register(SRCS "udpclient.c" "cJSON_Utils.c" "cJSON.c" "network.c" "${component_srcs}"
                       INCLUDE_DIRS ".")
