load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module", "bazel_support_platform", "oplus_ddk_get_kernel_version")
load("//build/kernel/oplus:oplus_modules_dist.bzl", "ddk_copy_to_dist_dir")

def version_compare(v1, v2):
    v1_parts = [int(x) for x in v1.split(".")]
    v2_parts = [int(x) for x in v2.split(".")]
    return v1_parts >= v2_parts

def define_oplus_local_modules():

    define_oplus_ddk_module(
        name = "oplus_connectivity_routerboost",
        srcs = native.glob([
            "**/*.h",
            "oplus_connectivity_routerboost/oplus_routerboost.c",
            "oplus_connectivity_routerboost/oplus_routerboost_game_monitor.c"
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_connectivity_sla",
        srcs = native.glob([
            "oplus_connectivity_sla/oplus_connectivity_sla.c"
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_wifi_wsa",
        srcs = native.glob([
            "oplus_wifi_wsa/oplus_wifismartantenna.c"
        ]),
        local_defines = ["OPLUS_FEATURE_WIFI_SMARTANTENNA"],
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_wificapcenter",
        srcs = native.glob([
            "**/*.h",
            "oplus_wificapcenter/oplus_wificapcenter.c"
        ]),
        includes = ["."],
    )

    #only for MTK platform, and kernel >= 6.10
    #compiling of version will be failed without mentioned rule
    #for lower kernel version of MTK, use Makefile instead
    if bazel_support_platform == "mtk" :

        kernel_version = oplus_ddk_get_kernel_version()
        print("kernel version: " + kernel_version)
        if version_compare(kernel_version, "6.12") :
            copts = [
                 "-I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/eccci/port/",
            ]
        else :
            copts = [
                "-I$(ROOT_DIR)/kernel_device_modules-{}/drivers/misc/mediatek/eccci/port/".format(kernel_version),
            ]

        define_oplus_ddk_module(
            name = "oplus_wifi_swtp",
            srcs = native.glob([
                "**/*.h",
                "oplus_wifi_swtp/oplus_wifi_swtp.c"
            ]),
            includes = ["."],
            copts = copts,
        )

        module_list = [
            "oplus_connectivity_routerboost",
            "oplus_connectivity_sla",
            "oplus_wifi_wsa",
            "oplus_wificapcenter",
            "oplus_wifi_swtp",
        ]

    else :
        module_list = [
            "oplus_connectivity_routerboost",
            "oplus_connectivity_sla",
            "oplus_wifi_wsa",
            "oplus_wificapcenter",
        ]

    ddk_headers(
        name = "config_headers",
        hdrs  = native.glob([
            "**/*.h",
        ]),
        includes = ["."],
    )

    ddk_copy_to_dist_dir(
        name = "oplus_wifi",
        module_list = module_list,
    )
