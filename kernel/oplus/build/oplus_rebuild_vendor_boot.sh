#!/bin/bash

if [ $# -lt 2 -o $# -gt 3 ]; then
    echo "***********************************************************"
    echo "参数必须为2个或者3个"
    echo "如果要编译in-tree ko:"
    echo "command : ./kernel/oplus/build/oplus_rebuild_vendor_boot.sh platform build_type"
    echo "例：./kernel/oplus/build/oplus_rebuild_vendor_boot.sh mt6991 user"
    echo "如果要编译out-tree ko，module可以在oplus_modules_list.bzl中找到"
    echo "command : ./kernel/oplus/build/oplus_rebuild_vendor_boot.sh platform build_type module"
    echo "例：./kernel/oplus/build/oplus_rebuild_vendor_boot.sh mt6991 user //vendor/oplus/kernel/dfr:oplus_bsp_dfr_shutdown_detect"
    echo "***********************************************************"
    exit
fi

source kernel/oplus/build/oplus_setup.sh $1 $2
init_build_environment
IS_INTRANET="no"

source kernel/oplus/build/oplus_rebuild_img_function.sh

rebuild_dtb_image() {
    echo "**********rebuild dtb.img start $(date +%H:%M:%S)**********"
    cp ${MAINDTB_PATH}/mtk.dtb ${VENDOR_BOOT_TMP_IMAGE}/origin/dtb
    echo "**********rebuild dtb.img end $(date +%H:%M:%S)**********"
}

vendor_boot_modules_all_update() {

    echo "vendor_boot module update"

    mkdir -p ${VENDOR_BOOT_TMP_IMAGE}/dist/
    mkdir -p ${VENDOR_BOOT_TMP_IMAGE}/tmp/

    mv ${VENDOR_BOOT_TMP_IMAGE}/ramdisk00/lib/modules/modules.load \
       ${VENDOR_BOOT_TMP_IMAGE}/ramdisk00/lib/modules/modules.load_bak

    cp ${ACKDIR}/oplus/prebuild/vendor_boot.load \
       ${VENDOR_BOOT_TMP_IMAGE}/ramdisk00/lib/modules/modules.load

    ko_list=`cat ${VENDOR_BOOT_TMP_IMAGE}/ramdisk00/lib/modules/modules.load | xargs -L 1 basename`

    for ko in  $ko_list
    do
        current=`find ${VENDOR_INTREE_MODULES_DIR} -maxdepth 1 -name ${ko}`
        if [ -n "${current}" ]; then
            cp ${current} ${VENDOR_BOOT_TMP_IMAGE}/dist/
            ${STRIP} -S ${VENDOR_BOOT_TMP_IMAGE}/dist/${ko} -o ${VENDOR_BOOT_TMP_IMAGE}/tmp/${ko}
            cp ${VENDOR_BOOT_TMP_IMAGE}/tmp/${ko} ${VENDOR_BOOT_TMP_IMAGE}/ramdisk00/lib/modules/
        else
            current=`find ${VENDOR_OUTTREE_MODULES_DIR} -name ${ko} | head -n 1`
            if [ -n "${current}" ]; then
                cp ${current} ${VENDOR_BOOT_TMP_IMAGE}/dist/
                ${STRIP} -S ${VENDOR_BOOT_TMP_IMAGE}/dist/${ko} -o ${VENDOR_BOOT_TMP_IMAGE}/tmp/${ko}
                cp ${VENDOR_BOOT_TMP_IMAGE}/tmp/${ko} ${VENDOR_BOOT_TMP_IMAGE}/ramdisk00/lib/modules/
            fi
        fi
    done
}

rebuild_outtree_ko() {
    echo "**********rebuild out-tree ko $(date +%H:%M:%S)**********"
    cd ${TOPDIR}/kernel

    tools/bazel \
    --output_root=${KLEAF_OBJ} \
    --output_base=${OUTPUT_BASE} \
    build \
    --action_env=PATH=${ACKDIR}/build/kernel/build-tools/path/linux-x86:/usr/bin:/bin \
    --//build/bazel_mgk_rules:kernel_version=${VERSION} \
    --experimental_writable_outputs --noenable_bzlmod --config=stamp \
    --repo_manifest=${TOPDIR}/kernel/kernel_device_modules-${VERSION}/fake_manifest.xml \
    $1.${KRN_MGK}.6.6.${variants_type} 2>&1 |tee ${TOPDIR}/LOGDIR/build_${CURRENT_LOG}.log

    cd ${TOPDIR}
    echo "**********rebuild out-tree ko end $(date +%H:%M:%S)**********"
}

rebuild_intree_ko() {
    echo "**********rebuild in-tree ko $(date +%H:%M:%S)**********"
    cd ${TOPDIR}/kernel

    tools/bazel \
    --output_root=${KLEAF_OBJ} \
    --output_base=${OUTPUT_BASE} \
    build \
    --action_env=PATH=${ACKDIR}/build/kernel/build-tools/path/linux-x86:/usr/bin:/bin \
    --//build/bazel_mgk_rules:kernel_version=${VERSION} \
    --experimental_writable_outputs --noenable_bzlmod --config=stamp \
    --repo_manifest=${TOPDIR}/kernel/kernel_device_modules-${VERSION}/fake_manifest.xml \
    //kernel_device_modules-${VERSION}:${KRN_MGK}_modules.${variants_type} 2>&1 |tee ${TOPDIR}/LOGDIR/build_${CURRENT_LOG}.log

    cd ${TOPDIR}
    echo "**********rebuild in-tree ko end $(date +%H:%M:%S)**********"
}

rebuild_vendor_boot_image() {
    echo "**********rebuild vendor_boot.img start $(date +%H:%M:%S)**********"
    rm -rf ${VENDOR_BOOT_TMP_IMAGE}/*
    boot_mkargs=$(${PYTHON_TOOL} ${UNPACK_BOOTIMG_TOOL} --boot_img ${ORIGIN_IMAGE}/vendor_boot.img --out ${VENDOR_BOOT_TMP_IMAGE}/origin --format=mkbootimg)
    rebuild_dtb_image
    index="00"
    for index in  $index
    do
        echo " index  $index "
        mv ${VENDOR_BOOT_TMP_IMAGE}/origin/vendor_ramdisk${index} ${VENDOR_BOOT_TMP_IMAGE}/vendor_ramdisk${index}.lz4
        #touch ${VENDOR_BOOT_TMP_IMAGE}/vendor_ramdisk${index}
        ${LZ4} -d -f ${VENDOR_BOOT_TMP_IMAGE}/vendor_ramdisk${index}.lz4 ${VENDOR_BOOT_TMP_IMAGE}/vendor_ramdisk${index}
        rm ${VENDOR_BOOT_TMP_IMAGE}/vendor_ramdisk${index}.lz4
        mkdir -p ${VENDOR_BOOT_TMP_IMAGE}/ramdisk${index}
        mv ${VENDOR_BOOT_TMP_IMAGE}/vendor_ramdisk${index} ${VENDOR_BOOT_TMP_IMAGE}/ramdisk${index}/vendor_ramdisk${index}
        pushd  ${VENDOR_BOOT_TMP_IMAGE}/ramdisk${index}
        ${CPIO} -idu < ${VENDOR_BOOT_TMP_IMAGE}/ramdisk${index}/vendor_ramdisk${index}

        popd
        rm ${VENDOR_BOOT_TMP_IMAGE}/ramdisk${index}/vendor_ramdisk${index}

        vendor_boot_modules_all_update
        ${MKBOOTFS} ${VENDOR_BOOT_TMP_IMAGE}/ramdisk${index} > ${VENDOR_BOOT_TMP_IMAGE}/vendor_ramdisk${index}
        #touch ${VENDOR_BOOT_TMP_IMAGE}/vendor_ramdisk${index}.lz4
        ${LZ4} -l -f -12 --favor-decSpeed ${VENDOR_BOOT_TMP_IMAGE}/vendor_ramdisk${index} ${VENDOR_BOOT_TMP_IMAGE}/vendor_ramdisk${index}.lz4
        mv ${VENDOR_BOOT_TMP_IMAGE}/vendor_ramdisk${index}.lz4 ${VENDOR_BOOT_TMP_IMAGE}/origin/vendor_ramdisk${index}
        rm ${VENDOR_BOOT_TMP_IMAGE}/vendor_ramdisk${index}
    done
    bash -c "${PYTHON_TOOL} ${MKBOOTIMG_PATH} ${boot_mkargs} --vendor_boot ${IMAGE_OUT}/vendor_boot.img"
    sign_vendor_boot_image
    echo "**********rebuild vendor_boot.img end $(date +%H:%M:%S)**********"
}

build_start_time
is_intranet
download_prebuild_image
get_image_info
get_modules_list
if [ -n "$3" ]; then
rebuild_outtree_ko $3
else
rebuild_intree_ko
rebuild_vendor_boot_image
fi
print_end_help
build_end_time