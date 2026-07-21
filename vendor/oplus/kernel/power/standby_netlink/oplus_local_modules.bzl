load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module", "oplus_ddk_get_variant", "oplus_ddk_get_target", "bazel_support_platform")
load("//build/kernel/oplus:oplus_modules_dist.bzl", "ddk_copy_to_dist_dir")

def define_oplus_local_modules():
    target = oplus_ddk_get_target()
    variant  = oplus_ddk_get_variant()
    kernel_build_variant = "{}_{}".format(target, variant)

    if bazel_support_platform == "qcom" :
        ko_deps = select({
            "//build/kernel/kleaf:socrepo_true":[
                    "//soc-repo:{}/drivers/regulator/debug-regulator".format(kernel_build_variant),
                    "//soc-repo:{}/drivers/clk/qcom/clk-qcom".format(kernel_build_variant),
                    "//soc-repo:{}/drivers/soc/qcom/smp2p".format(kernel_build_variant),
                ],
            "//build/kernel/kleaf:socrepo_false": [],
        })
    elif bazel_support_platform == "mtk":
        ko_deps = []
    else :
        ko_deps = []

    define_oplus_ddk_module(
        name = "oplus_standby_netlink",
        srcs = native.glob([
            "standby_netlink.h",
            "standby_netlink.c",
            "netlink_handler.c",
            "standby_netlink_deps.h",
            "standby_netlink_deps.c"
        ]),

        ko_deps = ko_deps,
        includes = ["."],
        conditional_defines = {
            "mtk":  ["CONFIG_OPLUS_STANDBY_NETLINK_MTK"],
            "qcom":  ["CONFIG_OPLUS_STANDBY_NETLINK_QCOM"],
        },
        local_defines = ["OPLUS_FEATURE_STANDBY_NETLINK"],
    )

    ddk_copy_to_dist_dir(
        name = "oplus_standby_netlink",
        module_list = [
            "oplus_standby_netlink",
        ],
    )
