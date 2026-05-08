idf_component_register(
    SRCS "acs712.c"
    INCLUDE_DIRS "."
    REQUIRES bt nvs_flash driver 
)
