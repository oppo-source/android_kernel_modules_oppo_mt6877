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
            "-I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include",
            "-I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/scp/include/",
        ]
        oplus_mtk_deps = [
            "//kernel_device_modules-{}/drivers/misc/mediatek/pwm:mtk-pwm".format(kernel_version),
            "//kernel_device_modules-{}/drivers/misc/mediatek/sensor/2.0/core:hf_manager".format(kernel_version),
            "//kernel_device_modules-{}/drivers/misc/mediatek/scp/rv:scp".format(kernel_version),
        ]
        oplus_header_deps = [
            "//kernel_device_modules-{}/drivers/misc/mediatek/scp/include:scp_public_headers".format(kernel_version),
            "//kernel_device_modules-{}/drivers/misc/mediatek/scp/rv:ddk_public_headers".format(kernel_version),
        ]
    else :
        oplus_mtk_deps = []
        oplus_mtk_copts = []
        oplus_header_deps = []

    define_oplus_ddk_module(
        name = "oplus_sensor_kookong_ir_pwm",
        srcs = native.glob([
            "**/*.h",
            "oplus_consumer_ir/oplus_ir_pwm.c",
        ]),
        includes = ["oplus_consumer_ir"],
        copts = oplus_mtk_copts,
        ko_deps = [
            "//vendor/oplus/sensor/kernel/sensorhub:oplus_sensor_ir_core",
        ] + oplus_mtk_deps,
        local_defines = [],
        out = "oplus_sensor_kookong_ir_pwm.ko",
        conditional_defines = {
            "mtk":  ["CONFIG_OPLUS_SENSOR_CONSUMER_IR_MTK"],
        },
    )

    define_oplus_ddk_module(
        name = "oplus_sensor_ir_core",
        srcs = native.glob([
            "**/*.h",
            "oplus_consumer_ir/oplus_ir_core.c",
        ]),
        includes = ["oplus_consumer_ir"],
        local_defines = [],
        out = "oplus_sensor_ir_core.ko",
    )

    define_oplus_ddk_module(
        name = "pseudo_sensor",
        srcs = native.glob([
            "**/*.h",
            "pseudo-sensor/pseudo_sensor.c",
        ]),
        includes = [],
        local_defines = ["CFG_OPLUS_ARCH_IS_MTK"],
        header_deps = oplus_header_deps,
        copts = oplus_mtk_copts,
        ko_deps = oplus_mtk_deps,
        conditional_build = {
            "OPLUS_FEATURE_BSP_DRV_INJECT_TEST": "1",
        },
    )

    ddk_copy_to_dist_dir(
        name = "oplus_sensor_consumer_ir",
        module_list = [
            "oplus_sensor_ir_core",
            "oplus_sensor_kookong_ir_pwm",
            "pseudo_sensor",
        ],
        conditional_builds = {
            "pseudo_sensor": {
                "OPLUS_FEATURE_BSP_DRV_INJECT_TEST": "1",
            },
        },
    )
