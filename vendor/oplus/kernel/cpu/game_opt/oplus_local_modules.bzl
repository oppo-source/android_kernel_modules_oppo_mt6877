load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module", "oplus_ddk_get_target")
load("//build/kernel/oplus:oplus_modules_dist.bzl", "ddk_copy_to_dist_dir")


def define_oplus_local_modules():
    ko_deps = []
    header_deps = []

    define_oplus_ddk_module(
        name = "oplus_bsp_game_opt",
        srcs = native.glob([
            "**/*.h",
            "cpu_load.c",
            "cpufreq_limits.c",
            "debug.c",
            "dsu_freq.c",
            "early_detect.c",
            "fake_cpufreq.c",
            "game_ctrl.c",
            "rt_info.c",
            "multi_rt_info.c",
            "task_load_track.c",
            "task_util.c",
            "multi_task_util.c",
            "yield_opt.c",
            "frame_load.c",
            "frame_sync.c",
            "task_boost/heavy_task_boost.c",
            "task_boost/boost_proc.c",
            "frame_detect/frame_detect.c",
            "oem_data/game_oem_data.c",
            "critical_task_boost.c",
        ]),
        includes = ["."],
        ko_deps = ko_deps,
        header_deps = header_deps,
        local_defines = [],
        copts = select({
            "//build/kernel/kleaf:kocov_is_true": ["-fprofile-arcs", "-ftest-coverage"],
            "//conditions:default": [],
        }),
    )

    ddk_copy_to_dist_dir(
        name = "oplus_bsp_game",
        module_list = [
            "oplus_bsp_game_opt",
        ],
    )
