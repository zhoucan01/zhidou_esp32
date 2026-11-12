if(CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_register(REQUIRES lvgl LZ4 DataCenter Music)
    return()
endif()

file(GLOB UI_IMAGE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/images/*.c")
list(SORT UI_IMAGE_SOURCES)

file(GLOB UI_LZ4_IMAGE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/images_lz4/*.c")
list(SORT UI_LZ4_IMAGE_SOURCES)

file(GLOB UI_FONT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Font/*.c")
list(SORT UI_FONT_SOURCES)

idf_component_register(
    SRCS
        "ui.c"
        "ui_lz4_decoder.c"
        "Tofu/ui_home.c"
        "Tofu/ui_tofu_firmness.c"
        "Tofu/ui_firm_tofu.c"
        "Tofu/ui_more_options.c"
        "Tofu/ui_add_ingredients.c"
        "Tofu/ui_machine_check.c"
        "Tofu/ui_press_parts_check.c"
        "Tofu/ui_tofu_making.c"
        "Tofu/ui_tofu_ready.c"
        "Tofu/ui_stop_tofu.c"
        "Tofu/ui_tofu_stopping.c"
        "Tofu/ui_tofu_stopped.c"
        "PlantMilk/ui_plantmilk.c"
        "PlantMilk/ui_plantmilk_select.c"
        "PlantMilk/ui_plantmilk_options.c"
        "PlantMilk/ui_plantmilk_add_ingredients.c"
        "PlantMilk/ui_plantmilk_machine_check.c"
        "PlantMilk/ui_plantmilk_making.c"
        "PlantMilk/ui_plantmilk_ready.c"
        "PlantMilk/ui_plantmilk_ready_no_filter.c"
        "PlantMilk/ui_plantmilk_stop_confirm.c"
        "PlantMilk/ui_plantmilk_stopping.c"
        "PlantMilk/ui_plantmilk_stopped.c"
        "Clean/ui_clean.c"
        "Clean/ui_clean_stopping.c"
        ${UI_FONT_SOURCES}
        ${UI_IMAGE_SOURCES}
        ${UI_LZ4_IMAGE_SOURCES}

    INCLUDE_DIRS
        "."

    REQUIRES
        lvgl
        LZ4
        DataCenter
        Music
)
