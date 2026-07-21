import argparse
import os
import shutil
import hashlib
import fcntl
from contextlib import contextmanager
import time
import glob
import stat

def parse_cmd_args():
    parser = argparse.ArgumentParser(description="ACK OKI GKI OGKI image OFP build tool")
    parser.add_argument('--type', type=str, help='Type of ogki, gki, oki, no (default: gki)', default="gki")
    parser.add_argument('--path', type=str, help='Path to the directory', default="euclid_images/vendor/")
    args = parser.parse_args()

    print("generate OGKI GKI OKI default build image")
    for key, value in vars(args).items():
        print("{}: {}".format(key, value))
    return args


def get_ack_type(ack_type):
    if ack_type == "":
        return 'default'
    elif ack_type == "gki":
        return 'gki'
    elif ack_type == "ogki":
        return 'ogki'
    elif ack_type in ["oki", "no"]:
        return 'oki'
    else:
        return 'gki'  # 默认返回 'gki' 如果 ack_type 不匹配任何已知值


def calculate_md5(file_path):
    if not os.path.exists(file_path) or not os.path.isfile(file_path):
        print("File {} does not exist.".format(file_path))
        return None  # 或者返回其他默认值，例如 "default_md5_value"

    hasher = hashlib.md5()
    with open(file_path, 'rb') as f:
        buf = f.read(65536)  # 读取64KB的数据块
        while buf:
            hasher.update(buf)
            buf = f.read(65536)
    return hasher.hexdigest()


def backup_image(file_path):
    base_name, ext = os.path.splitext(file_path)
    new_name = "{}_bak{}".format(base_name, ext)
    os.rename(file_path, new_name)
    print("Renamed {} to {}".format(file_path, new_name))
    return new_name

def check_ack_image_duplicate(path):

    boot_target_file = os.path.join(path, "boot.img")
    boot_target_md5 = calculate_md5(boot_target_file)

    for ack_type in ['oki', 'gki', 'ogki']:
        boot_img_path = os.path.join(path, "boot_{}.img".format(ack_type))
        system_dlkm_img_path = os.path.join(path, "system_dlkm_{}.img".format(ack_type))
        if os.path.exists(boot_img_path) and os.path.exists(system_dlkm_img_path):
            boot_img_ack_md5 = calculate_md5(boot_img_path)
            if boot_target_md5 == boot_img_ack_md5:
                print("{}     md5 {}\n{} md5 {}".format(boot_target_file, boot_target_md5,boot_img_path, boot_img_ack_md5))
                print("{} {} are same,we need rename and regenerate".format(boot_target_file,  boot_img_path))
                backup_image(boot_img_path)
                backup_image(system_dlkm_img_path)

def handle_file_action(source_file, target_file, action):
    """Copy or rename the file based on the action if the MD5 hashes are different."""
    if not (source_file and target_file):
        print("Source or target file path is not provided.")
        return

    if not (os.path.exists(source_file) and os.path.isfile(source_file)):
        print("Source file {} does not exist or is not a file.".format(source_file))
        return

    if os.path.exists(target_file) and os.path.isfile(target_file):
        source_md5 = calculate_md5(source_file)
        target_md5 = calculate_md5(target_file)
        if source_md5 == target_md5:
            print("MD5 hashes match. No need to {} {} to {}.".format(action, source_file, target_file))
            return

    try:
        if action == 'copy':
            shutil.copy2(source_file, target_file)
            print("Successfully copied {} to {}.".format(source_file, target_file))
        elif action == 'rename':
            shutil.move(source_file, target_file)
            print("Successfully renamed {} to {}.".format(source_file, target_file))
        else:
            print("Invalid action specified: {}. Use 'copy' or 'rename'.".format(action))
    except Exception as e:
        print("Failed to {} {} to {}: {}".format(action, source_file, target_file, e))


def list_files_with_pattern(path, patterns):
    """List files matching the given patterns in the specified path and print their MD5 hashes with alignment."""
    max_path_length = 0

    # Determine the maximum path length for alignment
    for pattern in patterns:
        for file_path in glob.glob(os.path.join(path, pattern)):
            if len(file_path) > max_path_length:
                max_path_length = len(file_path)

    # Print files with aligned paths and MD5 hashes
    for pattern in patterns:
        for file_path in glob.glob(os.path.join(path, pattern)):
            md5_hash = calculate_md5(file_path)
            print("{:{}} {}".format(file_path, max_path_length, md5_hash))


def generate_ack_image(path, ack_type):
    if ack_type not in ["ogki", "gki", "oki"]:
        print("ack_type: {}. not in one of ['ogki', 'gki', 'oki'] use default".format(ack_type))
        return

    boot_target_file = os.path.join(path, "boot.img")
    system_dlkm_target_file = os.path.join(path, "system_dlkm.img")

    for at in ["oki", "gki", "ogki"]:
        boot_source_file = os.path.join(path, "boot_{}.img".format(at))
        boot_backup_file = os.path.join(path, "boot_{}_bak.img".format(at))
        system_dlkm_source_file = os.path.join(path, "system_dlkm_{}.img".format(at))
        system_dlkm_backup_file = os.path.join(path, "system_dlkm_{}_bak.img".format(at))
        if at == ack_type:
            if os.path.exists(boot_source_file):
                handle_file_action(boot_source_file, boot_target_file, "copy")
                handle_file_action(boot_source_file, boot_backup_file, "rename")
            elif os.path.exists(boot_backup_file):
                handle_file_action(boot_backup_file, boot_target_file, "copy")
            else:
                print("boot_{}.img does not exist".format(at))

            if os.path.exists(system_dlkm_source_file):
                handle_file_action(system_dlkm_source_file, system_dlkm_target_file, "copy")
                handle_file_action(system_dlkm_source_file, system_dlkm_backup_file, "rename")
            elif os.path.exists(system_dlkm_backup_file):
                handle_file_action(system_dlkm_backup_file, system_dlkm_target_file, "copy")
            else:
                print("system_dlkm_{}.img does not exist".format(at))
        else:
            if not os.path.exists(boot_source_file) and os.path.exists(boot_backup_file):
                handle_file_action(boot_backup_file, boot_source_file, "rename")
            elif os.path.exists(boot_source_file) and os.path.exists(boot_backup_file):
                os.remove(boot_backup_file)
                print("remove {}".format(boot_backup_file))
            # else:
            #    print("boot_{}.img exists and boot_{}_bak.img not exists ".format(at, at))

            if not os.path.exists(system_dlkm_source_file) and os.path.exists(system_dlkm_backup_file):
                handle_file_action(system_dlkm_backup_file, system_dlkm_source_file, "rename")
            elif os.path.exists(system_dlkm_source_file) and os.path.exists(system_dlkm_backup_file):
                os.remove(system_dlkm_backup_file)
                print("remove {}".format(system_dlkm_backup_file))
            # else:
            #    print("system_dlkm_{}.img  exists and  system_dlkm_{}_bak.img  not exists ".format(at, at))

@contextmanager
def wait_for_lock(path):
    LOCK_FILE = os.path.join(path, "ack_image.lock")
    fd = os.open(LOCK_FILE, os.O_CREAT | os.O_RDWR)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX)
        yield
    finally:
        fcntl.flock(fd, fcntl.LOCK_UN)
        os.close(fd)
        try:
            os.unlink(LOCK_FILE)
        except:
            pass

def main():
    args = parse_cmd_args()
    with wait_for_lock(args.path):
        ack_type = get_ack_type(args.type)
        print("ack_type:{}".format(ack_type))
        check_ack_image_duplicate(args.path)
        generate_ack_image(args.path, ack_type)
        patterns = ["boot*.img", "system_dlkm*.img"]
        list_files_with_pattern(args.path, patterns)
        # time.sleep(10)

if __name__ == "__main__":
    main()
