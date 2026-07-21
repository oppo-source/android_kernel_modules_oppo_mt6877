load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module")
load("//build/kernel/oplus:oplus_modules_dist.bzl", "ddk_copy_to_dist_dir")

def define_oplus_local_modules():

    define_oplus_ddk_module(
        name = "oplus_bsp_fw_update",
        srcs = native.glob([
            "*.h",
            "kernelFwUpdate.c",
        ]),
        includes = ["."],
    )

    ddk_copy_to_dist_dir(
        name = "oplus_bsp_fw_update",
        module_list = [
            "oplus_bsp_fw_update",
        ],
    )
    ddk_headers(
        name = "oplus_bsp_fw_update_headers",
        hdrs  = native.glob([
            "*.h"
        ]),
        includes = [
            ".",
        ]
    )