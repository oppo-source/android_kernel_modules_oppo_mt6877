load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module")
load("//build/kernel/oplus:oplus_modules_dist.bzl", "ddk_copy_to_dist_dir")

def define_oplus_local_modules():

    define_oplus_ddk_module(
        name = "oplus_camera_wl28681c_regulator",
        srcs = native.glob([
            "**/*.h",
            "regulator/wl28681c-regulator.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_wl28681c1_regulator",
        srcs = native.glob([
            "**/*.h",
            "regulator/wl28681c1-regulator.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_dio8018wl20_regulator",
        srcs = native.glob([
            "**/*.h",
            "regulator/dio8018wl20-regulator.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_aw37004dnr_regulator",
        srcs = native.glob([
            "**/*.h",
            "regulator/aw37004dnr-regulator.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_sgm38121_regulator",
        srcs = native.glob([
            "**/*.h",
            "regulator/sgm38121-regulator.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_jd5516w_24720",
        srcs = native.glob([
            "**/*.h",
            "lens/vcm/v4l2/jd5516w_24720/jd5516w_24720.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_ak7377m",
        srcs = native.glob([
            "**/*.h",
            "lens/vcm/v4l2/ak7377m/ak7377m.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_ak7316m",
        srcs = native.glob([
            "**/*.h",
            "lens/vcm/v4l2/ak7316m/ak7316m.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_ak7316t",
        srcs = native.glob([
            "**/*.h",
            "lens/vcm/v4l2/ak7316t/ak7316t.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_ak7316t33",
        srcs = native.glob([
            "**/*.h",
            "lens/vcm/v4l2/ak7316t33/ak7316t33.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_jd5516w",
        srcs = native.glob([
            "**/*.h",
            "lens/vcm/v4l2/jd5516w/jd5516w.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_dw9827c_24071",
        srcs = native.glob([
            "**/*.h",
            "lens/vcm/v4l2/dw9827c_24071/dw9827c_24071.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_jd5516w_24071",
        srcs = native.glob([
            "**/*.h",
            "lens/vcm/v4l2/jd5516w_24071/jd5516w_24071.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_dw9800s_24071",
        srcs = native.glob([
            "**/*.h",
            "lens/vcm/v4l2/dw9800s_24071/dw9800s_24071.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_dw9800s_front_24071",
        srcs = native.glob([
            "**/*.h",
            "lens/vcm/v4l2/dw9800s_front_24071/dw9800s_front_24071.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_gt9764v_24069",
        srcs = native.glob([
            "**/*.h",
            "lens/vcm/v4l2/gt9764v_24069/gt9764v_24069.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_al9800a_25922",
        srcs = native.glob([
            "**/*.h",
            "lens/vcm/v4l2/al9800a_25922/al9800a_25922.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_dw9786",
        srcs = native.glob([
            "**/*.h",
            "lens/ois/dw9786/adaptor-i2c.c",
            "lens/ois/dw9786/dw9786af.c",
        ]),
        ko_deps = ["//vendor/mediatek/kernel_modules/mtkcam/cam_cal/src_v4l2/custom:mtk_cam_cal",],
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_ois_dw9786",
        srcs = native.glob([
            "**/*.h",
            "lens/ois/ois_dw9786/adaptor-i2c.c",
            "lens/ois/ois_dw9786/dw9786_if.c",
            "lens/ois/ois_dw9786/dw9786.c",
        ]),
        ko_deps = ["//vendor/mediatek/kernel_modules/mtkcam/cam_cal/src_v4l2/custom:mtk_cam_cal",],
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_aw36515_zhuquec1",
        srcs = native.glob([
            "**/*.h",
            "flashlight/v4l2/aw36515_zhuquec1.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_aw36515_knightm",
        srcs = native.glob([
            "**/*.h",
            "flashlight/v4l2/aw36515_knightm.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_ois_power",
        srcs = native.glob([
            "**/*.h",
            "lens/ois/ois_power/ois_power.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_tof8801",
        srcs = native.glob([
            "**/*.h",
            "lens/tof/tof8801/tof_hex_interpreter.c",
            "lens/tof/tof8801/tof8801_app0.c",
            "lens/tof/tof8801/tof8801_bootloader.c",
            "lens/tof/tof8801/tof8801_driver.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_jd5516we4",
        srcs = native.glob([
            "**/*.h",
            "lens/vcm/v4l2/jd5516we4/jd5516we4.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_dw9827c_main_25011",
        srcs = native.glob([
            "**/*.h",
            "lens/vcm/v4l2/dw9827c_main_25011/dw9827c_main_25011.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_jd5516w_front_25011",
        srcs = native.glob([
            "**/*.h",
            "lens/vcm/v4l2/jd5516w_front_25011/jd5516w_front_25011.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_gt9764v_wide_25011",
        srcs = native.glob([
            "**/*.h",
            "lens/vcm/v4l2/gt9764v_wide_25011/gt9764v_wide_25011.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_dw9827c_tele_25011",
        srcs = native.glob([
            "**/*.h",
            "lens/vcm/v4l2/dw9827c_tele_25011/dw9827c_tele_25011.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_aw36515_whoopassc1",
        srcs = native.glob([
            "**/*.h",
            "flashlight/v4l2/aw36515_whoopassc1.c",
        ]),
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "oplus_camera_sy7806e_whoopassc2",
        srcs = native.glob([
            "**/*.h",
            "flashlight/v4l2/sy7806e_whoopassc2.c",
        ]),
        includes = ["."],
    )

    ddk_copy_to_dist_dir(
        name = "oplus_camera",
        module_list = [
            "oplus_camera_wl28681c_regulator",
            "oplus_camera_wl28681c1_regulator",
            "oplus_camera_dio8018wl20_regulator",
            "oplus_camera_aw37004dnr_regulator",
            "oplus_camera_sgm38121_regulator",
            "oplus_camera_ak7377m",
            "oplus_camera_ak7316m",
            "oplus_camera_ak7316t",
            "oplus_camera_ak7316t33",
            "oplus_camera_jd5516w",
            "oplus_camera_dw9786",
            "oplus_camera_ois_dw9786",
            "oplus_camera_ois_power",
            "oplus_camera_tof8801",
            "oplus_camera_jd5516we4",
        ],
    )
