load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module")
load("//build/kernel/oplus:oplus_modules_dist.bzl", "ddk_copy_to_dist_dir")


def define_oplus_local_modules():

    define_oplus_ddk_module(
        name = "oplus_resctrl",
        srcs = native.glob([
            "iocost.c",
            "resctrl.c",
            "resctrl.h",
        ]),
        includes = ["."],
        local_defines = ["CONFIG_OPLUS_RESCTRL"],
    )

    ddk_headers(
        name = "config_headers",
        hdrs  = native.glob([
            "resctrl.h",
        ]),
        includes = ["."],
    )

    ddk_copy_to_dist_dir(
        name = "oplus_resctrl",
        module_list = [
            "oplus_resctrl",
        ],
    )
