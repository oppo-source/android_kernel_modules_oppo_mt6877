import argparse
import os
import shutil
import stat
import fcntl
from contextlib import contextmanager

def rename_last_directory_in_path(original_path, new_name=None):
    # Remove trailing slashes
    original_path = original_path.rstrip(os.sep)

    # Split the path into directory and last name
    directory, last_name = os.path.split(original_path)

    if not new_name:
        new_name = "{}_backup".format(last_name)

    # Construct the new path
    new_path = os.path.join(directory, new_name)
    return new_path

def parse_cmd_args():
    parser = argparse.ArgumentParser(description="Filter out modules in dlkm or vendor_boot image")
    parser.add_argument('--config', action='append', help='Filter out config file')
    parser.add_argument('--path', type=str, help='Filter out directory', default="")
    parser.add_argument('--action', type=str, help='Filter action', default="remove")
    parser.add_argument('--backup', type=str, help='Backup path', default="")
    args = parser.parse_args()

    if not args.backup:
        args.backup = rename_last_directory_in_path(args.path)
    args.path = args.path.rstrip(os.sep)

    for key, value in vars(args).items():
        print("{}: {}".format(key, value))
    print("Start filter out modules")
    return args

def find_and_process_modules_files(source_dir, target_dir, file_name, action):
    for root, dirs, files in os.walk(source_dir):
        for file in files:
            if file == file_name:
                source_file_path = os.path.join(root, file)
                relative_path = os.path.relpath(source_file_path, source_dir)
                target_file_path = os.path.join(target_dir, relative_path)

                # Ensure target directory exists
                target_file_dir = os.path.dirname(target_file_path)
                if not os.path.exists(target_file_dir) and action == 'backup':
                    os.makedirs(target_file_dir, exist_ok=True)

                if action == 'print':
                    print("Found: {}".format(source_file_path))
                elif action == 'remove':
                    os.remove(source_file_path)
                    print("Removed: {}".format(source_file_path))
                elif action == 'backup':
                    shutil.move(source_file_path, target_file_path)
                    print("Moved: {} -> {}".format(source_file_path, target_file_path))
                else:
                    print("Unknown action: {}".format(action))

def remove_lines_containing_string(file_path, target_string):
    try:
        with open(file_path, 'r', encoding='utf-8') as file:
            lines = file.readlines()

        # Filter out lines containing the target string after stripping whitespace from the start and end
        new_lines = [line for line in lines if line.strip() != target_string]

        # Write the filtered content back to the file
        with open(file_path, 'w', encoding='utf-8') as file:
            file.writelines(new_lines)

        print("Processed file: {}".format(file_path))
    except FileNotFoundError:
        print("File {} not found.".format(file_path))
    except Exception as e:
        print("Error processing file {}: {}".format(file_path, e))

def find_and_process_modules_load(source_dir, target_string, filter_file_name):
    for root, dirs, files in os.walk(source_dir):
        for file in files:
            if file == filter_file_name:
                file_path = os.path.join(root, file)
                os.chmod(file_path, stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR |
                         stat.S_IRGRP | stat.S_IWGRP | stat.S_IXGRP |
                         stat.S_IROTH | stat.S_IWOTH | stat.S_IXOTH)
                print("Found {} file: {}".format(filter_file_name, file_path))
                remove_lines_containing_string(file_path, target_string)

def filter_out_modules_list(args):
    for config in args.config:
        print(config)
        if os.path.exists(config):
            try:
                with open(config, 'r', encoding='utf-8') as file:
                    for module_name in file:
                        module_name = module_name.strip()  # Remove trailing whitespace and newline characters
                        # Ensure the line is not empty and does not start with '#'
                        if module_name and not module_name.startswith('#'):
                            print("Processing module name: {}".format(module_name))
                            find_and_process_modules_files(args.path, args.backup, module_name, args.action)
                            find_and_process_modules_load(args.path, module_name, "modules.load")
                            find_and_process_modules_load(args.path, module_name, "modules.load.recovery")
                        else:
                            print("ignore module name: {}".format(module_name))
            except FileNotFoundError:
                print("File {} not found.".format(config))
            except Exception as e:
                print("Error reading file {}: {}".format(config, e))

@contextmanager
def wait_for_filter_lock(path):
    filter_out_modules_lock_path  = os.path.join(path, "filter_out_modules.lock")
    fd = os.open(filter_out_modules_lock_path, os.O_CREAT | os.O_RDWR)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX)
        yield
    finally:
        fcntl.flock(fd, fcntl.LOCK_UN)
        os.close(fd)
        try:
            os.unlink(filter_out_modules_lock_path)
        except:
            pass

def main():
    args = parse_cmd_args()
    with wait_for_filter_lock(args.path):
        print("start filter {}".format(args.path))
        if args.config and os.path.exists(args.path):
            filter_out_modules_list(args)
        print("finish filter {}".format(args.path))

if __name__ == "__main__":
    main()
