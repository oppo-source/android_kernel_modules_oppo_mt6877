import os
import re
import argparse
import sys
import requests
from bs4 import BeautifulSoup
from concurrent.futures import ThreadPoolExecutor, as_completed
import shutil
from urllib.parse import urljoin
import glob
import hashlib
import time
from datetime import datetime
import fnmatch
from requests.adapters import HTTPAdapter
from urllib3.util.retry import Retry
import platform
import subprocess

backup_restore_file_list = [
    "IMAGES/boot.img",
    "IMAGES/system_dlkm.img",
    "IMAGES/vbmeta.img",
    "all_files_hash256.txt",
    "all_files_checksum.txt",
    "all_files_hash_checksum.txt",
    "IMAGES/Diges*.mbn",
    "IMAGES/super_meta*.raw"
]

replace_file_list = [
    # BOOT_DEBUG_IMAGE OKI_IMAGES GKI_IMAGES OGKI_IMAGES
    "boot.img", "IMAGES/boot.img",
    "system_dlkm.img", "IMAGES/system_dlkm.img",
    "vbmeta.img", "IMAGES/vbmeta.img",
    "Diges*.mbn", "IMAGES",
    "super_meta*.raw", "IMAGES",
    "hash_256_files_all.txt", "all_files_hash256.txt",
    "hash_128_files_all.txt", "all_files_checksum.txt",
    "hash_32_files_all.txt", "all_files_hash_checksum.txt",

]

ignore_patterns = []

base_urls = [
]


# pyinstaller --onefile ogki_gki_oki_ofp.py

def parse_cmd_args():
    parser = argparse.ArgumentParser(description="oki gki ogki version generate")
    parser.add_argument('-u', '--url', type=str, help='remote ofp url', default='')
    parser.add_argument('-o', '--out', type=str, help='local out dir', default='')
    parser.add_argument('-b', '--build', type=str, help='boot.img build type gki/ogki/oki', default='')
    parser.add_argument('-a', '--action', type=str, help='action download replace or restore', default='download')
    parser.add_argument('-f', '--force', type=str, help='force download or not', default='false')
    parser.add_argument('-t', '--type', type=str, help='ack type', default='')
    parser.add_argument('--checksum', type=str, help='checksum tools', default='')

    args = parser.parse_args()
    check_current_url(args)
    for key, value in vars(args).items():
        print(f"{key}: {value}")
    return args


def download_file(session, url, local_dir):
    local_filename = os.path.join(local_dir, url.split('/')[-1])
    with session.get(url, stream=True) as response:
        response.raise_for_status()
        with open(local_filename, 'wb') as f:
            for chunk in response.iter_content(chunk_size=8192):
                f.write(chunk)
    return local_filename, url


def list_directory(session, url):
    response = session.get(url)
    response.raise_for_status()
    soup = BeautifulSoup(response.text, 'html.parser')
    items = []
    for link in soup.find_all('a'):
        href = link.get('href')
        if href and href not in ('../', '/'):
            if url.endswith('/'):
                items.append(url + href)
            else:
                items.append(url + '/' + href)
    return items


def should_ignore(item, current_ignore_patterns, base):
    """
    Determine if the given item should be ignored
    :param item: The file or directory to match
    :param current_ignore_patterns: List of ignore patterns
    :param base: Base path, all matches are relative to this path
    :return: Boolean indicating whether the item should be ignored
    """
    # Get the relative path of the item with respect to the base
    relative_item = os.path.relpath(item, base)
    # Normalize the path and replace backslashes with forward slashes
    normalized_item = os.path.normpath(relative_item).replace(os.sep, '/')
    for pattern in current_ignore_patterns:
        # Normalize the ignore pattern and replace backslashes with forward slashes
        normalized_pattern = os.path.normpath(pattern).replace(os.sep, '/')
        if fnmatch.fnmatch(normalized_item, normalized_pattern):
            return True
    return False


def download_all(session, base_url, url, local_dir, current_ignore_patterns, max_workers=4):
    """
    Recursively download all files from a remote directory
    :param session: HTTP session object
    :param url: url
    :param base_url: Remote directory URL
    :param local_dir: Local storage directory
    :param current_ignore_patterns: List of file or directory patterns to ignore
    :param max_workers: Maximum number of worker threads in the thread pool
    """
    if not os.path.exists(local_dir):
        os.makedirs(local_dir)

    items = list_directory(session, url)
    futures = []

    with ThreadPoolExecutor(max_workers=max_workers) as executor:
        for item in items:
            # If the item matches an ignore pattern, skip it
            if should_ignore(item, current_ignore_patterns, base_url):
                print("Ignored {}".format(item))
                continue

            if item.endswith('/'):
                subdir_name = item.split('/')[-2]
                local_subdir = os.path.join(local_dir, subdir_name)
                download_all(session, base_url, item, local_subdir, current_ignore_patterns, max_workers)
            else:
                future = executor.submit(download_file_with_retry, session, item, local_dir)
                futures.append(future)

        for future in as_completed(futures):
            try:
                local_filename, orig_url = future.result()
                print("Downloaded {}".format(local_filename))
            except Exception as exc:
                print("Error downloading file: {}".format(exc))


def download_file_with_retry(session, url, local_dir, retries=5):
    for attempt in range(retries):
        try:
            return download_file(session, url, local_dir)
        except requests.RequestException as e:
            print("Attempt {} failed: {}".format(attempt + 1, e))
            if attempt + 1 == retries:
                raise
        except Exception as e:
            print("Attempt {} failed: {}".format(attempt + 1, e))
            if attempt + 1 == retries:
                raise


def create_session_with_retries(retries=5, backoff_factor=0.3):
    session = requests.Session()
    retry = Retry(
        total=retries,
        read=retries,
        connect=retries,
        backoff_factor=backoff_factor,
        status_forcelist=[500, 502, 503, 504]
    )
    adapter = HTTPAdapter(max_retries=retry)
    session.mount('http://', adapter)
    session.mount('https://', adapter)
    return session


def download_entery(args):
    session = create_session_with_retries()
    download_all(session, args.url, args.url, args.out, ignore_patterns)


def delete_directory_contents(directory_path):
    for filename in os.listdir(directory_path):
        file_path = os.path.join(directory_path, filename)
        try:
            if os.path.isfile(file_path) or os.path.islink(file_path):
                os.unlink(file_path)
            elif os.path.isdir(file_path):
                shutil.rmtree(file_path)
        except Exception as e:
            print('Failed to delete {}. Reason: {}'.format(file_path, e))


def create_file_if_not_exists(file_path):
    # Check if the file exists
    if not os.path.exists(file_path):
        # File does not exist, create the file and write "success"
        with open(file_path, 'w') as file:
            file.write('success')
        print("File {} created with 'success'".format(file_path))
    else:
        # File already exists
        print("File {} already exists".format(file_path))


def check_and_download(args):
    download_status = os.path.join(args.out, 'download_finish.txt')
    need_download = False
    if args.force.lower() != "true" and os.path.exists(download_status):
        print('{} exist and force is {} ignore download'.format(download_status, args.force))
    elif args.force.lower() == "true" and os.path.exists(download_status):
        print('{} exist and force is {} need remove and download'.format(download_status, args.force))
        delete_directory_contents(args.out)
        need_download = True
    else:
        print('{} not exist and force is {} need download'.format(download_status, args.force))
        need_download = True

    if need_download:
        download_entery(args)
        create_file_if_not_exists(download_status)


def check_vendor_files(path):
    return 'bak'


def ensure_suffix_once(file_path, suffix):
    """
    Ensure the suffix is added only once to the file path.
    :param file_path: Original file path
    :param suffix: Suffix to add
    :return: Processed file path
    """
    dir_name, file_name = os.path.split(file_path)
    base_name, ext = os.path.splitext(file_name)

    # If the suffix already exists, remove it
    if base_name.endswith("_{}".format(suffix)):
        base_name = base_name[:-(len(suffix) + 1)]

    # Add the suffix and return the new path
    return os.path.join(dir_name, "{}_{}{}".format(base_name, suffix, ext))


def backup_files(base_dir, file_list, suffix):
    """
    Backup files based on a file list and a backup suffix. If the backup file already exists, print a log and exit.
    :param base_dir: Base directory of the files
    :param file_list: List of files to backup (relative to the base directory)
    :param suffix: Suffix for the backup file names
    """
    for relative_path in file_list:
        # Handle wildcard patterns in the path
        file_paths = glob.glob(os.path.join(base_dir, relative_path))

        if not file_paths:
            # print(f"No files found for pattern: {relative_path}")
            continue

        for file_path in file_paths:
            # Construct the initial backup file path and ensure only one suffix
            backup_file_path = ensure_suffix_once(file_path, suffix)

            # Check if the backup file already exists
            if os.path.exists(backup_file_path):
                if file_path != backup_file_path:
                    print("Backup file already exists: {}".format(backup_file_path))
            else:
                if os.path.exists(file_path):
                    print("Backing up {} to {}".format(file_path, backup_file_path))
                    shutil.copy2(file_path, backup_file_path)
                else:
                    print("File not found: {}".format(file_path))


def restore_files(base_dir, file_list, suffix):
    """
    Restore files based on a file list and a backup suffix.
    :param base_dir: Base directory of the files
    :param file_list: List of files to restore (relative to the base directory)
    :param suffix: Suffix for the backup file names
    """
    for relative_path in file_list:
        # Handle wildcard patterns in the path
        file_paths = glob.glob(os.path.join(base_dir, relative_path))

        if not file_paths:
            # print(f"No files found for pattern: {relative_path}")
            continue

        for file_path in file_paths:
            # Construct the backup file path and ensure only one suffix
            backup_file_path = ensure_suffix_once(file_path, suffix)

            # Check if the backup file exists
            if not os.path.exists(backup_file_path):
                print("Backup file does not exist: {}".format(backup_file_path))
            else:
                if file_path != backup_file_path:
                    print("Restoring {} to {}".format(backup_file_path, file_path))
                    shutil.copy2(backup_file_path, file_path)

    print("Restore operation completed.")


def get_checksum(args, file_path, block_size):
    if os.path.exists(args.checksum):
        command = [args.checksum, file_path, str(block_size)]
        result = subprocess.run(command, capture_output=True, text=True)
        return result.stdout.strip()
    else:
        return "00000000"


def calculate_checksum(args, file_path, algorithm='sha256'):
    """
    Calculate the hash value of a specified file, supporting algorithms like SHA-256 and MD5.

    Parameters:
    - file_path: Path to the file
    - algorithm: Hash algorithm ('sha256', 'md5', etc.)

    Returns:
    - The hash value of the file
    """
    # Create the corresponding hash object based on the algorithm name
    if algorithm == 'sha256':
        hash_func = hashlib.sha256()
    elif algorithm == 'crc32':
        checksum = get_checksum(args, file_path, 6)
        return checksum
    else:
        hash_func = hashlib.md5()

    with open(file_path, "rb") as f:
        for byte_block in iter(lambda: f.read(4096), b""):
            hash_func.update(byte_block)
    return hash_func.hexdigest()


def get_normalized_path(file_path):
    """
    Get the last two levels of the directory. If the first level directory is not IMAGES, replace it with IMAGES.
    Only keep the file name and normalize the path format to POSIX style.
    """
    # Normalize the path to the system format and replace all separators with backslashes
    file_path = os.path.normpath(file_path)  # Normalize the path
    os_name = platform.system()
    if os_name == "Windows":
        file_path = file_path.replace('/', '\\')  # Replace all path separators with backslashes
    path_parts = file_path.split(os.sep)  # Split the path using the system separator

    if len(path_parts) < 2:
        raise ValueError("File path must contain at least two levels of directories.")

    last_part = path_parts[-1]  # Get the file name
    second_last_part = path_parts[-2]  # Get the second-to-last part

    if second_last_part != "IMAGES":
        second_last_part = "IMAGES"  # Replace with IMAGES

    normalized_path = os.path.join(second_last_part, last_part)  # Join to form the normalized path
    if os_name == "Windows":
        normalized_path = normalized_path.replace('\\', '/')  # Replace with POSIX style separators

    return normalized_path


def write_hash_to_file(args, file_path, output_file, algorithm):
    """
    Calculate the SHA-256 hash value of a file and write it to the all_files_hash256.txt file in the same directory.
    """
    hash_value = calculate_checksum(args, file_path, algorithm)
    relative_path = get_normalized_path(file_path)
    output_content = "{} {}\n".format(hash_value, relative_path) if algorithm == 'sha256' or algorithm == 'crc32' \
        else "{}  {}\n".format(hash_value, relative_path)

    # Open the file in 'a+' mode, set the default text encoding to 'UTF-8', and maintain the UNIX newline format "\n"
    with open(output_file, "a+", encoding="utf-8", newline='\n') as f:
        f.write(output_content)


def update_hash_values(original_file, updated_file, hash_algorithm):
    # Read the original file
    with open(original_file, 'r') as orig_file:
        original_lines = orig_file.readlines()

    # Read the file to be updated
    with open(updated_file, 'r') as upd_file:
        updated_lines = upd_file.readlines()

    # Store the updated lines
    fresh_lines = []

    # Iterate over each line in the original file
    for orig_line in original_lines:
        orig_parts = orig_line.strip().split()

        # Ensure the line has only two columns
        if len(orig_parts) != 2:
            continue

        orig_hash, orig_path = orig_parts

        # Iterate over each line in the file to be updated
        for upd_line in updated_lines:
            upd_parts = upd_line.strip().split()

            # Ensure the line has only two columns
            if len(upd_parts) == 2:
                upd_hash, upd_path = upd_parts

                # If the paths match, update the hash value
                if orig_path == upd_path:
                    fresh_line = "{} {}\n".format(orig_hash, orig_path) \
                        if hash_algorithm == 'sha256'  or hash_algorithm == 'crc32' \
                        else "{}  {}\n".format( orig_hash, orig_path)
                else:
                    fresh_line = upd_line
            else:
                fresh_line = upd_line

            fresh_lines.append(fresh_line)

        # Update the list of lines to be updated to avoid duplicate replacements
        updated_lines = fresh_lines
        fresh_lines = []

    # Write the updated lines back to the file in the original encoding format
    with open(updated_file, 'w', newline='') as upd_file:
        upd_file.writelines(updated_lines)


def get_image_type(file_path, default_value='gki'):
    """
    Read the content from a file, search for the image_type tag, and return its value. If not found, return the
    default value.
    :param file_path: Path to the file
    :param default_value: Default value to return if image_type is not found
    :return: The value of image_type or the default value
    """
    try:
        with open(file_path, 'r', encoding='utf-8') as file:
            content = file.read()

            # Use a regular expression to find the value of image_type
            match = re.search(r'image_type:(\S+)', content)
            if match:
                return match.group(1)
            else:
                return default_value
    except FileNotFoundError:
        print("File {} does not exist.".format(file_path))
        return default_value


def replace_files(base_dir, file_list, ack_type):
    """
    Copy files according to the replacement list and base directory, based on the pairing in the replacement list.
    :param base_dir: Base directory
    :param ack_type: ack type
    :param file_list: File replacement list, each pair consists of [source file, destination file]
    """
    if len(file_list) % 2 != 0:
        print("The file_list should contain pairs of source and destination files.")
        return

    for i in range(0, len(file_list), 2):
        src_relative = file_list[i]
        dest_relative = file_list[i + 1]

        # Use glob to match source files with wildcards
        src_paths = glob.glob(os.path.join(base_dir, ack_type.upper() + "_IMAGES", src_relative))

        if not src_paths:
            print("No source files found for: {}".format(src_relative))
            continue

        for src_path in src_paths:
            dest_path = os.path.join(base_dir, dest_relative)

            if os.path.isdir(dest_path):
                # If the destination is a directory, copy the file into that directory
                dest_path = os.path.join(dest_path, os.path.basename(src_path))
            else:
                # If the destination is not a directory, copy as planned
                dest_dir = os.path.dirname(dest_path)
                if not os.path.exists(dest_dir):
                    os.makedirs(dest_dir)

            # Check if the source file exists
            if not os.path.exists(src_path):
                print("Source file does not exist: {}".format(src_path))
                continue

            print("Copying {} to {}".format(src_path, dest_path))
            shutil.copy2(src_path, dest_path)

    print("File replacement completed.")


def is_posix_system():
    """Determine if the current system is a POSIX system (e.g., Linux and macOS)"""
    return os.name == 'posix'


def set_file_permissions(path, mode):
    """Set the permissions of a file or directory, only on POSIX systems"""
    if is_posix_system():
        try:
            os.chmod(path, mode)
            # print("Set permissions for {}: {}".format(path, oct(mode)))
        except OSError as e:
            print("Failed to set permissions for {}: {}".format(path, e))
    else:
        print("Setting permissions is not supported on this system")


def move_files_and_delete_dir(src_relative, src_relative_tmp):
    # Check if the src_relative_tmp directory exists
    if os.path.exists(src_relative_tmp) and os.path.isdir(src_relative_tmp):
        # Check if the src_relative_tmp directory has files
        set_file_permissions(src_relative_tmp, 0o777)
        if os.listdir(src_relative_tmp):
            # Ensure the src_relative directory exists
            if not os.path.exists(src_relative):
                os.makedirs(src_relative)

            # Move all files from src_relative_tmp to src_relative
            for item in os.listdir(src_relative_tmp):
                src_item = os.path.join(src_relative_tmp, item)
                dst_item = os.path.join(src_relative, item)
                set_file_permissions(src_item, 0o777)
                shutil.move(src_item, dst_item)
                # print("Moved {} to {}".format(src_item, dst_item))

        # Delete the empty directory
        if not os.listdir(src_relative_tmp):
            shutil.rmtree(src_relative_tmp)
            # print("Deleted empty directory: {}".format(src_relative_tmp))


def generate_current_hash(args, base_dir, ack_type):
    """
    Generate hash values for files in the specified directory and update the hash files.

    :param base_dir: Base directory
    :param ack_type: Type of files (e.g., 'oki', 'ogki', 'gki')
    """

    src_relative = "{}_IMAGES".format(ack_type.upper())
    src_relative_tmp = "{}_IMAGES.tmp".format(ack_type.upper())

    src_dir = os.path.join(base_dir, src_relative)
    src_dir_tmp = os.path.join(base_dir, src_relative_tmp)
    # Check if the target path exists
    if not os.path.exists(src_dir):
        # print("File {} does not exist, ignoring".format(src_dir))
        # Check if the temporary directory is empty and delete it if it is
        if os.path.exists(src_dir_tmp) and not os.listdir(src_dir_tmp):
            os.rmdir(src_dir_tmp)
        return

    move_files_and_delete_dir(src_dir, src_dir_tmp)

    hash256_file_txt = "{}/hash_256_files_all.txt".format(src_relative)
    checksum_file_txt = "{}/hash_128_files_all.txt".format(src_relative)
    crc32_file_txt = "{}/hash_32_files_all.txt".format(src_relative)

    hash256_file_tmp_txt = "{}/tmp_hash_256_files_all.txt".format(src_relative)
    checksum_file_tmp_txt = "{}/tmp_hash_128_files_all.txt".format(src_relative)
    crc32_file_tmp_txt = "{}/tmp_hash_32_files_all.txt".format(src_relative)

    hash256_file_final = os.path.join(base_dir, hash256_file_txt)
    checksum_file_final = os.path.join(base_dir, checksum_file_txt)
    crc32_file_final = os.path.join(base_dir, crc32_file_txt)

    hash256_file_tmp = os.path.join(base_dir, hash256_file_tmp_txt)
    checksum_file_tmp = os.path.join(base_dir, checksum_file_tmp_txt)
    crc32_file_tmp = os.path.join(base_dir, crc32_file_tmp_txt)

    current_hash256 = os.path.join(base_dir, hash256_file_tmp)
    hash256_base = os.path.join(base_dir, "all_files_hash256.txt")

    current_checksum = os.path.join(base_dir, checksum_file_tmp)
    checksum_base = os.path.join(base_dir, "all_files_checksum.txt")

    current_crc32 = os.path.join(base_dir, crc32_file_tmp)
    crc32_base = os.path.join(base_dir, "all_files_hash_checksum.txt")

    if os.path.exists(current_hash256):
        os.remove(current_hash256)

    if os.path.exists(current_checksum):
        os.remove(current_checksum)

    if os.path.exists(current_crc32):
        os.remove(current_crc32)

    if os.path.exists(checksum_base):
        shutil.copy2(checksum_base, checksum_file_final)
        set_file_permissions(checksum_file_final, 0o777)

    if os.path.exists(hash256_base):
        shutil.copy2(hash256_base, hash256_file_final)
        set_file_permissions(hash256_file_final, 0o777)

    if os.path.exists(crc32_base):
        shutil.copy2(crc32_base, crc32_file_final)
        set_file_permissions(crc32_file_final, 0o777)

    for root, dirs, files in os.walk(src_dir):
        for file in files:
            file_path = os.path.join(root, file)
            write_hash_to_file(args, file_path, hash256_file_tmp, "sha256")
            write_hash_to_file(args, file_path, checksum_file_tmp, "md5")
            write_hash_to_file(args, file_path, crc32_file_tmp, "crc32")

    if os.path.exists(hash256_file_final):
        update_hash_values(hash256_file_tmp, hash256_file_final, "sha256")

    if os.path.exists(checksum_file_final):
        update_hash_values(checksum_file_tmp, checksum_file_final, "md5")

    if os.path.exists(crc32_file_final):
        update_hash_values(crc32_file_tmp, crc32_file_final, "crc32")

    os.remove(checksum_file_tmp)
    os.remove(hash256_file_tmp)
    os.remove(crc32_file_tmp)
    # print("generate file hash completed.")


def generate_ack_hash(args, base_dir):
    """
    Generate hash values for files of different types in the specified directory.

    :param base_dir: Base directory
    """
    for ack_type in ["oki", "ogki", "gki"]:
        generate_current_hash(args, base_dir, ack_type)


def dump_url(urls):
    for url in urls:
        result = check_remote_url(url)
        if result:
            cleaned_url, parent_dir = result
            print("Result for {}: URL: {}, Parent Directory: {}".format(url, cleaned_url, parent_dir))
        else:
            print("Result for {}: {}".format(url, result))


def check_remote_url(base_url, file_path="IMAGES/boot.img"):
    """
    Check if a remote file exists and return a corresponding string based on different conditions.

    :param base_url: Base URL of the remote server
    :param file_path: Path of the file to check, default is "IMAGES/boot.img"
    :return: If the file exists and meets the conditions, return the path without
    "IMAGES/boot.img" and the name of the parent directory of IMAGES. If the conditions are not met, return None
    """

    def file_exists(url):
        responses = requests.head(url)
        return responses.status_code == 200

    def get_ini_value(url, field):
        response_ini = requests.get(url)
        if response_ini.status_code == 200:
            for line in response_ini.text.splitlines():
                if line.startswith(field):
                    return line.split('=')[1].strip()
        return None

    base_url = base_url.rstrip('/') + '/'
    # First, check if the ofp_folder field value exists in the compile.ini file
    compile_ini_url = urljoin(base_url, "compile.ini").replace('\\', '/')
    ofp_folder = get_ini_value(compile_ini_url, "ofp_folder")

    if ofp_folder:
        print("current base_url:" + base_url)
        print("current ofp_folder:" + ofp_folder)
        # Combine base_url and ofp_folder to form the complete path
        complete_url = urljoin(base_url, ofp_folder + '/').replace('\\', '/')

        # Check if the IMAGES/boot.img file exists
        file_url = urljoin(complete_url, file_path).replace('\\', '/')
        print("current file_url:" + file_url)
        if file_exists(file_url):
            # Return the complete path without IMAGES/boot.img and the name of the parent directory of IMAGES
            return file_url.rsplit('/', 2)[0] + '/', file_url.rstrip('/').split('/')[-3]

    # Continue with the original functionality check
    original_file_url = urljoin(base_url, file_path).replace('\\', '/')
    if file_exists(original_file_url):
        # Return the complete path without IMAGES/boot.img and the name of the parent directory of IMAGES
        return original_file_url.rsplit('/', 2)[0] + '/', original_file_url.rstrip('/').split('/')[-3]
    else:
        # Get all subdirectories under release_out
        release_out_url = urljoin(base_url, "release_out/").replace('\\', '/')
        response = requests.get(release_out_url)
        if response.status_code == 200:
            soup = BeautifulSoup(response.text, 'html.parser')
            directories = [node.get('href') for node in soup.find_all('a') if
                           node.get('href') and node.get('href').endswith('/')]

            for directory in directories:
                directory_url = urljoin(release_out_url, directory).replace('\\', '/')
                flash_bin_url = urljoin(directory_url, "flash_bin/{}".format(file_path)).replace('\\', '/')
                if file_exists(flash_bin_url):
                    print("Found IMAGES/boot.img in release_out/{}flash_bin {}".format(directory, flash_bin_url))
                    return flash_bin_url.rsplit('/', 2)[0] + '/', flash_bin_url.rstrip('/').split('/')[-3]
    return None


def check_current_url(args):
    # dump_url(base_urls)
    if args.action == "download":
        result = check_remote_url(args.url)
        if result:
            args.url, image_dir = result
            if not args.out:
                args.out = image_dir
            print("current URL: {}, out Directory: {}".format(args.url, args.out))
        else:
            print("invalid url: {}".format(args.url))
            if args.url and args.action == "download":
                print("You input URL is invalid and in download mode, please check again")
                sys.exit(1)
    else:
        print("action: {}  ignore URL check".format(args.action))


def main():
    print('\nstep 1 get input argc/argv')
    need_replace = False
    args = parse_cmd_args()

    if not args.build:
        args.build = check_vendor_files(args.out)

    if args.action == "download":
        # download
        check_and_download(args)
        need_replace = True
    if need_replace or args.action == "replace":
        # backup
        backup_files(args.out, backup_restore_file_list, args.build)
        # replace
        if not args.type:
            ack_info = os.path.join(args.out, "ACK_INFO/kernel_version.txt")
            ack_type = get_image_type(ack_info)
            print("args.type is null auto read from {} is {}".format(ack_info, ack_type))
            if ack_type == "ogki" or ack_type == "gki":
                ack_type = "oki"
            elif ack_type == "oki":
                ack_type = "gki"
            else:
                ack_type = "gki"
            args.type = ack_type

        print("args.type is {}".format(args.type))
        replace_files(args.out, replace_file_list, args.type)
    elif args.action == "restore":
        # restore
        restore_files(args.out, backup_restore_file_list, args.build)
    elif args.action == "hash":
        # hash
        generate_ack_hash(args, args.out)
    else:
        print("ignore current action:" + args.action)


if __name__ == "__main__":
    start_time = time.time()
    start_datetime = datetime.now()
    # Print start time, end time, and execution time
    print("start time: {}".format(start_datetime.strftime('%Y-%m-%d %H:%M:%S')))
    main()
    # Record end time
    end_time = time.time()
    end_datetime = datetime.now()
    # Calculate and print execution time
    execution_time = end_time - start_time
    # Convert execution time to hours, minutes, and seconds
    hours, remainder = divmod(execution_time, 3600)
    minutes, seconds = divmod(remainder, 60)
    print("end time: {}".format(end_datetime.strftime('%Y-%m-%d %H:%M:%S')))
    print("cost time: {} hour {} minute {:.2f} second".format(int(hours), int(minutes), seconds))
