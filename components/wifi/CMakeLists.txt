idf_component_register(
    SRCS "src/WifiManager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_event esp_wifi nvs_flash
)
