load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module", "oplus_ddk_get_kernel_version", "oplus_ddk_get_target", "oplus_ddk_get_variant", "bazel_support_platform")
load("//build/kernel/oplus:oplus_modules_dist.bzl", "ddk_copy_to_dist_dir")

def version_compare(v1, v2):
    v1_parts = [int(x) for x in v1.split(".")]
    v2_parts = [int(x) for x in v2.split(".")]
    return v1_parts >= v2_parts

def define_oplus_local_modules():
    kernel_version = oplus_ddk_get_kernel_version()
    target = oplus_ddk_get_target()
    variant  = oplus_ddk_get_variant()
    kernel_build_variant = "{}_{}".format(target, variant)

    if bazel_support_platform == "mtk" :
        if version_compare(kernel_version, "6.12") :
            oplus_fp_ko_deps = [
                "//kernel_device_modules-{}/drivers/base/touchpanel_notify:oplus_bsp_tp_notify".format(kernel_version),
                "//kernel_device_modules-{}/drivers/gpu/drm/mediatek/mediatek_v2:mtk_disp_notify".format(kernel_version),
                "//kernel_device_modules-{}/drivers/spi:spi-mt65xx".format(kernel_version),
            ]
            oplus_fp_copts =[
                "-I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2/",
            ]
        else :
            oplus_fp_ko_deps =[]
            oplus_fp_copts =[]
    else :
        oplus_fp_ko_deps =select({
                "//build/kernel/kleaf:socrepo_true": [
                    "//vendor/oplus/kernel/touchpanel/touchpanel_notify/bazel:oplus_bsp_tp_notify",
                    "//soc-repo:{}/drivers/soc/qcom/panel_event_notifier".format(kernel_build_variant),
                ],
                "//build/kernel/kleaf:socrepo_false": [],
            })
        oplus_fp_copts =[]

    define_oplus_ddk_module(
        name = "oplus_bsp_uff_fp_driver",
        srcs = native.glob([
            "**/*.h",
            "*.h",
            "fp_driver.c",
            "fp_platform.c",
            "fingerprint_event.c",
            "fp_health.c",
            "fp_netlink.c",
            "fp_fault_inject.c",
        ]),
        ko_deps = oplus_fp_ko_deps,
        copts = oplus_fp_copts,
        includes = ["."],
        conditional_defines = {
            "qcom":  ["QCOM_PLATFORM"],
            "mtk":   ["MTK_PLATFORM"],
        },
        local_defines = ["CONFIG_OPLUS_FINGERPRINT_GKI_ENABLE","CONFIG_TOUCHPANEL_NOTIFY"],
        header_deps = [
            "//vendor/oplus/kernel/touchpanel/oplus_touchscreen_v2:config_headers",
        ],
    )

    ddk_copy_to_dist_dir(
        name = "oplus_bsp_fp",
        module_list = [
            "oplus_bsp_uff_fp_driver",
        ],
    )
