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
    tv = "{}_{}".format(target, variant)

    define_oplus_ddk_module(
        name = "oplus_network_nfc_sn_ese",
        srcs = native.glob([
            "**/*.h",
            "sn_nci/ese/p73.c",
        ]),
        includes = ["."],
        ko_deps = [
            "//vendor/oplus/kernel/nfc:oplus_network_nfc_i2c",
        ],
    )
    define_oplus_ddk_module(
        name = "oplus_network_nfc_pn557_i2c",
        srcs = native.glob([
            "**/*.h",
            "nq330/nq330.c",
        ]),
        includes = ["."],
        copts = [
            "-DCONFIG_NFC_PN553_DEVICES",
        ],
    )

    if version_compare(kernel_version, "6.12") :
        if bazel_support_platform == "mtk":
            ko_deps = [
                     "//kernel_device_modules-{}/drivers/misc/mediatek/boot_common:mtk_boot_common".format(kernel_version),
            ]
        else :
            ko_deps = [
                     "//vendor/oplus/kernel/boot:oplus_bsp_bootmode",
                     "//soc-repo:{}/drivers/pinctrl/qcom/pinctrl-msm".format(tv),
            ]
    else :
       ko_deps = []

    define_oplus_ddk_module(
        name = "oplus_network_nfc_i2c",
        srcs = native.glob([
            "**/*.h",
            "sn_nci/nfc/common.c",
            "sn_nci/nfc/common_ese.c",
            "sn_nci/nfc/i2c_drv.c",
        ]),
        includes = ["."],
        ko_deps = ko_deps,
    )

    if version_compare(kernel_version, "6.12") :
        if bazel_support_platform == "mtk":
            ko_deps_oplus_nfc = [
                     "//kernel_device_modules-{}/drivers/soc/oplus/boot:oplus_bsp_boot_projectinfo".format(kernel_version),
            ]
        else :
            ko_deps_oplus_nfc = [
                     "//vendor/oplus/kernel/boot:oplus_bsp_boot_projectinfo",
            ]
    else :
       ko_deps_oplus_nfc = []

    define_oplus_ddk_module(
        name = "oplus_nfc",
        srcs = native.glob([
            "**/*.h",
            "oplus_nfc/oplus_nfc.c",
        ]),
        includes = ["."],
        copts = [
            "-DCONFIG_OPLUS_NFC",
        ],
        ko_deps = ko_deps_oplus_nfc,
    )

    define_oplus_ddk_module(
        name = "oplus_network_nfc_thn31",
        srcs = native.glob([
            "**/*.h",
            "thn31/tms_common.c",
            "thn31/nfc/nfc_common.c",
            "thn31/nfc/nfc_driver.c",
            "thn31/ese/ese_common.c",
            "thn31/ese/ese_driver.c",
            "thn31/debuger/checker.c",
            "thn31/debuger/debuger.c",
            "thn31/debuger/logger.c",
        ]),
        conditional_defines = {
             "qcom":  ["QCOM_PLATFORM"],
         },
        includes = ["."],
        copts = [
            "-DCONFIG_TMS_NFC_DEVICE",
            "-DCONFIG_TMS_ESE_DEVICE",
            "-DTMS_DEBUGER_LOGGER",
            "-DTMS_DEBUGER_CHECKER",
        ],
        ko_deps = ko_deps,
    )

    ddk_copy_to_dist_dir(
        name = "oplus_network_nfc",
        module_list = [
            "oplus_network_nfc_sn_ese",
            "oplus_network_nfc_i2c",
            "oplus_network_nfc_thn31",
            "oplus_network_nfc_pn557_i2c",
            "oplus_nfc",
        ],
    )
