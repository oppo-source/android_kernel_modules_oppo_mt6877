import gzip
import shutil
import os
import subprocess
import glob
import pathlib
import sys
import re
import secure_upload


def is_sparse_image(file_path):
    """
    Check if the file is a sparse image by examining its header.

    :param file_path: Path to the file.
    :return: True if the file is a sparse image, False otherwise.
    """
    with open(file_path, 'rb') as f:
        header = f.read(28)
        # Sparse image header magic number is 0xED26FF3A
        return header[:4] == b'\x3A\xFF\x26\xED'


def convert_sparse_to_raw(sparse_img_path, simg2img_tool, raw_img_path=None):
    """
    Convert a sparse image to a raw image using simg2img.

    :param sparse_img_path: Path to the sparse image (input).
    :param simg2img_tool: Path to the simg2img tool (input).
    :param raw_img_path: Path to the raw image (output). If None, rename and convert the sparse image.
    """
    # Ensure the input sparse image exists
    if not os.path.exists(sparse_img_path):
        raise FileNotFoundError("Sparse image not found at {}".format(sparse_img_path))

    # Check if the file is a sparse image
    if not is_sparse_image(sparse_img_path):
        print("The file {} is not a sparse image.".format(sparse_img_path))
        return

    # Determine the raw image path
    if raw_img_path is None:
        # Rename the sparse image to add _sparse suffix
        base_name, ext = os.path.splitext(sparse_img_path)
        raw_img_path = "{}{}".format(base_name, ext)
        new_sparse_img_path = "{}_sparse{}".format(base_name, ext)
        shutil.move(sparse_img_path, new_sparse_img_path)
        sparse_img_path = new_sparse_img_path

    # Ensure the output directory exists
    output_dir = os.path.dirname(raw_img_path)
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    # Run the simg2img command
    try:
        subprocess.run(
            [simg2img_tool, sparse_img_path, raw_img_path],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )
        print("Conversion successful: {} -> {}".format(sparse_img_path, raw_img_path))
    except subprocess.CalledProcessError as exc:
        print("Conversion failed: {}".format(exc.stderr.decode()))
        raise


def unpack_superimg_cmd(lpunpack_tool, raw_img_path, output_dir):
    """
    Unpack a super.img file using lpunpack.

    :param lpunpack_tool: Path to the lpunpack tool.
    :param raw_img_path: Path to the raw image (input).
    :param output_dir: Path to the output directory (output).
    """
    # Ensure the lpunpack tool exists
    if not os.path.exists(lpunpack_tool):
        raise FileNotFoundError("lpunpack tool not found at {}".format(lpunpack_tool))

    # Ensure the input raw image exists
    if not os.path.exists(raw_img_path):
        raise FileNotFoundError("Raw image not found at {}".format(raw_img_path))

    # Ensure the output directory exists
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    # Run the lpunpack command
    try:
        subprocess.run(
            [lpunpack_tool, raw_img_path, output_dir],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )
        print("Unpacking successful: {} -> {}".format(raw_img_path, output_dir))
    except subprocess.CalledProcessError as e:
        print("Unpacking failed: {}".format(e.stderr.decode()))
        raise


def unpack_superimg(super_img):
    output_dir = os.path.dirname(super_img)
    # Convert sparse image to raw image
    # Find simg2img in PATH
    lpunpack_tool = shutil.which('lpunpack')
    simg2img_tool = shutil.which('simg2img')

    required_tools = {
        'lpunpack_tool': lpunpack_tool,
        'simg2img_tool': simg2img_tool
    }
    for tool, path in required_tools.items():
        if path is None:
            print("Error: {} not found.".format(tool))
            sys.exit(1)
    # Unpack the raw image
    convert_sparse_to_raw(super_img, simg2img_tool)
    unpack_superimg_cmd(lpunpack_tool, super_img, output_dir)


def rename_files(base_dir, file_renames):
    """
    Rename files in the specified directory based on the provided mapping.

    :param base_dir: Base directory containing the files to be renamed.
    :param file_renames: Dictionary mapping old filenames to new filenames.
    """
    # Ensure the base directory exists
    if not os.path.exists(base_dir):
        raise FileNotFoundError("Base directory not found at {}".format(base_dir))

    # Iterate over the file renaming mapping
    for old_name, new_name in file_renames.items():
        old_path = os.path.join(base_dir, old_name)
        new_path = os.path.join(base_dir, new_name)

        # Ensure the old file exists
        if not os.path.exists(old_path):
            print("File not found: {}".format(old_path))
            continue

        # Rename the file
        try:
            os.rename(old_path, new_path)
            print("Renamed: {} -> {}".format(old_path, new_path))
        except Exception as exc:
            print("Failed to rename {} to {}: {}".format(old_path, new_path, exc))


def run_unpack_image_command(
        script_path,
        input_img,
        output_dir,
        seven_zip_path,
        erofsfuse_path,
        mount_point
):
    """
    Run the unpack_image.py script with the specified parameters.

    :param script_path: Path to the unpack_image.py script.
    :param input_img: Path to the input image file.
    :param output_dir: Path to the output directory.
    :param seven_zip_path: Path to the 7z_new tool.
    :param erofsfuse_path: Path to the erofsfuse tool.
    :param mount_point: Path to the mount point.
    """
    # Construct the command
    command = [
        "python3",
        script_path,
        "-i", input_img,
        "-o", output_dir,
        "-z", seven_zip_path,
        "-r", erofsfuse_path,
        "-m", mount_point
    ]
    # print(f"Command: {command}")
    # 将命令列表转换为字符串
    command_string = subprocess.list2cmdline(command)

    # 打印命令字符串
    print(command_string)
    # Run the command
    try:
        subprocess.run(command, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        print("Command executed successfully.\n")
    except subprocess.CalledProcessError as exc:
        print("Command failed with error: {}".format(exc.stderr.decode()))
        raise


def unpack_boot_img(unpack_bootimg_path, boot_img_path, output_dir):
    if not os.path.exists(boot_img_path):
        print("{} File does not exist.ignore".format(boot_img_path))
        return
    args = ["--boot_img", boot_img_path, "--out", output_dir, "--format=mkbootimg"]

    command = [unpack_bootimg_path] + args
    print("command:{}".format(command))

    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = process.communicate()

    if process.returncode != 0:
        print("Error executing command: ", stderr.decode())
        return False
    else:
        print("Command executed successfully: ", stdout.decode())
        return True


def process_lz4_kernel(lz4_tool_path, kernel_path):
    print("{}".format(kernel_path))
    lz4_kernel_path = "{}.lz4".format(kernel_path)
    print("{}".format(lz4_kernel_path))
    os.rename(kernel_path, lz4_kernel_path)
    with open(kernel_path, 'w') as f:
        subprocess.run([lz4_tool_path, '-d', '-c', lz4_kernel_path], stdout=f)


def decompress_gz_file(input_file_path, output_file_path):
    gz_file_path = '{}.gz'.format(input_file_path)
    os.rename(input_file_path, gz_file_path)
    with gzip.open(gz_file_path, 'rb') as f_in:
        with open(output_file_path, 'wb') as f_out:
            shutil.copyfileobj(f_in, f_out)
    os.remove(gz_file_path)


def get_compression_type(file_path, lz4):
    if not os.path.exists(file_path):
        print("{} File does not exist.ignore".format(file_path))
        return 'normal'
    command = ["file", file_path]
    print("command:{}".format(command))
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)

    if "LZ4 compressed data" in result.stdout:
        process_lz4_kernel(lz4, file_path)
        return 'lz4'
    elif "gzip compressed data" in result.stdout:
        decompress_gz_file(file_path, file_path)
        return 'gz'
    else:
        return 'normal'


def get_boot_img_version(unpack_bootimg, bootimg, unpack, kernel, lz4):
    print("\nunpack boot.img")
    unpack_boot_img(unpack_bootimg, bootimg, unpack)
    print("get kernel compression type and decompress")
    compression_type = get_compression_type(kernel, lz4)
    print("current compression type: {}\n".format(compression_type))
    version = find_linux_versions(kernel)
    print("linux_kernel_version: {}\n".format(version))
    return version


def get_paths(input_img_path, mnt_type="mnt"):
    """
    Get the input image path, output directory, and mount point from the input image path.
    :param mnt_type: mount type.
    :param input_img_path: Path to the input image.
    :return: Tuple containing input_img, output_dir, and mount_point.
    """
    # Get the directory path of the input image
    input_dir = os.path.dirname(input_img_path)

    # Get the base name of the input image without the extension
    base_name, ext = os.path.splitext(os.path.basename(input_img_path))

    # Construct the output directory path
    output_dir = os.path.join(input_dir, base_name)

    # Construct the mount point path
    mount_point = os.path.join(input_dir, "{}_{}".format(base_name, mnt_type))

    return input_img_path, output_dir, mount_point


def unpack_cpio(archive_path, output_dir):
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    unpack_cmd = [
        'cpio',
        '-idmv',
        '-F', archive_path
    ]

    # Change the current working directory to the output directory
    current_dir = os.getcwd()
    try:
        os.chdir(output_dir)
        subprocess.run(unpack_cmd, check=True)
    finally:
        os.chdir(current_dir)


def find_vendor_ramdisk_files(base_dir, max_suffix=5):
    """
    Find vendor_ramdisk files in the specified directory.

    :param base_dir: Base directory to search in.
    :param max_suffix: Maximum suffix number to check (e.g., 5 for vendor_ramdisk00 to vendor_ramdisk05).
    :return: List of dictionaries containing file path and file name.
    """
    found_files = []

    # Check for vendor_ramdisk without suffix
    file_path = os.path.join(base_dir, 'vendor_ramdisk')
    if os.path.isfile(file_path):
        found_files.append({
            'path': file_path,
            'name': 'vendor_ramdisk'
        })

    # Check for vendor_ramdisk with suffixes from 00 to max_suffix
    for i in range(max_suffix + 1):
        suffix = "{:02d}".format(i)  # Format suffix as two digits (e.g., 00, 01, ..., 05)
        file_path = os.path.join(base_dir, 'vendor_ramdisk{}'.format(suffix))
        if os.path.isfile(file_path):
            found_files.append({
                'path': file_path,
                'name': 'vendor_ramdisk{}'.format(suffix)
            })

    return found_files


def unpack_vendor_boot_img(boot_img):
    print("\nunpack boot.img")
    unpack_bootimg = shutil.which('unpack_bootimg')
    lz4 = shutil.which('lz4')

    required_tools = {
        'unpack_bootimg.py': unpack_bootimg,
        'lz4': lz4
    }
    for tool, path in required_tools.items():
        if path is None:
            print("Error: {} not found.".format(tool))
            sys.exit(1)

    if os.path.exists(boot_img):
        input_img, output_dir, vendor_ramdisk = get_paths(boot_img, "ramdisk")
        unpack_boot_img(unpack_bootimg, input_img, vendor_ramdisk)
        # 查找 vendor_ramdisk 文件
        found_files = find_vendor_ramdisk_files(vendor_ramdisk, max_suffix=5)

        # 打印找到的文件路径及文件名
        for file_info in found_files:
            # path = file_info['path']
            name = file_info['name']
            vendor_ramdisk_local = os.path.join(vendor_ramdisk, name)
            vendor_ramdisk_out = os.path.join(output_dir, name)
            compression_type = get_compression_type(vendor_ramdisk_local, lz4)
            print(compression_type)
            # 检查并删除 vendor_ramdisk_out 路径
            if os.path.exists(vendor_ramdisk_out):
                if os.path.isdir(vendor_ramdisk_out):
                    shutil.rmtree(vendor_ramdisk_out)
                    print("Deleted directory: {}".format(vendor_ramdisk_out))
                else:
                    os.remove(vendor_ramdisk_out)
                    print("Deleted file: {}".format(vendor_ramdisk_out))

            unpack_cpio(vendor_ramdisk_local, vendor_ramdisk_out)


# Example usage
def unpack_dlkm_image(dlkm_img):
    # Find simg2img in PATH
    script_path = shutil.which('unpack_image.py')
    seven_zip_path = shutil.which('7z_new')
    erofsfuse_path = shutil.which('erofsfuse')
    simg2img_tool = shutil.which('simg2img')

    required_tools = {
        'unpack_image.py': script_path,
        '7z_new': seven_zip_path,
        'erofsfuse': erofsfuse_path,
        'simg2img': simg2img_tool
    }
    for tool, path in required_tools.items():
        if path is None:
            print("Error: {} not found.".format(tool))
            sys.exit(1)

    if os.path.exists(dlkm_img):
        input_img, output_dir, mount_point = get_paths(dlkm_img)
        # 打印路径
        convert_sparse_to_raw(dlkm_img, simg2img_tool)
        run_unpack_image_command(
            script_path,
            input_img,
            output_dir,
            seven_zip_path,
            erofsfuse_path,
            mount_point
        )

    return


def find_kernel_version(base_path):
    """
    查找指定路径下是否存在 kernel/kernel-* 目录，并返回版本号。

    :param base_path: 基础路径，例如 'kernel/'
    :return: 找到的版本号，如果没有找到则返回 None
    """
    # 定义路径模式
    pattern = os.path.join(base_path, 'kernel-*')

    # 使用 glob 查找匹配的路径
    for path in glob.glob(pattern):
        # 提取版本号
        version = os.path.basename(path).split('-')[-1]
        print("Found kernel directory: {} with version: {}".format(path, version))
        return version

    # print("No kernel directory found.")
    return None


def find_build_config_constants(kernel_version):
    # 定义路径模板
    kernel_path_template = 'kernel/kernel-{kernel_version}'
    common_path = 'kernel_platform/common'

    # 检查 kernel 特定版本路径是否存在
    kernel_path = kernel_path_template.format(kernel_version=kernel_version)
    if os.path.exists(kernel_path):
        build_config_path = os.path.join(kernel_path, 'build.config.constants')
        vendor = "mtk"
    elif os.path.exists(common_path):
        build_config_path = os.path.join(common_path, 'build.config.constants')
        vendor = "qcom"
    else:
        print("Neither kernel/{kernel_version} nor kernel_platform/common exists.")
        return None, None, None

    # 检查 build.config.constants 文件是否存在
    if not os.path.exists(build_config_path):
        print("build.config.constants not found at: {}".format(build_config_path))
        return None, None, None

    # 检查 Makefile 文件是否存在
    makefile_path = os.path.join(os.path.dirname(build_config_path), 'Makefile')
    if not os.path.exists(makefile_path):
        print("Makefile not found at: {}".format(makefile_path))
        return build_config_path, vendor, None

    # 读取 Makefile 文件内容
    with open(makefile_path, 'r') as file:
        makefile_content = file.read()

    # 查找版本号
    version_match = re.search(r'VERSION\s*=\s*(\d+)', makefile_content)
    patchlevel_match = re.search(r'PATCHLEVEL\s*=\s*(\d+)', makefile_content)
    sublevel_match = re.search(r'SUBLEVEL\s*=\s*(\d+)', makefile_content)

    if version_match and patchlevel_match and sublevel_match:
        version = version_match.group(1)
        patchlevel = patchlevel_match.group(1)
        sublevel = sublevel_match.group(1)
        full_version = "{}.{}.{}".format(version, patchlevel, sublevel)
        return build_config_path, vendor, full_version
    else:
        print("Version information not found in Makefile.")
        return build_config_path, vendor, None


def set_env_from_file(filepath):
    if not os.path.exists(filepath):
        return

    with open(filepath, 'r') as file:
        for line in file:
            # Remove any whitespace and newline characters, then check
            # if the line contains environment variable definitions
            line = line.strip()
            if line and not line.startswith('#'):  # Ignore blank lines and comments
                key, _, value = line.partition('=')
                if key and value:
                    # Set the environment variable in the current process's environment
                    os.environ[key] = value
                    # Print the variable (if required)
                    # print("Set {}={}".format(key, value))


def find_clang_bin_path():
    # 定义 clang 路径模板
    clang_bin_templates = [
        "kernel/prebuilts/clang/host/linux-x86/clang-{clang_version}/bin/",
        "kernel_platform/prebuilts/clang/host/linux-x86/clang-{clang_version}/bin/"
    ]

    clang_version = os.environ.get('CLANG_VERSION', '6.0')  # 默认值为 '6.0'，可以根据需要调整

    # 替换路径模板中的 {clang_version}
    paths_to_check = [path.format(clang_version=clang_version) for path in clang_bin_templates]

    # 遍历路径列表，检查路径是否存在
    for path in paths_to_check:
        if os.path.exists(path):
            print("Found clang bin path: {}".format(path))
            # os.environ["PATH"] = path + os.environ["PATH"]
            os.environ["PATH"] = path + ":" + os.environ["PATH"]
            return path

    print("No clang bin path found.")
    return None


def find_build_tools_path():
    # 定义 build-tools 路径列表
    build_tools_paths = [
        'out/host/linux-x86/bin/',
        'out_hal/host/linux-x86/bin/',
        "kernel/build/kernel/build-tools/path/linux-x86",
        'kernel/prebuilts/kernel-build-tools/linux-x86/bin/',
        "kernel_platform/build/build-tools/path/linux-x86",
        'kernel_platform/oplus/tools/',
        'kernel/oplus/tools/'
    ]

    # 遍历路径列表，检查路径是否存在
    for path in build_tools_paths:
        if os.path.exists(path):
            print("Found build-tools path: {}".format(path))
            # os.environ["PATH"] = path + os.environ["PATH"]
            os.environ["PATH"] = path + ":" + os.environ["PATH"]

    # print("No build-tools path found.")


def find_existing_extract_symbols():
    """
    检查指定的文件路径是否存在，如果找到任何一个文件，则停止查找并返回该文件的路径，并打印存在的目录。

    :return: 存在的文件路径，如果没有找到任何文件则返回 None
    """
    file_paths = [
        "kernel/build/abi/extract_symbols",
        "kernel_platform/build/abi/extract_symbols"
    ]
    for file_path in file_paths:
        path = pathlib.Path(file_path)
        if path.exists():
            print("Found extract_symbols at: {}".format(path))
            return path
    print("No extract_symbols file found in the specified paths.")
    return None


def set_common_build_env():
    main_version = find_kernel_version('kernel/')
    build_config, vendor, full_version = find_build_config_constants(main_version)
    if build_config:
        set_env_from_file(build_config)
        find_clang_bin_path()

    find_build_tools_path()
    extract_symbols = find_existing_extract_symbols()

    return main_version, build_config, vendor, extract_symbols, full_version


def delete_all_exists_file(folder_path):
    if not os.path.exists(folder_path):
        os.makedirs(folder_path)

    for item in os.listdir(folder_path):
        item_path = os.path.join(folder_path, item)
        if os.path.isdir(item_path):
            shutil.rmtree(item_path)
        else:
            os.remove(item_path)


def create_symlinks_from_pairs(src_and_targets):
    for src_pattern, target_filename in src_and_targets:
        src_files = glob.glob(src_pattern)
        directory = os.path.dirname(target_filename)
        if not os.path.exists(directory):
            os.makedirs(directory)

        if len(src_files) == 1:
            src_filename = src_files[0]

            if not os.path.exists(src_filename):
                print("Source file does not exist: {}".format(src_filename))
                continue

            src_absolute_path = pathlib.Path(src_filename).resolve()  # obtain absolute path
            if os.path.exists(target_filename):
                os.remove(target_filename)
            os.symlink(str(src_absolute_path), target_filename)
        elif len(src_files) > 1:
            print("Multiple source files match the pattern '{}'. Cannot create symlink.".format(src_pattern))
        else:
            print("No source files match the pattern '{}'. Cannot create symlink.".format(src_pattern))


def src_to_dst_from_directories(src_dir_list, file_pattern, target_dir, action='link'):
    # print( f"Starting function with src_dir_list={src_dir_list}, file_pattern={file_pattern}, target_dir={
    # target_dir}, action={action}")

    target_path = pathlib.Path(target_dir)
    # print(f"Creating target directory: {target_path}")
    target_path.mkdir(parents=True, exist_ok=True)

    for src_dir in src_dir_list:
        # print(f"Processing source directory: {src_dir}")
        src_path = pathlib.Path(src_dir)
        for file in src_path.rglob(file_pattern):
            file_absolute_path = file.resolve()  # obtain absolute path
            # print(f"Found file: {file_absolute_path}")
            symlink_target = target_path / file.name

            if symlink_target.exists():
                # print(f"Removing existing target: {symlink_target}")
                os.remove(str(symlink_target))

            if action == 'link':
                # print(f"Creating symlink from {file_absolute_path} to {symlink_target}")
                os.symlink(str(file_absolute_path), str(symlink_target))
            elif action == 'copy':
                # print(f"Copying file from {file_absolute_path} to {symlink_target}")
                shutil.copy2(str(file_absolute_path), str(symlink_target))
            else:
                # print(f"Invalid action: {action}")
                raise ValueError("Action must be either 'link' or 'copy'")


def format_duration(td):
    minutes, seconds = divmod(td.seconds, 60)
    hours, minutes = divmod(minutes, 60)
    return '{:02} Hour {:02} Minute {:02} Second'.format(hours, minutes, seconds)


def download_file_from_remote(local_path, remote_path):
    curl_command = [
        "curl",
        "-f",  # Fail silently (no output at all) on server errors
        "-s",  # Silent mode
        "--write-out", "%{http_code}",  # Write HTTP status code to output
        "-o", local_path,
        remote_path
    ]
    # curl_command_str = ' '.join(curl_command)
    # print("Downloading {} to {} by cmd {}...".format(remote_path, local_path,curl_command_str))
    try:
        result = subprocess.run(curl_command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
        output = result.stdout.strip()
        http_code = output[-3:]  # Get the last 3 characters which represent the HTTP status code
        if result.returncode == 0 and http_code == "200":
            print("Successfully downloaded {} to {}".format(remote_path, local_path))
        else:
            print("output: {} result{}".format(output, result))
            print("Failed to download from {} to {}".format(remote_path, local_path))
            print("Error: stderr {} stdout {} return code {}".format(result.stderr, result.stdout, result.returncode))
        if not os.path.exists(local_path):
            print("Error: {} not exist please check again\n".format(local_path))

    except Exception as e:
        print("Exception occurred while downloading from {} to {}: {}".format(remote_path, local_path, e))


def download_files_from_server(local_dir, file_paths, remote_base_address):
    if not os.path.exists(local_dir):
        os.makedirs(local_dir)

    for file_name in file_paths:
        local_path = os.path.join(local_dir, file_name)
        remote_path = os.path.join(remote_base_address, file_name)
        download_file_from_remote(local_path, remote_path)


def copy_files_form_src_to_dst(dst, file_names, src):
    if not os.path.exists(dst):
        os.makedirs(dst)

    for file_name in file_names:
        src_path = os.path.join(src, file_name)
        dest_path = os.path.join(dst, file_name)

        if os.path.exists(src_path):
            print("cp {} to {}".format(src_path, dest_path))
            shutil.copy2(src_path, dest_path)
        else:
            print("Source file {} does not exist. Skipping.".format(src_path))


def get_symbols_and_ko_list(input_file, output_list, output_ko, output_symbol_ko, delimiter=' '):
    unique_symbols = set()
    unique_ko = set()
    unique_symbols_ko = set()
    with open(input_file, 'r') as f_in:
        for line in f_in:
            columns = line.strip().split(delimiter)
            if len(columns) >= 4:
                unique_symbols.add(columns[1])
                unique_ko.add(columns[4])
                entry = (columns[1], columns[4])
                unique_symbols_ko.add(entry)

    with open(output_list, 'w') as f_out:
        # print("missing symbols is:")
        for item in unique_symbols:
            # print(item)
            f_out.write(item + '\n')

    with open(output_ko, 'w') as ko_out:
        # print("symbols is needed by:")
        for item in unique_ko:
            # print(item)
            ko_out.write(item + '\n')

    with open(output_symbol_ko, 'w') as s_ko_out:
        for symbol, ko in unique_symbols_ko:
            s_ko_out.write(symbol + ' ' + ko + "\n")


def find_linux_versions(file_path):
    if not os.path.exists(file_path):
        print("{} File does not exist,ignore".format(file_path))
        return "0"
    command = "strings {} | grep 'Linux version.*SMP PREEMPT.*'".format(file_path)
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True,
                            shell=True)

    lines = result.stdout.strip().split('\n')
    print(lines)
    if not lines or lines == ['']:
        print("Cannot find version information in {}".format(lines))
        return "0"

    versions = [line.split()[2] for line in lines]
    print(versions)
    if len(versions) > 1 and len(set(versions)) > 1:
        print("Version information is inconsistent len={}".format(len(versions)))
        return "0"
    elif versions:
        return versions[0]
    else:
        print("Cannot find version information default")
        return "0"


def upload_file(local_path, remote_path):
    # 获取当前脚本所在目录
    script_dir = os.path.dirname(os.path.abspath(__file__))
    secure_upload_path = os.path.join(script_dir, "secure_upload")

    # 检查 secure_upload 文件是否存在
    if not os.path.exists(secure_upload_path):
        print("Error: {} not found.".format(secure_upload_path))
        return

    command = [
        secure_upload_path,
        "-l", local_path,
        "-r", remote_path
    ]

    try:
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
        if result.returncode != 0:
            print("Failed to upload {} to {}".format(local_path, remote_path))
            print("Error: {}".format(result.stderr))
            raise Exception(result.stderr)
        else:
            print("Successfully uploaded {} to {}".format(local_path, remote_path))

    except Exception as e:
        print("Exception occurred while uploading {} to {}: {}".format(local_path, remote_path, e))


def upload_files_to_server(user, password, local_dir, paths, remote_address):
    for file_name in paths:
        local_path = os.path.join(local_dir, file_name)
        remote_path = os.path.join(remote_address, file_name)
        if not os.path.exists(local_path):
            print("Error: {} not exist please check again\n".format(local_path))
            sys.exit(1)
        if user and password:
            print("source mode")
            secure_upload.secure_curl_upload(local_path, remote_path, user, password)
        else:
            print("prebuilt mode")
            upload_file(local_path, remote_path)
