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

    if bazel_support_target == "canoe" :
        oplusboot_ko_deps = [
            "//vendor/oplus/kernel/boot:oplusboot",
            "//vendor/oplus/kernel/boot:oplus_bsp_bootmode",
        ]
        oplus_bsp_boot_projectinfo_ko_deps = [
            "//vendor/oplus/kernel/boot:oplus_bsp_boot_projectinfo",
        ]
        panel_event_notifier_ko_deps = [
            "//soc-repo:{}/drivers/soc/qcom/panel_event_notifier".format(kernel_build_variant),
        ]
        tp_others_ko_deps = [
            "//vendor/oplus/kernel/device_info/device_info/bazel:device_info",
            "//vendor/oplus/kernel/touchpanel/touchpanel_notify/bazel:oplus_bsp_tp_notify",
            "//vendor/oplus/kernel/touchpanel/kernelFwUpdate/bazel:oplus_bsp_fw_update",
        ]
    else :
        oplusboot_ko_deps = []
        oplus_bsp_boot_projectinfo_ko_deps = []
        panel_event_notifier_ko_deps = []
        tp_others_ko_deps = []

    if bazel_support_platform == "qcom" :
        ko_deps = [
            "//vendor/oplus/kernel/touchpanel/oplus_touchscreen_v2:oplus_bsp_tp_custom",
        ]
        copts = []
    else :
        if version_compare(kernel_version, "6.12") :
            ko_deps = [
                "//vendor/oplus/kernel/touchpanel/oplus_touchscreen_v2:oplus_bsp_tp_custom",
                "//kernel_device_modules-{}/drivers/gpu/drm/mediatek/mediatek_v2:mtk_disp_notify".format(kernel_version),
                "//kernel_device_modules-{}/drivers/gpu/drm/mediatek/mediatek_v2:mtk_panel_ext".format(kernel_version),
                "//kernel_device_modules-{}/drivers/soc/oplus/boot:oplus_bsp_boot_projectinfo".format(kernel_version),
                "//kernel_device_modules-{}/drivers/misc/mediatek/boot_common:mtk_boot_common".format(kernel_version),
                "//kernel_device_modules-{}/drivers/soc/oplus/boot:oplusboot".format(kernel_version),
                "//kernel_device_modules-{}/drivers/soc/oplus/device_info:device_info".format(kernel_version),
                "//kernel_device_modules-{}/drivers/base/kernelFwUpdate:oplus_bsp_fw_update".format(kernel_version),
                "//kernel_device_modules-{}/drivers/base/touchpanel_notify:oplus_bsp_tp_notify".format(kernel_version),
            ]
            copts = [
                "-I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/",
                "-I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2/",
            ]
        else :
            ko_deps = [
                "//vendor/oplus/kernel/touchpanel/oplus_touchscreen_v2:oplus_bsp_tp_custom",
            ]
            copts = []

    define_oplus_ddk_module(
        name = "oplus_bsp_synaptics_tcm2",
        srcs = native.glob([
            "**/*.h",
            "syna_tcm2.c",
            "tcm/synaptics_touchcom_core_v1.c",
            "tcm/synaptics_touchcom_core_v2.c",
            "tcm/synaptics_touchcom_func_base.c",
            "tcm/synaptics_touchcom_func_touch.c",
            "tcm/synaptics_touchcom_func_reflash.c",
            "tcm/synaptics_touchcom_func_romboot.c",
            "syna_tcm2_platform_spi.c",
            "syna_tcm2_sysfs.c",
            "syna_tcm2_testing.c",
            "synaptics_common.c",
            "touchpanel_proc.c",
            "touch_comon_api/touch_comon_api.c",
            "touchpanel_autotest/touchpanel_autotest.c",
            "touchpanel_healthinfo/touchpanel_healthinfo.c",
            "touchpanel_healthinfo/touchpanel_exception.c",
        ]),
        copts = copts,
        ko_deps = ko_deps + oplusboot_ko_deps + oplus_bsp_boot_projectinfo_ko_deps + panel_event_notifier_ko_deps + tp_others_ko_deps,
        includes = ["."],
        local_defines = [
			"CONFIG_TOUCHPANEL_NOTIFY",
			"CONFIG_TOUCHPANEL_OPLUS_MODULE",
			"CONFIG_OF",
			"BUILD_BY_BAZEL",
		],
        conditional_defines = {
            "mtk":  ["CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY", "CONFIG_TOUCHPANEL_MTK_PLATFORM"],
            "qcom":  ["CONFIG_QCOM_PANEL_EVENT_NOTIFIER"],
        },
        header_deps = [
            "//vendor/oplus/kernel/touchpanel/oplus_touchscreen_v2:config_headers",
        ],
    )

    ddk_copy_to_dist_dir(
        name = "oplus_bsp_synaptics_tcm2",
        module_list = [
            "oplus_bsp_synaptics_tcm2",
        ],
    )
