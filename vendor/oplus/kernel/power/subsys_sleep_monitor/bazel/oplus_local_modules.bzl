load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module")
load("//build/kernel/oplus:oplus_modules_dist.bzl", "ddk_copy_to_dist_dir")

def define_oplus_local_modules():

    define_oplus_ddk_module(
        name = "oplus_subsys_sleep_monitor",
        srcs = native.glob([
            "oplus_subsys_sleep_monitor/*.h",
            "oplus_subsys_sleep_monitor/oplus_subsys_sleep_monitor.c",
        ]),
        includes = ["."],
	local_defines = ["CONFIG_OPLUS_SUBSYS_SLEEP_MONITOR"],
    )

    ddk_copy_to_dist_dir(
        name = "oplus_ss_sleep_monitor",
        module_list = [
            "oplus_subsys_sleep_monitor",
        ],
    )
