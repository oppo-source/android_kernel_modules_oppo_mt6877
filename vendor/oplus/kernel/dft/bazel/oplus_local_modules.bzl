load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module", "oplus_ddk_get_kernel_version", "oplus_ddk_get_target", "oplus_ddk_get_variant", "bazel_support_platform")
load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
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

    if bazel_support_platform == "qcom" :
        olc_kconfig = None
        kfb_kconfig = None
        olc_defconfig = None
        kfb_defconfig = None
        if version_compare(kernel_version, "6.12"):
            ddk_config = "//soc-repo:{}_config".format(kernel_build_variant)
    else :
        olc_kconfig = "common/olc/Kconfig"
        kfb_kconfig = "common/feedback/Kconfig"
        olc_defconfig = "oplus_olc_defconfig"
        kfb_defconfig = "oplus_kfb_defconfig"
        if version_compare(kernel_version, "6.12"):
            ddk_config = None

    if version_compare(kernel_version, "6.12"):
        define_oplus_ddk_module(
            name = "oplus_bsp_dft_olc",
            srcs = native.glob([
                "common/olc/*.c",
            ]),
            includes = ["include"],
            kconfig = olc_kconfig,
            defconfig = olc_defconfig,
            config = ddk_config,
        )
    else :
        define_oplus_ddk_module(
            name = "oplus_bsp_dft_olc",
            srcs = native.glob([
                "common/olc/*.c",
            ]),
            includes = ["include"],
            kconfig = olc_kconfig,
            defconfig = olc_defconfig,
        )

    if version_compare(kernel_version, "6.12"):
        define_oplus_ddk_module(
            name = "oplus_bsp_dft_kernel_fb",
            srcs = native.glob([
                "common/feedback/**/*.h",
            ]),
            conditional_srcs = {
                "CONFIG_OPLUS_DDK_MTK": {
                    True: ["common/feedback/gki_mtk/kernel_fb.c"],
                    False: ["common/feedback/kernel_fb.c"],
                }
            },
            includes = ["include"],
            kconfig = kfb_kconfig,
            defconfig = kfb_defconfig,
            config = ddk_config,
        )
    else :
        define_oplus_ddk_module(
            name = "oplus_bsp_dft_kernel_fb",
            srcs = native.glob([
                "common/feedback/**/*.h",
            ]),
            conditional_srcs = {
                "CONFIG_OPLUS_DDK_MTK": {
                    True: ["common/feedback/gki_mtk/kernel_fb.c"],
                    False: ["common/feedback/kernel_fb.c"],
                }
            },
            includes = ["include"],
            kconfig = kfb_kconfig,
            defconfig = kfb_defconfig,
        )

    ddk_headers(
        name = "oplus_dft_headers",
        hdrs = native.glob([
            "include/*.h",
        ]),
        includes = ["."],
    )

    ddk_copy_to_dist_dir(
        name = "oplus_bsp_dft",
        module_list = [
            "oplus_bsp_dft_olc",
            "oplus_bsp_dft_kernel_fb"
        ],
    )
