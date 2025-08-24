idf_component_register(
    SRCS "src/HttpServer.cpp"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_http_server
)
