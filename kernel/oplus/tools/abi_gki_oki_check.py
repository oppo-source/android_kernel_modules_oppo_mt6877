import os
import subprocess
import shutil
import fnmatch
import pathlib
import sys
import csv
import io
import glob
from datetime import datetime
import argparse
import re
import common

out_put = "out/oplus"
log_dir = 'LOGDIR/abi/'
exclude_file = ['*.ko', 'vmlinux']


def generate_ack_abi(args, ack, output_dir):
    abi_aarch64_oplus = "{}/abi_{}_aarch64_oplus".format(output_dir, ack)
    abi_required_oplus = "{}/abi_{}_required_full.txt".format(output_dir, ack)
    generate_abi_aarch64_oplus = "{}  {}/ --skip-module-grouping --symbol-list " \
                                 "{} | grep 'Symbol '>> " \
                                 "{}".format(args.extract_symbols, output_dir,
                                             abi_aarch64_oplus, abi_required_oplus)
    process = subprocess.Popen(generate_abi_aarch64_oplus, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return process, generate_abi_aarch64_oplus


def dump_modversions(ack_path, ko_modversions_full):
    ko_files = find_ko_files(ack_path)
    command = "modprobe --dump-modversions {} *.ko to {}".format(ack_path, ko_modversions_full)
    ko_files.sort()

    commands = []
    for ko_file in ko_files:
        commands.append("modprobe --dump-modversions {}".format(ko_file))

    combined_command = "; ".join(commands)

    with open(ko_modversions_full, 'a') as outfile:
        process = subprocess.Popen(combined_command, shell=True, stdout=outfile)

    return process, command

def generate_abis_modversions(args, acks, output_dir):
    print("\ngenerate_abi...")
    processes = []
    for ack in acks:
        ack_path = pathlib.Path(output_dir) / ack
        ack_vmlinux = str(ack_path / "vmlinux")
        ko_modversions_full = "{}/ko_modversions_full.txt".format(ack_path)
        if os.path.exists(ack_vmlinux):
            process, command = generate_ack_abi(args, ack, ack_path)
            processes.append((process, command))

        process, command = dump_modversions(ack_path, ko_modversions_full)
        processes.append((process, command))

    # need wait finish
    for process, command in processes:
        print("Waited process: {} with command: {} finish\n".format(process.pid, command))
        process.communicate()


def remove_lines_and_write_new_file(required_file, weak_file, new_file):
    if not os.path.exists(required_file):
        print("Error: The file {} does not exist.".format(required_file))
        return

    with open(required_file, 'r') as file:
        required_lines = set(file.read().splitlines())

    if not os.path.exists(weak_file):
        with open(new_file, 'w') as file:
            for line in sorted(required_lines):
                file.write(line + '\n')
        return

    with open(weak_file, 'r') as file:
        weak_lines = set(file.read().splitlines())

    updated_lines = required_lines - weak_lines

    with open(new_file, 'w') as file:
        for line in sorted(updated_lines):
            file.write(line + '\n')


def get_approval_config_from_file(input_file: str, output_file: str) -> None:
    # Check if input file exists
    if not os.path.exists(input_file):
        print("Input file {} does not exist. Creating an empty output file.".format(input_file))
        with open(output_file, 'w', encoding='utf-8') as file:
            pass
        return

    # Read content from CSV file
    first_column = []
    try:
        with open(input_file, "r", encoding='utf-8') as file:
            csv_reader = csv.reader(file, delimiter=',')
            first_column = [row[0] for row in csv_reader if row and row[0].strip()]
    except Exception as exc:
        print("Error reading input file {}: {}".format(input_file, exc))
        with open(output_file, 'w', encoding='utf-8') as file:
            pass
        return

    # Determine if the data is valid
    if not first_column:
        print("No valid data found in {}.".format(input_file))
        with open(output_file, 'w', encoding='utf-8') as file:
            pass
        return

    # Temporarily store the extracted content in a StringIO object
    output_buffer = io.StringIO()
    for item in first_column:
        output_buffer.write("{}\n".format(item))

    output_without_bom = output_buffer.getvalue().replace('\ufeff', '')

    # Write the extracted content into a new file
    with open(output_file, "w", encoding='utf-8') as output:
        output.write(output_without_bom)


def compare_and_output_extra_lines(base_file, new_file, output_file):
    # read the content of basic files and new files
    with open(base_file, 'r', encoding='utf-8') as f_base:
        base_lines = f_base.readlines()

    with open(new_file, 'r', encoding='utf-8') as f_new:
        new_lines = f_new.readlines()

    extra_lines = []
    for new_line in new_lines:
        if new_line not in base_lines:
            extra_lines.append(new_line)

    # print("Extra Lines:")
    for line in extra_lines:
        print(line)

    # write the excess number of lines to the specified file
    with open(output_file, 'w') as f_output:
        for line in extra_lines:
            f_output.write("{}".format(line))


def compare_same_line_files(base_file, new_file, output_file):
    try:
        with open(base_file, "r") as file1, open(new_file, "r") as file2:
            base_lines = file1.readlines()
            new_lines = file2.readlines()

        with open(output_file, "w") as output:
            for line in new_lines:
                if line in base_lines:
                    output.write(line)
                    print("missing symbols in recovery mode ko:\n{}".format(line.strip()))
    except FileNotFoundError as exc:
        print("error", exc)


def copy_log_files_to_dest(src, dest, exclude_patterns=None):
    print("Copy build log from {} to {} exclude{}".format(src, dest, exclude_patterns))
    print("You can find log in {} or in {} ".format(src, dest))
    print("For more information, please refer to the documentation link:")
    print("检查是否缺少符号,检查是否破坏KMI,检查是否能够进入recovery\n如需帮助,请阅读如下指导文档:")
    print("")

    if not os.path.exists(src):
        raise ValueError("src dir {} not exists".format(src))

    if not os.path.exists(dest):
        os.makedirs(dest)

    for item in os.listdir(src):
        src_item = os.path.join(src, item)
        dest_item = os.path.join(dest, item)

        if exclude_patterns and any(fnmatch.fnmatch(item, pattern) for pattern in exclude_patterns):
            continue

        if os.path.isfile(src_item):
            shutil.copy2(src_item, dest_item)


def check_file_is_empty(file_path):
    file = pathlib.Path(file_path)
    if not file.exists():
        raise FileNotFoundError("File not found: {}".format(file_path))

    if file.stat().st_size != 0:
        with file.open('r') as f:
            content = f.read()
        print("\nFile {} content:\n{}".format(file_path, content))
        raise Exception("File {} is not null please check reason".format(file_path))


def check_current_status(file_path, src, dest, exclude_patterns=None):
    try:
        # print("check abi_required_and_remove_tmp_approval.txt is empty or not...")
        check_file_is_empty(file_path)
    except Exception as exc:
        print(exc)
        copy_log_files_to_dest(src, dest, exclude_patterns)
        sys.exit(1)


def find_ko_files(path):
    ko_files = []
    for root, dirs, files in os.walk(str(path)):
        for file in files:
            if file.endswith('.ko'):
                ko_files.append(os.path.join(root, file))
    return ko_files


def find_system_dlkm_version(base_path):
    # Step 1: Find the .ko file
    ko_files = find_ko_files(base_path)
    if not ko_files:
        return "No .ko files found"
    ko_files.sort()
    ko_file_path = ko_files[0]

    # Step 2: Execute modinfo command
    result = subprocess.run(['modinfo', ko_file_path], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            universal_newlines=True)
    if result.returncode != 0:
        return "Error executing modinfo: {}".format(result.stderr)

    # Step 3: Extract the vermagic string
    for line in result.stdout.split('\n'):
        if line.startswith('vermagic:'):
            vermagic = line.split(':', 1)[1].strip()
            # Extract the version part before the first space
            version = vermagic.split()[0]
            print(version)
            return version

    return "vermagic not found in modinfo output"


def check_src_img_approval(src_img_approval_csv, src, img):
    with open(src_img_approval_csv, mode='r', newline='') as file:
        reader = csv.DictReader(file)
        for row in reader:
            if row['src'] == src and row['img'] == img:
                print("Match found: src={}, img={}".format(src, img))
                return True
    print("No match found for src={}, img={}".format(src, img))
    return False


def sort_by_second_column(line):
    columns = line.split()
    return columns[1]


def remove_duplicates_and_sort_by_second_column(input_file, output_file):
    if not os.path.exists(input_file):
        print("input file {} no exist.".format(input_file))
        return
    with open(input_file, "r") as file:
        lines = file.readlines()
    unique_lines = list(set(lines))
    unique_lines.sort(key=sort_by_second_column)
    with open(output_file, "w") as output_file:
        for line in unique_lines:
            output_file.write(line)


def tab_to_spaces(input_file, output_file):
    with open(input_file, "r") as file:
        content = file.read()
    content = content.replace("\t", " ")
    with open(output_file, "w") as output:
        output.write(content)


def generate_modversions_from_src_to_dst(input_file, output_file):
    if not os.path.exists(input_file):
        print("input file {} no exist.".format(input_file))
        return
    # read content from file
    with open(input_file, "r") as file:
        lines = file.readlines()

    columns = [line.split()[:2] for line in lines if line.strip() and len(line.split()) >= 2]

    # determine if the data is valid
    if not columns:
        print("No valid data found in {}.".format(out_put))
        return

    # write the extracted content into a new file
    with open(output_file, "w") as output:
        for col1, col2 in columns:
            output.write("{} {}\n".format(col1, col2))


def compare_symbols_modversions(current_file, base_file, output_file):
    if not os.path.exists(base_file) or not os.path.exists(current_file):
        print("input file {} or {} no exist.".format(base_file, current_file))
        return

    with open(current_file, "r") as f1:
        data1 = [line.split() for line in f1]
        data1 = [(item[0], item[1]) for item in data1 if len(item) == 2]

    with open(base_file, "r") as f2:
        data2 = [line.split() for line in f2]
        data2 = [(item[0], item[1]) for item in data2 if len(item) == 2]

    with open(output_file, "w") as output:
        for item1 in data1:
            for item2 in data2:
                if item1[1] == item2[1] and item1[0] != item2[0]:
                    result_line = "{}: {} {}\n{}: {} {}\n".format(current_file, item1[0], item1[1], base_file, item2[0],
                                                                  item2[1])
                    output.write(result_line)
                    print(result_line)


def find_vmlinux_file(path_patterns, kernel_version, ack):
    """
    查找指定路径模式下是否存在 vmlinux 文件。

    :param path_patterns: 包含路径模式的列表
    :param kernel_version: 内核版本号，例如 '6.1', '6.6'
    :param ack: 内核版本号，例如 oki gki ogki
    :return: 找到的 vmlinux 文件路径，如果没有找到则返回 None
    """
    # 遍历路径模式，查找 vmlinux 文件
    for pattern in path_patterns:
        # 检查路径模式中是否包含 {kernel_version} 占位符
        if '{kernel_version}' in pattern:
            formatted_pattern = pattern.format(kernel_version=kernel_version)
        else:
            formatted_pattern = pattern

        # 使用 glob 查找匹配的路径
        for path in glob.glob(formatted_pattern):
            vmlinux_path = os.path.join(path, 'vmlinux')
            if os.path.exists(vmlinux_path):
                print("Found {} vmlinux file at: {}".format(ack, vmlinux_path))
                return path.rstrip('/')

    print("No {} vmlinux file found.".format(ack))
    return None


def generate_src_and_targets(source_directory, build_type, src_and_dst_templates, src_and_targets):
    """
    根据传递的参数生成 src_and_targets 数组，并检查第一列文件是否存在。

    :param source_directory: 源目录路径，例如 'vendor/aosp_gki/kernel-6.6/aarch64/'
    :param build_type: 构建类型，例如 'gki'
    :param src_and_targets: 包含源路径和目标路径模板的列表
    :param src_and_dst_templates: 包含源路径和目标路径模板的templates
    :return: 存在文件的 src_and_targets 数组，如果没有找到任何文件则返回空列表
    """

    for src, target in src_and_dst_templates:
        formatted_src = src
        formatted_target = target
        if '{source_path}' in src:
            formatted_src = src.format(source_path=source_directory)
        if '{type}' in target:
            formatted_target = target.format(type=build_type)

        src_files = glob.glob(formatted_src)

        if not src_files:
            # print("No files found matching {}".format(formatted_src))
            continue

        if len(src_files) > 1 and not os.path.isdir(formatted_target):
            raise ValueError("Multiple source files found {} but target {} is not a directory.".format(src_files,formatted_target))

        for src_file in src_files:
            if os.path.isdir(formatted_target):
                # 如果目标是目录，创建同名文件
                target_file = os.path.join(formatted_target, os.path.basename(src_file))
            else:
                target_file = formatted_target
            if os.path.exists(src_file):
                src_and_targets.append((src_file, target_file))

    return src_and_targets


def get_resource_path(relative_path):
    base_path = os.path.dirname(os.path.realpath(__file__))
    return os.path.join(base_path, relative_path)


def parse_cmd_args():
    parser = argparse.ArgumentParser(description="abi check for oki gki ogki")
    parser.add_argument('--build_config', type=str, help='build_config', default="")
    parser.add_argument('--main_kernel_version', type=str, help='linux kernel main version', default="")
    parser.add_argument('--full_kernel_version', type=str, help='linux kernel full version', default="")
    parser.add_argument('--vendor', type=str, help='vendor', default="qcom")
    parser.add_argument('--extract_symbols', type=str, help='extract_symbols', default="")

    parser.add_argument('-z', '--lz4', type=str, help='lz4 process tool', default=get_resource_path('lz4'))
    parser.add_argument('-n', '--unpack_bootimg', type=str, help='boot unpack tool',
                        default=get_resource_path('unpack_bootimg'))

    args = parser.parse_args()
    return args


def set_build_env(args):
    (
        args.main_kernel_version,
        args.build_config,
        args.vendor,
        args.extract_symbols,
        args.full_kernel_version
    ) = common.set_common_build_env()


def find_ack_path(kernel_version, ack):
    gki_path_patterns = [
        "vendor/aosp_gki/kernel-{kernel_version}/aarch64/gki/",
        "vendor/aosp_gki/kernel-{kernel_version}/aarch64/",
        "kernel_platform/oplus/platform/aosp_gki/gki/",
        "kernel_platform/oplus/platform/aosp_gki/",
    ]
    ogki_path_patterns = [
        "vendor/aosp_gki/kernel-{kernel_version}/aarch64/ogki/",
        "kernel_platform/oplus/platform/aosp_gki/ogki/",
    ]
    oki_path_patterns = [
        'out_krn/target/product/vnd/obj/KLEAF_OBJ/dist/kernel_device_modules-*/*kernel_aarch64.user/',
        'out/target/product/vnd/obj/KLEAF_OBJ/dist/kernel_device_modules-*/*kernel_aarch64.user/',
        'kernel_platform/out/msm-kernel-*/dist/'
    ]

    # 使用字典存储路径模式列表
    path_patterns_dict = {
        'gki': gki_path_patterns,
        'ogki': ogki_path_patterns,
        'oki': oki_path_patterns
    }

    ack_path_patterns = path_patterns_dict[ack]
    ack_path = find_vmlinux_file(ack_path_patterns, kernel_version, ack)
    return ack_path


def find_ko_path(args):
    src_dir_list_qcom = [
        pathlib.Path("out/target/product/vnd/dlkm/lib/modules"),
        pathlib.Path("out/target/product/vnd/system_dlkm/lib/modules"),
        pathlib.Path("out/target/product/vnd/odm_dlkm/lib/modules"),
        pathlib.Path("out/target/product/vnd/vendor_dlkm/lib/modules"),
        pathlib.Path("out/target/product/vnd/vendor_ramdisk/lib/modules")
    ]
    src_dir_list_mtk = [
        pathlib.Path("out_krn/target/product/vnd/symbols/*/system/lib/modules"),
        pathlib.Path("out/target/product/vnd/symbols/*/system/lib/modules")
    ]
    if args.vendor == "qcom":
        existing_paths = filter_existing_paths(src_dir_list_qcom)
    else:
        existing_paths = filter_existing_paths(src_dir_list_mtk)

    print("Existing paths:")
    for path in existing_paths:
        print(path)
    return existing_paths


def prepare_source(args, src_and_targets, acks, out_path='out/oplus'):
    link_file_list = [
        ('kernel/oplus/config/modules.filter.*', 'out/oplus/'),
        ('kernel_platform/oplus/config/modules.filter.*', 'out/oplus/')
    ]
    src_and_dst_templates = [
        ('{source_path}/vmlinux.symvers', 'out/oplus/{type}/vmlinux.symvers'),
        ('{source_path}/vmlinux', 'out/oplus/{type}/vmlinux'),
    ]

    src_and_dst_path = [
        ('kernel/oplus/config/abi_symbols_tmp_approval.csv', 'out/oplus/{type}/abi_symbols_tmp_approval.csv'),
        ('kernel_platform/oplus/config/abi_symbols_tmp_approval.csv', 'out/oplus/{type}/abi_symbols_tmp_approval.csv'),
        ('kernel/oplus/config/src_img_approval.csv', 'out/oplus/{type}/src_img_approval.csv'),
        ('kernel_platform/oplus/config/src_img_approval.csv', 'out/oplus/{type}/src_img_approval.csv'),
        ('out/target/product/vnd/vendor_ramdisk/lib/modules/modules.load.recovery',
         'out/oplus/{type}/modules.load.recovery')
    ]

    generate_src_and_targets("", "", link_file_list, src_and_targets)
    for ack in acks:

        out_put_path = out_path + '/' + ack
        if not os.path.exists(out_put_path):
            os.makedirs(out_put_path, exist_ok=True)
        ack_path = find_ack_path(args.main_kernel_version, ack)
        if ack_path:
            system_dlkm = ack_path + '/system_dlkm.img'
            if os.path.exists(system_dlkm):
                print("Found system_dlkm.img in {}".format(system_dlkm))
                common.unpack_dlkm_image(system_dlkm)
            else:
                print("{} not exist ignore\n".format(system_dlkm))

            generate_src_and_targets(ack_path, ack, src_and_dst_templates, src_and_targets)
            generate_src_and_targets("", ack, src_and_dst_path, src_and_targets)

    if src_and_targets:
        print("\nGenerated src_and_targets:")
        for src, target in src_and_targets:
            print("  {} -> {}".format(src, target))

    print("create_symlinks for vmlinux and config file...")
    common.create_symlinks_from_pairs(src_and_targets)


def filter_existing_paths(src_dir_list):
    """
    过滤存在的路径，并返回一个包含所有存在路径的列表。

    :param src_dir_list: 包含路径的列表
    :return: 存在的路径列表
    """
    existing_paths = []

    for path in src_dir_list:
        # 如果路径包含通配符，使用 glob 扩展路径
        if '*' in str(path):
            glob_paths = glob.glob(str(path))
            for glob_path in glob_paths:
                p = pathlib.Path(glob_path)
                if p.exists():
                    existing_paths.append(p)
        else:
            # 检查路径是否存在
            if path.exists():
                existing_paths.append(path)

    return existing_paths


def find_all_ko(args, acks):
    for ack in acks:
        print("\ncreate_symlinks_for {} ko".format(ack))
        existing_paths = find_ko_path(args)
        new_path = existing_paths.copy()
        out_put_path = pathlib.Path(out_put + '/' + ack)
        ack_path = find_ack_path(args.main_kernel_version, ack)
        if ack_path:
            system_dlkm = ack_path + '/system_dlkm'
            if os.path.exists(system_dlkm):
                print("system_dlkm ko exist in {}".format(system_dlkm))
                new_path.append(system_dlkm)
        common.src_to_dst_from_directories(new_path, "*.ko", out_put_path)


def check_img_src_help():
    print("Check if the source code for boot.img and system_dlkm.img is compatible.")
    print("检查 source code boot.img system_dlkm.img 是否兼容:")
    print("如需帮助，请阅读如下指导文档:")
    print("")


def check_img_src_match(args):
    acks = ['gki', 'ogki']
    for ack in acks:
        ack_path = find_ack_path(args.main_kernel_version, ack)
        if ack_path:
            out_put_path = pathlib.Path(out_put + '/' + ack)
            src_img_approval_csv = "{}/src_img_approval.csv".format(out_put_path)
            system_dlkm = ack_path + '/system_dlkm'
            bootimg = os.path.join(ack_path, "boot.img")
            unpack = os.path.join(ack_path, "unpack")
            kernel = os.path.join(unpack, "kernel")
            src_version = args.full_kernel_version

            boot_strings = common.get_boot_img_version(args.unpack_bootimg, bootimg, unpack, kernel, args.lz4)
            print("\nboot_strings {} ".format(boot_strings))
            match = re.search(r'(\d+\.\d+\.\d+)-', boot_strings)
            if match:
                boot_version = match.group(1)
            else:
                boot_version = ''
            print("\nsrc_version {}  boot_version{} ".format(src_version, boot_version))

            if boot_version != src_version:
                print("linux kernel src version {} does not match boot.img {}  ".format(src_version, boot_version))
                approval = check_src_img_approval(src_img_approval_csv, src_version, boot_version)
                if not approval:
                    print("approval is {} force exit".format(approval))
                    check_img_src_help()
                    sys.exit(1)
                else:
                    print("approval is {} src img in approval list ignore".format(approval))

            if os.path.exists(system_dlkm):
                print("system_dlkm ko exist in {}".format(system_dlkm))
                system_dlkm_version = find_system_dlkm_version(system_dlkm)
                print("\nsystem_dlkm_version: {} boot_version:{} ".format(system_dlkm_version, boot_strings))
                if system_dlkm_version != boot_strings:
                    print("system_dlkm.img/boot.img version not match")
                    check_img_src_help()
                    sys.exit(1)


def symbols_crc_check(acks, output_dir):
    for ack in acks:
        print("\ngenerate {} ko modversions".format(ack))
        ack_path = str(pathlib.Path(output_dir) / ack)
        ack_log = str(pathlib.Path(log_dir) / ack)
        ack_vmlinux = str(pathlib.Path(ack_path) / 'vmlinux')

        if not os.path.exists(ack_vmlinux):
            print("path:{} not exist ignore".format(ack_vmlinux))
            continue
        abi_required_full = "{}/abi_{}_required_full.txt".format(ack_path, ack)
        abi_required_symbol = "{}/abi_{}_required_symbol.txt".format(ack_path, ack)
        abi_required_ko = "{}/abi_{}_required_ko.txt".format(ack_path, ack)
        abi_required_symbol_ko = "{}/abi_{}_required_symbol_ko.txt".format(ack_path, ack)
        abi_required_and_remove_tmp_approval = "{}/abi_{}_required_and_remove_tmp_approval.txt".format(ack_path, ack)
        recovery = "{}/modules.load.recovery".format(ack_path)
        abi_compare_recovery_ko_need_symbol = "{}/abi_{}_compare_recovery_ko_need_symbol.txt".format(ack_path, ack)
        vmlinux_symvers = "{}/vmlinux.symvers".format(ack_path)
        vmlinux_modversions_all_crc_symbols = "{}/vmlinux_{}_modversions_all_crc_symbols".format(ack_path, ack)
        modversions_duplicates_sort = "{}/vmlinux_{}_modversions_crc_duplicates_sort".format(ack_path, ack)
        symvers_ko_result = "{}/check_{}_symvers_ko_result.txt".format(ack_path, ack)
        ko_modversions_full = "{}/ko_modversions_full.txt".format(ack_path)
        ko_modversions_remove_duplicates_and_sort = "{}/ko_modversions_remove_duplicates_and_sort.txt".format(ack_path)
        ko_modversions_remove_spaces = "{}/ko_modversions_remove_spaces.txt".format(ack_path)
        approval_csv = "{}/abi_symbols_tmp_approval.csv".format(ack_path)
        approval_txt = "{}/abi_symbols_tmp_approval.txt".format(ack_path)


        remove_duplicates_and_sort_by_second_column(ko_modversions_full, ko_modversions_remove_duplicates_and_sort)
        tab_to_spaces(ko_modversions_remove_duplicates_and_sort, ko_modversions_remove_spaces)

        print("get_approval_config_from_file...")
        get_approval_config_from_file(approval_csv, approval_txt)

        print("get_symbols_and_ko_list...")
        # extract the second column and remove duplicates
        common.get_symbols_and_ko_list(abi_required_full, abi_required_symbol, abi_required_ko, abi_required_symbol_ko)

        compare_and_output_extra_lines(approval_txt, abi_required_symbol, abi_required_and_remove_tmp_approval)
        print("compare_and_output_extra_lines")
        check_current_status(abi_required_and_remove_tmp_approval, ack_path, ack_log, exclude_file)

        print("compare recovery install ko missing symbols...")
        compare_same_line_files(recovery, abi_required_ko, abi_compare_recovery_ko_need_symbol)
        check_current_status(abi_compare_recovery_ko_need_symbol, ack_path, ack_log, exclude_file)

        print("get modversions from vmlinux.symvers...")
        generate_modversions_from_src_to_dst(vmlinux_symvers, vmlinux_modversions_all_crc_symbols)
        remove_duplicates_and_sort_by_second_column(vmlinux_modversions_all_crc_symbols, modversions_duplicates_sort)

        print("check {} ko modversions".format(ack))
        compare_symbols_modversions(ko_modversions_remove_spaces, modversions_duplicates_sort, symvers_ko_result)
        check_current_status(symvers_ko_result, ack_path, ack_log, exclude_file)

        print("copy log to dest...")
        copy_log_files_to_dest(ack_path, ack_log, exclude_file)


def find_numeric_modules_filter(directory):
    numeric_suffixes = []
    pattern = re.compile(r'^modules\.filter\.([0-9a-fA-F]+)$')

    for filename in os.listdir(directory):
        match = pattern.match(filename)
        if match:
            suffix = match.group(1)
            try:
                # 尝试将后缀转换为十进制或十六进制
                int(suffix, 16)  # 如果可以转换为十六进制，则认为是有效的
                numeric_suffixes.append(suffix)
                print('current id {}'.format(suffix))
            except ValueError:
                continue

    return numeric_suffixes


def create_files_from_numeric_suffixes(directory, numeric_suffixes):
    filter_list = []
    # 获取环境变量 OPLUS_VND_BUILD_PLATFORM 的值
    platform_value = os.getenv('OPLUS_VND_BUILD_PLATFORM')
    # 处理平台文件
    platform_file = os.path.join(directory, 'modules.filter.{}'.format(platform_value))
    # 处理 common 文件
    common_file = os.path.join(directory, 'modules.filter.common')

    if numeric_suffixes:
        for suffix in numeric_suffixes:
            source_file = os.path.join(directory, 'modules.filter.{}'.format(suffix))
            target_file = os.path.join(directory, 'local.all.{}'.format(suffix))
            filter_list.append(suffix)
            # 打开目标文件以追加内容
            with open(target_file, 'w') as dst:
                # 处理数字后缀文件
                if os.path.exists(source_file):
                    with open(source_file, 'r') as src:
                        content = src.read()
                    dst.write(content)
                    print("Created {} from {}".format(target_file, source_file))
                else:
                    print("Source file {} does not exist.".format(source_file))

                if os.path.exists(platform_file):
                    with open(platform_file, 'r') as src:
                        content = src.read()
                    dst.write(content)
                    print("Appended content from {} to {}".format(platform_file, target_file))
                else:
                    print("Source file {} does not exist.".format(platform_file))

                if os.path.exists(common_file):
                    with open(common_file, 'r') as src:
                        content = src.read()
                    dst.write(content)
                    print("Appended content from {} to {}".format(common_file, target_file))
                else:
                    print("Source file {} does not exist.".format(common_file))

    elif os.path.exists(platform_file):
        target_file = os.path.join(directory, 'local.all.{}'.format(platform_value))
        filter_list.append(platform_value)
        # 打开目标文件以追加内容
        with open(target_file, 'w') as dst:
            if os.path.exists(platform_file):
                with open(platform_file, 'r') as src:
                    content = src.read()
                dst.write(content)
                print("Appended content from {} to {}".format(platform_file, target_file))
            else:
                print("Source file {} does not exist.".format(platform_file))

            if os.path.exists(common_file):
                with open(common_file, 'r') as src:
                    content = src.read()
                dst.write(content)
                print("Appended content from {} to {}".format(common_file, target_file))
            else:
                print("Source file {} does not exist.".format(common_file))

    elif os.path.exists(common_file):
        target_file = os.path.join(directory, 'local.all.common')
        filter_list.append('common')
        # 打开目标文件以追加内容
        with open(target_file, 'w') as dst:
            if os.path.exists(common_file):
                with open(common_file, 'r') as src:
                    content = src.read()
                dst.write(content)
                print("Appended content from {} to {}".format(common_file, target_file))
            else:
                print("Source file {} does not exist.".format(common_file))

    return filter_list

def copy_files_to_directories(directory, filter_list, source_directory):
    for filter_item in filter_list:
        target_dir = os.path.join(directory, filter_item)
        os.makedirs(target_dir, exist_ok=True)
        print("Created directory {}".format(target_dir))

        for filename in os.listdir(source_directory):
            source_file = os.path.join(source_directory, filename)
            target_file = os.path.join(target_dir, filename)

            if os.path.islink(source_file):
                # 如果是符号链接，直接拷贝符号链接
                os.symlink(os.readlink(source_file), target_file)
                # print("Copied symbolic link {} to {}".format(source_file, target_file))
            else:
                # 否则，拷贝文件
                shutil.copy2(source_file, target_file)
                print("Copied file {} to {}".format(source_file, target_file))

def read_config_file(config_file_path):
    """
    读取配置文件并返回每一行的内容列表。

    :param config_file_path: 配置文件的路径
    :return: 包含配置文件每一行内容的列表
    """
    try:
        with open(config_file_path, 'r') as file:
            lines = file.readlines()
        return [line.strip() for line in lines if line.strip()]
    except FileNotFoundError:
        print("Config file {} not found.".format(config_file_path))
        return []
    except Exception as e:
        print("Error reading config file {}: {}".format(config_file_path, e))
        return []

def delete_files_or_symlinks(directory, file_list):
    """
    根据文件列表删除指定目录内的文件或软链接。

    :param directory: 目录路径
    :param file_list: 包含文件名的列表
    """
    for filename in file_list:
        file_path = os.path.join(directory, filename)
        if os.path.exists(file_path):
            if os.path.islink(file_path):
                os.unlink(file_path)
                print("Deleted symbolic link: {}".format(file_path))
            else:
                os.remove(file_path)
                print("Deleted file: {}".format(file_path))
        else:
            print("File or symbolic link {} does not exist.".format(file_path))

def remove_filter_files(directory, filter_list):
    for filter_item in filter_list:
        config_file_path = os.path.join(directory, 'local.all.{}'.format(filter_item))
        filter_path = os.path.join(directory, filter_item)
        # 读取配置文件
        file_list = read_config_file(config_file_path)
        # 删除文件或软链接
        delete_files_or_symlinks(filter_path, file_list)


def main():
    src_and_targets = []
    acks = ['oki', 'gki', 'ogki']

    args = parse_cmd_args()
    set_build_env(args)
    common.delete_all_exists_file(out_put)
    prepare_source(args, src_and_targets, acks)
    find_all_ko(args, acks)

    numeric_suffixes = find_numeric_modules_filter(out_put)
    filter_list = create_files_from_numeric_suffixes(out_put, numeric_suffixes)
    copy_files_to_directories(out_put, filter_list, os.path.join(out_put, 'gki'))
    remove_filter_files(out_put, filter_list)

    generate_abis_modversions(args, acks + filter_list , out_put)
    symbols_crc_check(acks + filter_list, out_put)
    check_img_src_match(args)


if __name__ == "__main__":
    start_time = datetime.now()
    print("Begin time:", start_time.strftime("%Y-%m-%d %H:%M:%S"))
    print("abi_gki_oki_check.py start ...\n")
    main()
    print("abi_gki_oki_check.py end !!!\n")
    end_time = datetime.now()
    print("End time:", end_time.strftime("%Y-%m-%d %H:%M:%S"))
    elapsed_time = end_time - start_time
    print("Total time:", common.format_duration(elapsed_time))
