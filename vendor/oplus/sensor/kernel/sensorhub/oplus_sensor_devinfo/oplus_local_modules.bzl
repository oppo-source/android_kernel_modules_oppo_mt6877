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

    if version_compare(kernel_version, "6.12") :
        oplus_mtk_copts = [
            "-I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/sensor/2.0/core",
        ]
        oplus_mtk_ko_deps = [
            "//kernel_device_modules-{}/drivers/misc/mediatek/sensor/2.0/core:hf_manager".format(kernel_version),
            "//kernel_device_modules-{}/drivers/misc/mediatek/sensor/2.0/sensorhub:sensorhub".format(kernel_version),
            "//kernel_device_modules-{}/drivers/gpu/drm/mediatek/mediatek_v2:mtk_disp_notify".format(kernel_version),
            "//kernel_device_modules-{}/drivers/misc/mediatek/scp/rv:scp".format(kernel_version),
            "//kernel_device_modules-{}/drivers/gpu/drm/mediatek/mediatek_v2:mediatek-drm".format(kernel_version),
            "//kernel_device_modules-{}/drivers/gpu/drm/mediatek/mediatek_v2/v1:mediatek_drm_v1".format(kernel_version),
        ]
        oplus_mtk_header_deps = [
            "//kernel_device_modules-{}/drivers/misc/mediatek/scp/include:scp_public_headers".format(kernel_version),
            "//kernel_device_modules-{}/drivers/misc/mediatek/sensor/2.0/sensorhub:ddk_public_headers".format(kernel_version),
            "//kernel_device_modules-{}/drivers/gpu/drm/mediatek/mediatek_v2:ddk_public_headers".format(kernel_version),
            "//kernel_device_modules-{}/drivers/gpu/drm/mediatek/mediatek_v2/v1:ddk_public_headers".format(kernel_version),
        ]
    else :
        oplus_mtk_copts = []
        oplus_mtk_ko_deps = []
        oplus_mtk_header_deps = []

    define_oplus_ddk_module(
        name = "oplus_sensor_deviceinfo",
        srcs = native.glob([
            "*.h",
            "*.c",
        ]),
        includes = ["."],
        copts = oplus_mtk_copts,
        ko_deps = oplus_mtk_ko_deps,
        header_deps = oplus_mtk_header_deps,
        local_defines = ["CONFIG_OPLUS_SENSOR_MTK68XX"],
        conditional_defines = {
            "mtk":  ["CONFIG_OPLUS_SENSOR_USE_BLANK_MODE",
                     "CONFIG_OPLUS_SENSOR_USE_SCREENSHOT_INFO"],
        },
    )

    ddk_copy_to_dist_dir(
        name = "oplus_sensor_deviceinfo",
        module_list = [
            "oplus_sensor_deviceinfo",
        ],
    )
