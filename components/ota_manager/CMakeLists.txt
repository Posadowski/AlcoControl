idf_component_register(
    SRCS "src/ota_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_event nvs_flash esp_http_client esp_https_ota app_update
)
