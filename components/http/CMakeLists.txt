idf_component_register(
    SRCS "src/HttpServer.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server app_core
)
