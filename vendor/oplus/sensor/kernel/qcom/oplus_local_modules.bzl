load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module", "oplus_ddk_get_target", "oplus_ddk_get_variant")
load("//build/kernel/oplus:oplus_modules_dist.bzl", "ddk_copy_to_dist_dir")

def define_oplus_local_modules():
    target = oplus_ddk_get_target()
    variant  = oplus_ddk_get_variant()
    kernel_build_variant = "{}_{}".format(target, variant)
    bazel_support_target = oplus_ddk_get_target()

    if bazel_support_target == "canoe" :
        oplus_bsp_boot_projectinfo_ko_deps = [
            "//vendor/oplus/kernel/boot:oplus_bsp_boot_projectinfo",
        ]
        oplus_bsp_kfb_ko_deps = [
            "//vendor/oplus/kernel/dft/bazel:oplus_bsp_dft_kernel_fb",
        ]
        smem_ko_deps = [
            "//soc-repo:{}/drivers/soc/qcom/smem".format(kernel_build_variant),
        ]
        panel_event_notifier_ko_deps = [
            "//soc-repo:{}/drivers/soc/qcom/panel_event_notifier".format(kernel_build_variant),
        ]
        qmi_ko_deps = [
            "//soc-repo:{}/drivers/soc/qcom/qmi_helpers".format(kernel_build_variant),
        ]
    else :
        oplus_bsp_boot_projectinfo_ko_deps = []
        oplus_bsp_kfb_ko_deps = []
        smem_ko_deps = []
        panel_event_notifier_ko_deps = []
        qmi_ko_deps = []

    define_oplus_ddk_module(
        name = "oplus_sensor_ir_core",
        srcs = native.glob([
            "**/*.h",
            "oplus_consumer_ir/oplus_ir_core.c",
        ]),
        includes = ["oplus_consumer_ir"],
    )

    define_oplus_ddk_module(
        name = "oplus_sensor_kookong_ir_spi",
        srcs = native.glob([
            "**/*.h",
            "oplus_consumer_ir/oplus_ir_spi.c",
        ]),
        includes = ["oplus_consumer_ir"],
        ko_deps = [
            "//vendor/oplus/sensor/kernel/qcom:oplus_sensor_ir_core",
        ] + smem_ko_deps,
    )

    define_oplus_ddk_module(
        name = "oplus_sensor_deviceinfo",
        srcs = native.glob([
            "**/*.h",
            "sensor/oplus_sensor_devinfo.c",
            "sensor/oplus_press_cali_info.c",
            "sensor/oplus_pad_als_info.c",
        ]),
        includes = ["."],
        ko_deps = oplus_bsp_boot_projectinfo_ko_deps + smem_ko_deps,
        conditional_defines = {
            "mtk":  ["CONFIG_OPLUS_SYSTEM_KERNEL_MTK"],
            "qcom": ["CONFIG_OPLUS_SYSTEM_KERNEL_QCOM"],
        },
    )

    define_oplus_ddk_module(
        name = "oplus_sensor_interact",
        srcs = native.glob([
            "**/*.h",
            "sensor/oplus_ssc_interact/oplus_ssc_interact.c",
        ]),
        includes = ["."],
        local_defines = ["CONFIG_OPLUS_SENSOR_FB_QC",
                         "CONFIG_OPLUS_SENSOR_DRM_PANEL_NOTIFY",
                         "CONFIG_OPLUS_SENSOR_DRM_PANEL_ADFR_MIN_FPS",
                         "CONFIG_OPLUS_SENSOR_USE_SCREENSHOT_INFO",
                         "OPLUS_FEATURE_DISPLAY"],
        ko_deps = [
            "//vendor/oplus/sensor/kernel/qcom:oplus_sensor_feedback",
        ] + panel_event_notifier_ko_deps,
    )

    define_oplus_ddk_module(
        name = "oplus_sensor_feedback",
        srcs = native.glob([
            "**/*.h",
            "sensor/oplus_sensor_feedback/sensor_feedback.c",
        ]),
        includes = ["."],
        local_defines = ["CFG_OPLUS_ARCH_IS_QCOM",
                         "CONFIG_OPLUS_SENSOR_DRM_PANEL_NOTIFY"],
        ko_deps = smem_ko_deps + oplus_bsp_kfb_ko_deps,
    )

    define_oplus_ddk_module(
        name = "pseudo_sensor",
        srcs = native.glob([
            "**/*.h",
            "pseudo-sensor/pseudo_sensor.c",
        ]),
        includes = [],
        local_defines = ["CFG_OPLUS_ARCH_IS_QCOM"],
        conditional_build = {
            "OPLUS_FEATURE_BSP_DRV_INJECT_TEST": "1",
        },
        ko_deps = smem_ko_deps + qmi_ko_deps,
    )

    ddk_copy_to_dist_dir(
        name = "oplus_bsp_sensor",
        module_list = [
            "oplus_sensor_ir_core",
            "oplus_sensor_kookong_ir_spi",
            "oplus_sensor_deviceinfo",
            "oplus_sensor_interact",
            "oplus_sensor_feedback",
            "pseudo_sensor",
        ],
        conditional_builds = {
            "pseudo_sensor": {
                "OPLUS_FEATURE_BSP_DRV_INJECT_TEST": "1",
            },
        },
    )
