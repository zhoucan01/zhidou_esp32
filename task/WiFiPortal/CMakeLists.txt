idf_component_register(SRCS "WiFiPortal.c"
    INCLUDE_DIRS "."
    REQUIRES WiFiConnect OTAService Music Bluetooch esp_http_server esp_timer lwip cjson
    EMBED_TXTFILES "portal.html")
