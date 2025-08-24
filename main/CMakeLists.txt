idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES sensors ds18b20 onewiretemp wifi http
)
