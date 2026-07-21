load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module", "oplus_ddk_get_kernel_version", "oplus_ddk_get_target", "oplus_ddk_get_variant", "bazel_support_platform")
load("//build/kernel/oplus:oplus_modules_dist.bzl", "ddk_copy_to_dist_dir")

def define_oplus_local_modules():
    target = oplus_ddk_get_target()
    variant  = oplus_ddk_get_variant()
    kernel_build_variant = "{}_{}".format(target, variant)
    kernel_version = oplus_ddk_get_kernel_version()

    if bazel_support_platform == "qcom" :
        ko_deps = []
        copts = []
    else :
        ko_deps = [
            "//kernel_device_modules-{}/drivers/gpu/drm/mediatek/mediatek_v2:mtk_disp_notify".format(kernel_version),
            "//kernel_device_modules-{}/drivers/gpu/drm/mediatek/mediatek_v2:mtk_panel_ext".format(kernel_version),
        ]
        copts = [
            "-I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/",
            "-I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2/",
        ]

    define_oplus_ddk_module(
        name = "oplus_bsp_cs_press_f71",
        srcs = native.glob([
            "*.h",
            "cs_press_f71.c"
        ]),
        ko_deps = ko_deps,
        includes = ["."],
        conditional_defines = {
            "mtk":  ["CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY", "CONFIG_OPLUS_DEVICE_INFO_MTK_PLATFORM"],
        },
        copts = copts,
    )
    ddk_copy_to_dist_dir(
        name = "oplus_bsp_cs_press_f71",
        module_list = [
            "oplus_bsp_cs_press_f71",
        ],
    )