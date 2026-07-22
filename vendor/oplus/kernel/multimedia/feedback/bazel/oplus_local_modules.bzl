load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module")
load("//build/kernel/oplus:oplus_modules_dist.bzl", "ddk_copy_to_dist_dir")

def define_oplus_local_modules():
    define_oplus_ddk_module(
        name = "oplus_mm_kevent",
        srcs = native.glob([
            "src/**/*.h",
            "src/oplus_mm_kevent.c",
        ]),
        includes = ["."],
        local_defines = ["CONFIG_OPLUS_FEATURE_MM_FEEDBACK"],
    )

    define_oplus_ddk_module(
        name = "oplus_mm_kevent_fb",
        srcs = native.glob([
            "src/**/*.h",
            "src/oplus_mm_kevent_fb.c",
        ]),
        ko_deps = [
            ":oplus_mm_kevent",
        ],
        includes = ["."],
        local_defines = ["CONFIG_OPLUS_FEATURE_MM_FEEDBACK"],
    )

    ddk_copy_to_dist_dir(
        name = "oplus_multimedia_feedback",
        module_list = [
            "oplus_mm_kevent",
            "oplus_mm_kevent_fb",
        ],
    )
