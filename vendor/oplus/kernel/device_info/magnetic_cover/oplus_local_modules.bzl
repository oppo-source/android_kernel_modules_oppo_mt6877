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
        others_ko_deps = [
            "//vendor/oplus/kernel/device_info/magtransfer:oplus_magcvr_notify",
        ]
    else :
        others_ko_deps = []

    if bazel_support_platform == "qcom" :
        ko_deps = []
        copts = []
    else :
        if version_compare(kernel_version, "6.12") :
            ko_deps = [
                "//kernel_device_modules-{}/drivers/base/magtransfer:oplus_magcvr_notify".format(kernel_version),
            ]
            copts = [
                "-I$(DEVICE_MODULES_PATH)/drivers/base/",
            ]
        else :
            ko_deps = []
            copts = []

    define_oplus_ddk_module(
        name = "oplus_magnetic_cover",
        srcs = native.glob([
            "**/*.h",
            "magcvr_src/abstract/magnetic_cover_core.c",
#            "magcvr_src/transfer/magcvr_notify.c", /* need set intree for other driver used */
        ]),
        includes = ["."],
        local_defines = ["CONFIG_OPLUS_MAGCVR_NOTIFY"],
        conditional_defines = {
#            "qcom":  ["CONFIG_QCOM_PANEL_EVENT_NOTIFIER"],
#            "mtk":  ["CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY"],
        },
        ko_deps = ko_deps + others_ko_deps,
        copts = copts,
    )

    define_oplus_ddk_module(
        name = "oplus_magcvr_ak09973",
        srcs = native.glob([
            "**/*.h",
            "magcvr_src/hardware/magcvr_ak09973.c"
        ]),
        ko_deps = [
            "//vendor/oplus/kernel/device_info/magnetic_cover:oplus_magnetic_cover",
        ],
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_magcvr_mxm1120",
        srcs = native.glob([
            "**/*.h",
            "magcvr_src/hardware/magcvr_mxm1120.c"
        ]),
        ko_deps = [
            "//vendor/oplus/kernel/device_info/magnetic_cover:oplus_magnetic_cover",
        ],
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_magcvr_mkh100a",
        srcs = native.glob([
            "**/*.h",
            "magcvr_src/hardware/magcvr_mkh100a.c"
        ]),
        ko_deps = [
            "//vendor/oplus/kernel/device_info/magnetic_cover:oplus_magnetic_cover",
        ],
        includes = ["."],
    )

    ddk_headers(
        name = "config_headers",
        hdrs  = native.glob([
            "**/*.h",
        ]),
        includes = [".","magcvr_notify"],
    )

    ddk_copy_to_dist_dir(
        name = "oplus_magnetic_cover",
        module_list = [
             "oplus_magnetic_cover",
             "oplus_magcvr_ak09973",
             "oplus_magcvr_mxm1120",
             "oplus_magcvr_mkh100a",
        ],
    )
