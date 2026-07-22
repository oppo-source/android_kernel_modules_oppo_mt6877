load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module", "oplus_ddk_get_kernel_version", "oplus_ddk_get_target", "oplus_ddk_get_variant", "bazel_support_platform")
load("//build/kernel/oplus:oplus_modules_dist.bzl", "ddk_copy_to_dist_dir")

def version_compare(v1, v2):
    v1_parts = [int(x) for x in v1.split(".")]
    v2_parts = [int(x) for x in v2.split(".")]
    return v1_parts >= v2_parts

def define_oplus_local_modules():
    target = oplus_ddk_get_target()
    variant  = oplus_ddk_get_variant()
    kernel_build_variant = "{}_{}".format(target, variant)
    kernel_version = oplus_ddk_get_kernel_version()
    bazel_support_target = oplus_ddk_get_target()

    if bazel_support_platform == "qcom" :
        if bazel_support_target == "canoe" :
            ko_deps = [
                "//soc-repo:{}/drivers/soc/qcom/panel_event_notifier".format(kernel_build_variant),
                "//vendor/oplus/kernel/dft/bazel:oplus_bsp_dft_kernel_fb",
            ]
            copts = []
        else :
            ko_deps = []
            copts = []
    else :
        if version_compare(kernel_version, "6.12") :
            ko_deps = [
                "//kernel_device_modules-{}/drivers/gpu/drm/mediatek/mediatek_v2:mtk_disp_notify".format(kernel_version),
                "//kernel_device_modules-{}/drivers/gpu/drm/mediatek/mediatek_v2:mtk_panel_ext".format(kernel_version),
            ]
            copts = [
                "-I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/",
                "-I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2/",
            ]
        else :
            ko_deps = []
            copts = []

    define_oplus_ddk_module(
        name = "oplus_bsp_game_press",
        srcs = native.glob([
            "*.h",
            "cs_press_f71.c"
        ]),
        ko_deps = ko_deps,
        includes = ["."],
        conditional_defines = {
            "mtk":  ["CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY", "CONFIG_OPLUS_DEVICE_INFO_MTK_PLATFORM"],
            "qcom":  ["CONFIG_QCOM_PANEL_EVENT_NOTIFIER"],
        },
        copts = copts,
    )
    ddk_copy_to_dist_dir(
        name = "oplus_bsp_game_press",
        module_list = [
            "oplus_bsp_game_press",
        ],
    )