load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/kernel/oplus:oplus_modules_define.bzl", "define_oplus_ddk_module", "bazel_support_platform", "oplus_ddk_get_kernel_version")

def define_oplus_local_modules():
    kernel_version = oplus_ddk_get_kernel_version()

    if bazel_support_platform == "mtk" :
        tfa98xx_ko_deps = [
            "//kernel_device_modules-{}/sound/soc/mediatek/common:mtk-sp-spk-amp".format(kernel_version),
            "//kernel_device_modules-{}/sound/soc/codecs/multimedia/feedback/bazel:oplus_mm_kevent_fb".format(kernel_version),
        ]
        tfa98xx_header_deps = [
            "//kernel_device_modules-{}/sound/soc/mediatek/common:ddk_public_headers".format(kernel_version),
        ]
    else :
        tfa98xx_ko_deps = [
        ]
        tfa98xx_header_deps = [
        ]

    define_oplus_ddk_module(
        name = "snd-soc-tfa98xx",
        srcs = native.glob([
            "**/*.h",
            "tfa98xx_v6.14.2/src/tfa98xx.c",
            "tfa98xx_v6.14.2/src/tfa_container.c",
            "tfa98xx_v6.14.2/src/tfa_dsp.c",
            "tfa98xx_v6.14.2/src/tfa_init.c",
            "tfa98xx_v6.14.2/src/oplus_tfa98xx_feedback.c",
        ]),
        includes = [
            ".",
            "tfa98xx_v6.14.2/inc",
        ],
        local_defines = [
            "CONFIG_OPLUS_FEATURE_MM_FEEDBACK",
            "OPLUS_ARCH_EXTENDS",
            "TFA98XX_GIT_VERSIONS=v6",
            "OPLUS_FEATURE_SPEAKER_MUTE"
        ],
        copts = [
            "-Werror",
        ],
        ko_deps = tfa98xx_ko_deps,
        header_deps = tfa98xx_header_deps,
    )

    define_oplus_ddk_module(
        name = "snd-audio-extend",
        srcs = native.glob([
            "**/*.h",
        ]),
        conditional_srcs = {
            "CONFIG_OPLUS_DDK_MTK": {
                True:  ["mtk/audio_extend_drv.c"],
            }
        },
        includes = ["."],
        ko_deps = [
            "//kernel_device_modules-{}/sound/soc/mediatek/common:mtk-sp-spk-amp".format(kernel_version),
        ],
        header_deps = [
            "//kernel_device_modules-{}/sound/soc/mediatek/common:ddk_public_headers".format(kernel_version),
        ],
    )

    define_oplus_ddk_module(
        name = "snd-hal-feedback",
        srcs = native.glob([
            "**/*.h",
        ]),
        conditional_srcs = {
            "CONFIG_OPLUS_DDK_MTK": {
                True: ["mtk/hal_feedback.c"],
            }
        },
        includes = ["."],
        local_defines = [
            "CONFIG_OPLUS_FEATURE_MM_FEEDBACK",
        ],
        ko_deps = [
            "//kernel_device_modules-{}/sound/soc/codecs/multimedia/feedback/bazel:oplus_mm_kevent_fb".format(kernel_version),
        ],
    )

    define_oplus_ddk_module(
        name = "oplus_audio_netlink",
        srcs = native.glob([
            "**/*.h",
            "oplus_audio_netlink/oplus_audio_netlink_kernel.c"
        ]),
        includes = [
            ".",
            "oplus_audio_netlink/oplus_audio_netlink_kernel.h",
        ],
    )

    define_oplus_ddk_module(
        name = "snd-speaker_manager",
        srcs = native.glob([
            "**/*.h",
        ]),
        conditional_srcs = {
            "CONFIG_OPLUS_DDK_MTK": {
                True: ["mtk/oplus_speaker_manager/oplus_speaker_manager.c"],
            }
        },
        includes = ["."],
    )

    define_oplus_ddk_module(
        name = "snd-soc-aw87xxx",
        srcs = native.glob([
            "**/*.h",
            "aw87xxx_2.x.0/aw87xxx_2_x_0.c",
            "aw87xxx_2.x.0/aw87xxx_device_2_x_0.c",
            "aw87xxx_2.x.0/aw87xxx_monitor_2_x_0.c",
            "aw87xxx_2.x.0/aw87xxx_bin_parse_2_x_0.c",
            "aw87xxx_2.x.0/aw87xxx_dsp_2_x_0.c",
            "aw87xxx_2.x.0/aw87xxx_acf_bin_2_x_0.c",
        ]),
        includes = ["."],
        local_defines = [
            "CONFIG_SND_SOC_OPLUS_PA_MANAGER",
        ],
        copts = [
            "-Werror",
            "-Wno-date-time",
        ],
        ko_deps = [
            "//kernel_device_modules-{}/sound/soc/codecs:snd-soc-mt6366".format(kernel_version),
            "//kernel_device_modules-{}/sound/soc/codecs/audio/bazel:snd-speaker_manager".format(kernel_version),
            "//kernel_device_modules-{}/sound/soc/mediatek/common:mtk-sp-spk-amp".format(kernel_version),
        ],
        header_deps = [
            "//kernel_device_modules-{}/sound/soc/mediatek/common:ddk_public_headers".format(kernel_version),
        ],
    )

    define_oplus_ddk_module(
        name = "snd-soc-aw882xx",
        srcs = native.glob([
            "**/*.h",
            "aw882xx_v1.13.0/aw882xx_bin_parse.c",
            "aw882xx_v1.13.0/aw882xx_calib.c",
            "aw882xx_v1.13.0/aw882xx_device.c",
            "aw882xx_v1.13.0/aw882xx_dsp.c",
            "aw882xx_v1.13.0/aw882xx_init.c",
            "aw882xx_v1.13.0/aw882xx_monitor.c",
            "aw882xx_v1.13.0/aw882xx_spin.c",
            "aw882xx_v1.13.0/aw882xx.c",
        ]),
        includes = ["."],
        local_defines = [
            "OPLUS_ARCH_EXTENDS",
            "OPLUS_FEATURE_SPEAKER_MUTE",
        ],
        copts = [
            "-Werror",
            "-Wno-date-time",
            "-Wno-visibility",
            "-Wno-incompatible-pointer-types",
        ],
        ko_deps = [
            "//kernel_device_modules-{}/sound/soc/mediatek/common:mtk-sp-spk-amp".format(kernel_version),
            "//kernel_device_modules-{}/sound/soc/mediatek/audio_dsp:snd-soc-audiodsp-common".format(kernel_version),
            "//kernel_device_modules-{}/drivers/misc/mediatek/audio_ipi:audio_ipi".format(kernel_version),
        ],
        header_deps = [
            "//kernel_device_modules-{}/sound/soc/mediatek/common:ddk_public_headers".format(kernel_version),
        ],
    )

    define_oplus_ddk_module(
        name = "snd-soc-typec-switch",
        srcs = native.glob([
            "**/*.h",
            "oplus_typec_switch/oplus_typec_switch.c",
        ]),
        includes = ["."],
        ko_deps = [
            "//kernel_device_modules-{}/drivers/misc/mediatek/typec/mux:mux_switch".format(kernel_version),
            "//kernel_device_modules-{}/drivers/misc/mediatek/typec/tcpc:tcpc_class".format(kernel_version),
        ],
        header_deps = [
            "//kernel_device_modules-{}/drivers/misc/mediatek/typec/mux:ddk_public_headers".format(kernel_version),
            "//kernel_device_modules-{}/drivers/misc/mediatek/typec/tcpc:ddk_public_headers".format(kernel_version),
        ],
        local_defines = [
            "OPLUS_FEATURE_CHG_BASIC",
            "CONFIG_SND_SOC_OPLUS_TYPEC_SWITCH"
        ],
    )

    define_oplus_ddk_module(
        name = "fsa4480-i2c",
        srcs = native.glob([
            "**/*.h",
            "fsa44xx/fsa4480-i2c.c",
        ]),
        includes = ["."],
        local_defines = [
            "OPLUS_ARCH_EXTENDS",
            "CONFIG_SND_SOC_FSA",
            "CONFIG_OPLUS_MTK_PLATFORM",
        ],
        ko_deps = [
            "//kernel_device_modules-{}/drivers/misc/mediatek/typec/mux:mux_switch".format(kernel_version),
            "//kernel_device_modules-{}/drivers/misc/mediatek/typec/tcpc:tcpc_class".format(kernel_version),
            "//kernel_device_modules-{}/sound/soc/codecs:mt6358-accdet".format(kernel_version),
        ],
        header_deps = [
            "//kernel_device_modules-{}/drivers/misc/mediatek/typec/mux:ddk_public_headers".format(kernel_version),
            "//kernel_device_modules-{}/drivers/misc/mediatek/typec/tcpc:ddk_public_headers".format(kernel_version),
        ],
    )