idf_component_register(
    SRCS "app_main.cpp" "control_task.cpp" "services.cpp" "shared.cpp" "nvs_store.cpp"
    LDFRAGMENTS "linker.lf"  # the control tick in IRAM, see the file
    PRIV_REQUIRES direcsun_core direcsun_drivers direcsun_comms
                  esp_app_format spi_flash nvs_flash esp_driver_gptimer esp_timer
)
