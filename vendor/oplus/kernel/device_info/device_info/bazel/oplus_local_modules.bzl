load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module")
load("//build/kernel/oplus:oplus_modules_dist.bzl", "ddk_copy_to_dist_dir")

def define_oplus_local_modules():

    define_oplus_ddk_module(
        name = "device_info",
        srcs = native.glob([
            "*.h",
            "device_info.c",
        ]),
        includes = ["."],
        conditional_defines = {
            "mtk":  ["CONFIG_MTK_PLATFORM", "CONFIG_OPLUS_DEVICE_INFO_MTK_PLATFORM"],
        },
    )

    ddk_copy_to_dist_dir(
        name = "device_info",
        module_list = [
            "device_info",
        ],
    )