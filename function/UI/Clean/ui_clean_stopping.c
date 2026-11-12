#include "ui_clean_internal.h"

/* ui_update Screen7；子控件坐标换算到共用屏幕，Clean 保持禁用。 */
const ui_image_config_t ui_cl_stopping_images[UI_CL_STOPPING_IMAGE_COUNT] = {
    CL_IMG(ui_img_cl_448x182_png_lz4, 9, 0),
    CL_IMG(ui_img_cl_67668799_lz4, 13, -176),
    CL_IMG(ui_img_normal_png_lz4, -123, 0),
};

const ui_label_config_t ui_cl_stopping_labels[UI_CL_STOPPING_LABEL_COUNT] = {
    CL_LABEL("Stopping...", 200, -208, 0xFFFFFF, 22),
    CL_LABEL("Please wait", 104, -106, 0xF2BE9F, 22),
    CL_LABEL("keep parts in place", 91, -106, 0xFFFFFF, 18),
    CL_LABEL("Clean", -84, -11, 0xFFFFFF, 20),
};
