load(":defconfig/oplus_k6789v1_user_config.bzl", "oplus_k6789v1_user_config")
load(":defconfig/oplus_k6789v1_userdebug_config.bzl", "oplus_k6789v1_userdebug_config")
load(":defconfig/oplus_k6895v1_user_config.bzl", "oplus_k6895v1_user_config")
load(":defconfig/oplus_k6895v1_userdebug_config.bzl", "oplus_k6895v1_userdebug_config")

oplus_config = {
    "mtk": {
        "k6789v1": {
            "user": oplus_k6789v1_user_config,
            "userdebug": oplus_k6789v1_user_config | oplus_k6789v1_userdebug_config
        },
        "k6895v1": {
            "user": oplus_k6895v1_user_config,
            "userdebug": oplus_k6895v1_user_config | oplus_k6895v1_userdebug_config
        },
    }
}
