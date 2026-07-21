import argparse
import fnmatch
import os
import subprocess

ignore_list = [
    "android/*"
]

def parse_cmd_args():
    parser = argparse.ArgumentParser(description="Check if commit's changed lines exceed the specified limit.")
    parser.add_argument('-c', '--commit', type=str, help='Commit hash or path to a diff file', required=True)
    parser.add_argument('-l', '--limit', type=int, help='Line count limit', default=500)
    parser.add_argument('-i', '--ignore', nargs='*', help='List of files or patterns to ignore', default=ignore_list)
    parser.add_argument('-p', '--path', type=str, help='Path to the git repository', default='.')
    return parser.parse_args()

def get_changed_lines_from_commit_info(diff_content, ignore_list):
    changed_lines = 0
    for line in diff_content.split('\n'):
        if line:
            parts = line.split('\t')
            if len(parts) == 3:
                added, deleted, file_path = parts
                if is_file_ignored(file_path, ignore_list):
                    print(f"Ignoring file: {file_path}")
                    continue
                print(f"File: {file_path}, Added lines: {added}, Deleted lines: {deleted}")
                changed_lines += int(added) + int(deleted)
    print(f"Total changed lines: {changed_lines}")
    return changed_lines

def get_changed_lines_from_format_patch_info(diff_content, ignore_list):
    changed_lines = 0
    lines = diff_content.split('\n')
    file_path = None
    ignore_current_file = False

    for line in lines:
        if line.startswith('diff --git'):
            file_path = line.split(' ')[2][2:]
            ignore_current_file = is_file_ignored(file_path, ignore_list)
            if not ignore_current_file:
                print(f"Analyzing file: {file_path}")
        elif line.startswith('--- ') or line.startswith('+++ ') or line == '':
            continue
        elif not ignore_current_file:
            if line.startswith('+') and not line.startswith('+++'):
                #print(f"Analyzing line: {line}")
                changed_lines += 1
            elif line.startswith('-') and (not line.startswith('---') and not line.startswith('--')):
                #print(f"Analyzing line: {line}")
                changed_lines += 1

    print(f"Total changed lines: {changed_lines}")
    return changed_lines

def get_changed_lines_count_from_commit(repo_path, commit_hash, ignore_list):
    """
    Get the total count of modified lines in a specific commit, ignoring specified files.
    """
    # Check if commit has a parent
    try:
        parent_hash = subprocess.check_output(
            ["git", "rev-list", "--parents", "-n", "1", commit_hash],
            cwd=repo_path,
            text=True
        ).strip().split()
        if len(parent_hash) > 1:
            parent_hash = parent_hash[1]
            git_diff_command = ["git", "diff", f"{parent_hash}", f"{commit_hash}", "--numstat"]
        else:
            git_diff_command = ["git", "diff", f"{commit_hash}", "--numstat"]
    except subprocess.CalledProcessError as e:
        raise Exception(f"Failed to get git parent commit: {e.stderr}")

    print(f"Running command: {' '.join(git_diff_command)} in {repo_path}")
    result = subprocess.run(git_diff_command, cwd=repo_path, text=True, capture_output=True)

    if result.returncode != 0:
        raise Exception(f"Failed to get git diff: {result.stderr}")

    return get_changed_lines_from_commit_info(result.stdout, ignore_list)

def is_file_ignored(file_path, ignore_list):
    """
    Check if the file matches any pattern in the ignore list.
    """
    for pattern in ignore_list:
        if fnmatch.fnmatch(file_path, pattern):
            return True
    return False

def check_commit_line_changes(commit_input, line_limit, ignore_list, repo_path='.'):
    """
    Check if the total number of lines changed in a commit exceed the line limit.
    """
    if os.path.isfile(commit_input):
        with open(commit_input, 'r') as file:
            diff_content = file.read()
        changed_lines = get_changed_lines_from_format_patch_info(diff_content, ignore_list)
    else:
        changed_lines = get_changed_lines_count_from_commit(repo_path, commit_input, ignore_list)

    result = changed_lines <= line_limit
    print(f"Lines changed ({changed_lines}) {'do not exceed' if result else 'exceed'} the limit ({line_limit}).")
    return result

def main():
    args = parse_cmd_args()
    result = check_commit_line_changes(args.commit, args.limit, args.ignore, args.path)
    print(f"Result: {result}")

if __name__ == "__main__":
    main()
    """
    python3 kernel/kernel-6.6/check_commit.py  -c kernel-6.6/0001-BSP.Security.AVF-3-4-MTK-patch-FROMLIST-virt-geniezo.patch  -p kernel-6.6
    python3 kernel/kernel-6.6/check_commit.py  -c ab7a92eac5df26e2ebaf146fd8436e81f649dad8 -p kernel-6.6
    """