load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module", "oplus_ddk_get_target", "oplus_ddk_get_variant", "bazel_support_platform", "oplus_ddk_get_kernel_version")
load("//build/kernel/oplus:oplus_modules_dist.bzl", "ddk_copy_to_dist_dir")

def define_oplus_local_modules():
    target = oplus_ddk_get_target()
    variant  = oplus_ddk_get_variant()
    kernel_build_variant = "{}_{}".format(target, variant)
    kernel_version = oplus_ddk_get_kernel_version()

    if bazel_support_platform == "qcom" :
        ko_deps = select({
            "//build/kernel/kleaf:socrepo_true":[
                    "//soc-repo:{}/drivers/soc/qcom/qcom_stats".format(kernel_build_variant),
                    "//soc-repo:{}/drivers/soc/qcom/smem".format(kernel_build_variant),
                ],
            "//build/kernel/kleaf:socrepo_false": [],
        })
        copts = ["-DCONFIG_QCOM_SMEM"]
        header_deps = []
    elif bazel_support_platform == "mtk" :
        if target == "k6993v1_64" :
            ko_deps = ["//kernel_device_modules-{}/drivers/misc/mediatek/lpm/modules/debug:mtk-lpm-dbg-common-v2".format(kernel_version),]
            header_deps = ["//kernel_device_modules-{}/drivers/misc/mediatek/lpm/modules/debug:ddk_public_headers".format(kernel_version),]
        else :
            ko_deps = []
            header_deps = []
        copts = []
    else :
        ko_deps = []
        copts = []
        header_deps = []

    define_oplus_ddk_module(
        name = "oplus_rpmh_statics",
        #outs = "oplus_rpmh_statics.ko",
        srcs = native.glob([
            "**/*.h",
            "oplus_rpmh_statics.c",
        ]),
        includes = ["."],
        ko_deps = ko_deps,
        copts = copts,
        header_deps = header_deps,
    )

    ddk_copy_to_dist_dir(
        name = "oplus_rpmh_statics",
        module_list = [
            "oplus_rpmh_statics",
        ],
    )
