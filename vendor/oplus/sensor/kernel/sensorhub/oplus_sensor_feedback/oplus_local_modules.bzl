load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module", "oplus_ddk_get_kernel_version", "bazel_support_platform")
load("//build/kernel/oplus:oplus_modules_dist.bzl", "ddk_copy_to_dist_dir")

def version_compare(v1, v2):
    v1_parts = [int(x) for x in v1.split(".")]
    v2_parts = [int(x) for x in v2.split(".")]
    return v1_parts >= v2_parts

def define_oplus_local_modules():
    kernel_version = oplus_ddk_get_kernel_version()

    if version_compare(kernel_version, "6.12") :
        oplus_mtk_copts = [
            "-I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/sensor/2.0/core",
            "-I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/scp/include/",
            "-I$(DEVICE_MODULES_PATH)/drivers/soc/oplus/dft/include/",
        ]
        oplus_mtk_deps = [
            "//kernel_device_modules-{}/drivers/misc/mediatek/sensor/2.0/core:hf_manager".format(kernel_version),
            "//kernel_device_modules-{}/drivers/misc/mediatek/scp/rv:scp".format(kernel_version),
            "//kernel_device_modules-{}/drivers/soc/oplus/dft/bazel:oplus_bsp_dft_kernel_fb".format(kernel_version),
        ]
        oplus_header_deps = [
            "//kernel_device_modules-{}/drivers/misc/mediatek/scp/include:scp_public_headers".format(kernel_version),
            "//kernel_device_modules-{}/drivers/misc/mediatek/scp/rv:ddk_public_headers".format(kernel_version),
            "//kernel_device_modules-{}/drivers/soc/oplus/dft/bazel:oplus_dft_headers".format(kernel_version),
        ]
    else :
        oplus_mtk_deps = []
        oplus_mtk_copts = []
        oplus_header_deps = []

    define_oplus_ddk_module(
        name = "oplus_sensor_feedback",
        srcs = native.glob([
            "*.h",
            "*.c",
        ]),
        includes = ["."],
        local_defines = [
            "CFG_OPLUS_ARCH_IS_MTK",
            "CONFIG_OPLUS_FEATURE_FEEDBACK=1",
        ],
        header_deps = oplus_header_deps,
        copts = oplus_mtk_copts,
        ko_deps = oplus_mtk_deps,
    )

    ddk_copy_to_dist_dir(
        name = "oplus_sensor_feedback",
        module_list = [
            "oplus_sensor_feedback",
        ],
    )
