import re
import os
import csv
import requests
from bs4 import BeautifulSoup


def fetch_directory(url):
    """
    Fetches the directory contents from the given URL

    Args:
        url (str): URL to fetch the directory contents from

    Returns:
        list: List of hrefs in the directory
    """
    try:
        response = requests.get(url)
        response.raise_for_status()
        soup = BeautifulSoup(response.text, 'html.parser')
        return [a['href'] for a in soup.find_all('a', href=True) if a['href'] != '../']
    except Exception as e:
        print(f"Error fetching {url}: {e}")
        return []


def recursive_search(url, date_pattern):
    """
    Recursively searches through directories and appends URLs with matching date patterns to results

    Args:
        url (str): Base URL to start the search
        date_pattern (Pattern): Compiled regex pattern for the date format

    Returns:
        list: List of result URLs that match the date pattern
    """
    results = []
    directories = fetch_directory(url)
    for directory in directories:
        full_path = url + directory
        if date_pattern.search(directory):
            results.append(full_path)
        else:
            results.extend(recursive_search(full_path, date_pattern))
    return results


def save_results_to_csv(file_path, results):
    """
    Saves the results to a CSV file

    Args:
        file_path (str): the output file path
        results (list): list of result URLs
    """
    with open(file_path, mode='w', newline='') as file:
        writer = csv.writer(file)
        # writer.writerow(['URL'])
        for result in results:
            writer.writerow([result])


def generate_and_check_urls(protocols, numbers, suffixes, output_file):
    """
    生成所有可能的 URL 组合，检查 URL 是否存在，如果存在则解析目录列表并输出到 CSV 文件。

    :param protocols: 协议列表，例如 ['http', 'https']
    :param numbers: 数字范围，例如 range(10, 31)
    :param suffixes: 后缀列表，例如 ['TSW', 'PSW']
    :param output_file: 输出文件路径
    """
    # 打开输出文件
    with open(output_file, 'w', newline='') as csvfile:
        csvwriter = csv.writer(csvfile)
        # 写入 CSV 文件的表头
        csvwriter.writerow(['url', 'directory', 'full_path'])

        # 生成所有可能的组合
        for protocol in protocols:
            for number in numbers:
                for suffix in suffixes:
                    url = f"{protocol}://gpw{number}.myoas.com/artifactory/phone-snapshot-local/{suffix}"
                    # print(f"Checking URL: {url}")

                    try:
                        # 发送请求
                        response = requests.get(url, timeout=5)
                        if response.status_code == 200:
                            # print(f"URL exists: {url}")

                            # 解析 HTML 内容
                            soup = BeautifulSoup(response.text, 'html.parser')

                            # 假设目录列表在 <a> 标签的 href 属性中
                            directories = [a['href'] for a in soup.find_all('a', href=True) if
                                           a['href'].endswith('/') and not a['href'].startswith('../')]
                            for directory in directories:
                                full_path = f"{url}/{directory.strip()}"
                                # print(f"Full path: {full_path}")
                                # 写入 CSV 文件
                                csvwriter.writerow([url, directory.strip(), full_path])
                        else:
                            pass
                            # print(f"URL does not exist: {url} (Status code: {response.status_code})")
                    except requests.exceptions.RequestException as e:
                        pass
                        # print(f"Request failed for URL: {url} (Error: {e})")

    print(f"Output written to {output_file}")


def dump_all_artifactory_info(args, output_file):
    # 定义变量
    protocols = ['http', 'https']
    numbers = range(10, 30)  # 10到30的范围
    suffixes = ['TSW', 'PSW']
    # 调用函数
    generate_and_check_urls(protocols, numbers, suffixes, output_file)


def dump_all_project_info(args, input_file, output_file):
    """
    Main function to run the recursive search on base URLs and save results to CSV
    """
    base_urls = []

    date_pattern = re.compile(r'\d{14}')  # Pattern for matching 14-digit date format

    all_results = []  # List to store all results
    """
    for base_url in base_urls:
        print(f"Processing paths for base URL: {base_url}")
        results = recursive_search(base_url, date_pattern)
        all_results.extend(results)
    """

    with open(input_file, mode='r', newline='') as file:
        reader = csv.DictReader(file)
        for row in reader:
            base_url = row['full_path']
            print(f"Processing paths for base URL: {base_url}")
            results = recursive_search(base_url, date_pattern)
            all_results.extend(results)

    save_results_to_csv(output_file, all_results)
    print(f"Search complete, results saved to: {output_file}")


def contains_keywords(input_string, keywords):
    """
    检查输入字符串是否包含任何一个指定的关键词。

    :param input_string: 要检查的字符串
    :param keywords: 包含关键词的列表
    :return: 如果包含任何一个关键词，返回 True；否则返回 False
    """
    for keyword in keywords:
        if keyword in input_string:
            return True
    return False


def parse_filename(filepath):
    if filepath.endswith('/'):
        filepath = filepath[:-1]
    parts = filepath.rsplit('/', 1)
    if len(parts) == 2:
        parent = parts[0] + '/'
        filename = parts[1]
        return parent, filename
    else:
        return filepath, ''


def find_latest_files(paths):
    file_dict = {}
    date_pattern = re.compile(r'\d{14}')
    keywords = ["dev", "userdebug", "XTS", "Dev", "TC", "Verify", "Specific",
                "Weekly", "BringUp", "Company"]

    for path in paths:
        parent, filename = parse_filename(path)
        match = date_pattern.search(filename)
        result = contains_keywords(path, keywords)
        if match and not result:
            date_str = match.group()
            if parent not in file_dict or date_str > file_dict[parent][1]:
                file_dict[parent] = (filename, date_str)

    latest_paths = [parent + file_dict[parent][0] for parent in file_dict]
    return latest_paths


def deduplicate_and_save(input_file, output_file):
    """
    读取指定的 CSV 文件第一列内容，分割每一行数据，并根据第9个分割的数据去重，
    保留最后一个出现的重复数据，然后将去重后的数据写入新的文件。

    :param input_file: 输入的 CSV 文件路径
    :param output_file: 输出的 CSV 文件路径
    """
    seen = {}

    with open(input_file, mode='r', newline='', encoding='utf-8') as csvfile:
        reader = csv.reader(csvfile)
        for row in reader:
            if row:  # 确保行不为空
                parts = row[0].split('/')
                # 7 6 for platform  9 8 for project
                if len(parts) >= 9:
                    key = parts[8]  # 第9个分割的数据（索引为8）
                    seen[key] = row[0]  # 保留最后一个出现的重复数据

    with open(output_file, mode='w', newline='', encoding='utf-8') as csvfile:
        writer = csv.writer(csvfile)
        for value in seen.values():
            writer.writerow([value])


def read_paths_from_csv(file_path):
    try:
        with open(file_path, 'r') as file:
            reader = csv.reader(file)
            paths = [row[0] for row in reader if row]
            return paths
    except Exception as e:
        print(f"An error occurred while reading the CSV file: {e}")
        return []


def write_paths_to_csv(file_path, paths):
    try:
        with open(file_path, 'w', newline='') as file:
            writer = csv.writer(file)
            for path in paths:
                writer.writerow([path])
        print(f'Results saved to {file_path}')
    except Exception as e:
        print(f"An error occurred while writing to the CSV file: {e}")


def check_all_project_info(args, input_csv, output_csv):
    paths = read_paths_from_csv(input_csv)
    if paths:
        print(f"Read {len(paths)} paths from {input_csv}")
        latest_paths = find_latest_files(paths)
        write_paths_to_csv(output_csv, latest_paths)
    else:
        print("No paths to process.")


def get_all_project_info(args):
    artifactory = os.path.join(args.out, "artifactory.csv")
    search_results = os.path.join(args.out, "search_results.csv")
    latest_path = os.path.join(args.out, "latest_paths.csv")
    deduplicated_paths = os.path.join(args.out, "final_paths.csv")

    dump_all_artifactory_info(args, artifactory)
    dump_all_project_info(args, artifactory, search_results)
    check_all_project_info(args, search_results, latest_path)
    deduplicate_and_save(latest_path, deduplicated_paths)
