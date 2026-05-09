#!/usr/bin/env python3
# encoding=utf-8
# ============================================================================
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026-2026. All rights reserved.
# ============================================================================
"""
CI 门禁脚本, 根据变更来源决定编译范围:

    仅 vendor/ 下 .c/.h 变更 → 逐一编译受影响的 sample
    仅 src/    下 .c/.h 变更 → 全量编译 SDK
    两者同时变更              → 逐一编译受影响的 sample
    无 .c/.h 变更             → 跳过
    ci_gate.py 自身变更       → 先跑测试用例, 再全量编译
"""

import subprocess
import json
import os
import sys
import time
import stat
import shutil
import hashlib
from typing import List, Set, Optional

# ============================================================
# 常量定义
# ============================================================
BUILD_INFO_FILENAME = 'build_config.json'
CMAKE_SEARCH_STRING = 'build_component()'
SAMPLE_CMAKELISTS_FILE = 'src/application/samples/CMakeLists.txt'
MENUCONFIG_FILE = 'src/build/config/target_config/ws63/menuconfig/acore/ws63_liteos_app.config'
SAMPLE_DIRECTORY = 'src/application/samples'
BUILD_DIRECTORY = 'src'
BUILD_SCRIPT = 'build.py'
OUTPUT_FWPKG_PATH = 'output/ws63/fwpkg/ws63-liteos-app/ws63-liteos-app_all.fwpkg'
ARCHIVE_DIRECTORY = 'archives'
ERRCODE_H_FILE = 'src/include/errcode.h'
ERRCODE_SEARCH_STRING = '#define ERRCODE_SUCC                                        0UL'
DEFAULT_BUILD_TIMEOUT = 60 * 5

# ============================================================
# 全局状态
# ============================================================
has_src_changes = False
global_combined = ''


# ============================================================
# git 差异分析
# ============================================================

def compare_with_remote_master() -> Set[str]:
    """
    比较当前分支与 origin/master 的差异文件，
    调用 check_changes_and_get_folders 提取受影响的 vendor/sample key 集合。
    """
    result = subprocess.run(
        ["git", "diff", "--name-only", "origin/master", "HEAD"],
        capture_output=True,
        text=True
    )
    if result.returncode != 0:
        print(f"Error getting git diff: {result.stderr}")
        sys.exit(-1)

    changed_files = result.stdout.strip().split("\n") if result.stdout else []
    print(f"[compare_with_remote_master] changed_files: {changed_files}")

    return check_changes_and_get_folders(changed_files)


def check_changes_and_get_folders(changed_files: List[str]) -> Optional[Set[str]]:
    """
    分析变更文件列表。

    - 如果只有非 C/H 文件变更 → 返回 None（无需编译）
    - 如果变更了 ci/ci_gate.py 本身 → 先跑测试用例, 然后设置 has_src_changes = True 触发全量编译
    - src/ 下的 C/H 变更 → 设置 has_src_changes = True
    - vendor/ 下的 C/H 变更 → 提取 "{vendor_name}+{subdir}+{sample}" key 加入返回集合

    返回: None（无需编译）或 Set[str]（受影响的 vendor key 集合，可能为空）
    """
    global has_src_changes
    has_src_changes = False

    # ci_gate.py 自身被修改 → 先跑测试用例，全部通过后走全量编译验证
    ci_gate_modified = False
    for file_path in changed_files:
        normalized = file_path.replace('\\', '/')
        if normalized in ('ci/ci_gate.py', 'ci_gate.py'):
            has_src_changes = True
            ci_gate_modified = True
            print("ci_gate.py modified, running test suite first...")
            run_ci_gate_tests()

    # 仅保留 .c / .h 文件
    c_h_files = [f for f in changed_files if f.endswith(('.c', '.h'))]
    if not c_h_files:
        if ci_gate_modified:
            print("[Changed folders]: set()")
            print("[has_src_changes]: True")
            return set()
        print("Not need build, only non-source files modified")
        return None

    changed_folders = set()
    for file_path in c_h_files:
        file_path = file_path.replace('\\', '/')

        if file_path.startswith('src/'):
            has_src_changes = True

        if file_path.startswith('vendor/'):
            parts = file_path.split('/')
            if len(parts) >= 4:
                folder_name = '+'.join(parts[1:4])
                changed_folders.add(folder_name)

    print(f"[Changed folders]: {changed_folders}")
    print(f"[has_src_changes]: {has_src_changes}")
    return changed_folders


# ============================================================
# build_config.json 解析
# ============================================================

def process_build_info_files() -> List[dict]:
    """
    遍历所有 vendor/*/build_config.json，返回展平后的条目列表。

    每条目包含: file_path, build_target, sample_name, platform, build_defs
    """
    print("start process_build_info_files")
    result_list = []

    for root, dirs, files in os.walk("./"):
        for file in files:
            if file == BUILD_INFO_FILENAME:
                file_path = os.path.join(root, file).replace('\\', '/')
                print(file_path)

                with open(file_path, 'r', encoding='utf-8') as f:
                    try:
                        data = json.load(f)
                        for item in data:
                            build_target = item.get('buildTarget', '')
                            relative_path = item.get('relativePath', '').replace('/', '-')
                            chip_name = item.get('chip', '')

                            build_defs = []
                            if item.get('buildDef', ''):
                                build_defs = [
                                    d.strip() for d in item['buildDef'].split(',')
                                    if d.strip()
                                ]

                            entry = {
                                'file_path': file_path,
                                'build_target': build_target,
                                'sample_name': relative_path,
                                'platform': chip_name,
                                'build_defs': build_defs,
                            }
                            result_list.append(entry)
                    except json.JSONDecodeError:
                        print(f"Error decoding JSON in file: {file_path}")
                        sys.exit(-1)

    return result_list


def extract_exact_match(entries: List[dict], match_list: Set[str]) -> List[dict]:
    """
    将 git 差异提取出的 vendor key 集合与 build_config 条目进行精确匹配。

    匹配规则:
      对每条 entry, 从 file_path 提取 vendor_name,
      将 sample_name 的 '-' 转换为 '+' 后拼接:
        "{vendor_name}+{sample_key}"
      若该 key 在 match_list 中 → 匹配成功。

    若 match_list 为空或无任何匹配 → sys.exit(0)
    """
    print("start extract_exact_match")
    print(f"[extract_exact_match] match_list: {match_list}")

    if not match_list:
        print("match_list is empty")
        sys.exit(0)

    exact_matches = []
    for entry in entries:
        file_path = entry['file_path']
        parts = file_path.split('/')
        if len(parts) >= 3:
            vendor_name = parts[2]  # ./vendor/{vendor_name}/build_config.json
        else:
            continue

        sample_key = entry['sample_name'].replace('-', '+')
        combined_key = vendor_name + '+' + sample_key

        if combined_key in match_list:
            exact_matches.append(entry)

    print(f"[extract_exact_match] exact_matches: {exact_matches}")
    if not exact_matches:
        print("build-config has not been synchronously updated")
        sys.exit(0)

    return exact_matches


# ============================================================
# 文件操作工具
# ============================================================

def run_ci_gate_tests() -> None:
    """运行 CI 门禁自身测试用例，全部通过才允许继续编译"""
    print("Running ci_gate test suite...")
    test_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'test_ci_gate.py')
    result = subprocess.run(
        ["python", test_path],
        capture_output=True,
        text=True
    )
    print(result.stdout)
    if result.returncode != 0:
        print("ci_gate tests FAILED! Fix test failures before proceeding.")
        if result.stderr:
            print(result.stderr)
        sys.exit(result.returncode)
    print("ci_gate tests all PASSED.")


def insert_content_before_line(file_path: str, target_line: str, content_to_insert: str) -> None:
    """在文件的 target_line 之前插入 content_to_insert；找不到则追加到末尾"""
    print(f"insert_content_before_line: {file_path}")
    found_target_line = False
    try:
        with open(file_path, 'r', encoding='utf-8') as file:
            lines = file.readlines()
            for i in range(len(lines)):
                if target_line in lines[i]:
                    lines.insert(i, content_to_insert)
                    found_target_line = True
                    print(f"Inserted before '{target_line}' in {file_path}")
                    break

            if not found_target_line:
                lines.append(content_to_insert)
                print(f"Appended at end of {file_path}")

            with open(file_path, 'w', encoding='utf-8') as file:
                file.writelines(lines)
    except FileNotFoundError:
        print(f"File {file_path} not found.")


def delete_specific_content(file_path: str, content_to_delete: str) -> None:
    """删除文件中所有包含 content_to_delete 的行"""
    print(f"delete_specific_content: {file_path}")
    with open(file_path, 'r', encoding='utf-8') as file:
        lines = file.readlines()

    modified_lines = [line for line in lines if content_to_delete not in line]

    with open(file_path, 'w', encoding='utf-8') as file:
        file.writelines(modified_lines)


def move_file(source_file: str, new_filename: str) -> None:
    """将构建产物移动到 archives/ 目录"""
    print("start move_file")
    target_file = os.path.join(f'../{ARCHIVE_DIRECTORY}', f'{new_filename}.fwpkg')
    os.chmod(source_file, stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)
    try:
        shutil.move(source_file, target_file)
        os.chmod(target_file, stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)
        print(f"File {source_file} moved and renamed to {target_file}")
    except FileNotFoundError:
        print(f"File {source_file} not found")
    except PermissionError:
        print(f"Permission error, cannot move file.")
    except Exception as e:
        print(f"Error: {str(e)}")


# ============================================================
# 构建宏处理
# ============================================================

def _apply_build_defs(build_defs: List[str]) -> None:
    """
    应用构建宏:
      - =y 或 = y 类型 → 写入 menuconfig 文件 (ws63_liteos_app.config)
      - 其他类型 (=value) → 写入 errcode.h 作为 #define
    """
    for defn in build_defs:
        if '=y' in defn or '= y' in defn:
            insert_content_before_line(
                MENUCONFIG_FILE,
                CMAKE_SEARCH_STRING,
                f'{defn}\n'
            )
        else:
            def_name_cleaned = defn.replace('=', ' ')
            insert_content_before_line(
                ERRCODE_H_FILE,
                ERRCODE_SEARCH_STRING,
                f'#define {def_name_cleaned}\n'
            )


def _remove_build_defs(build_defs: List[str]) -> None:
    """
    移除构建宏（与 _apply_build_defs 对应）:
      - =y / = y 类型 → 从 menuconfig 文件中删除
      - 其他类型 → 从 errcode.h 中删除对应的 #define
    """
    for defn in build_defs:
        if '=y' in defn or '= y' in defn:
            delete_specific_content(MENUCONFIG_FILE, defn)
        else:
            def_name_cleaned = defn.replace('=', ' ')
            delete_specific_content(ERRCODE_H_FILE, f'#define {def_name_cleaned}')


# ============================================================
# 单个 sample 的 prepare / cleanup
# ============================================================

def sample_build_prepare_one(entry: dict) -> None:
    """
    为单个 sample 做编译前准备:
      1. 应用构建宏 (menuconfig / errcode.h)
      2. 复制 sample 源码到 src/application/samples/
      3. 在 CMakeLists.txt 中添加 add_subdirectory_if_exist
    """
    global global_combined

    file_path = entry['file_path']
    build_target = entry['build_target']
    sample_name = entry['sample_name']
    platform = entry['platform']
    build_defs = entry.get('build_defs', [])

    # 生成 global_combined 标识
    global_combined = f'{build_target}_{sample_name}_{platform}'
    if build_defs:
        global_combined += '_'
        build_def_sha = hashlib.sha256(
            '-'.join(build_defs).encode('utf-8', errors='ignore')
        )
        global_combined += build_def_sha.hexdigest()[:32]

    # 应用构建宏
    if build_defs:
        _apply_build_defs(build_defs)

    # 复制 sample 源码
    target_string = file_path.rsplit('/build_config.json', 1)[0] + '/'
    samples = sample_name.replace('-', '/')
    source_directory = target_string + samples
    print(f"[sample_build_prepare_one] source_directory: {source_directory}")

    sample_basename = os.path.basename(source_directory)
    dest_path = os.path.join(SAMPLE_DIRECTORY, sample_basename)

    try:
        if os.path.exists(dest_path):
            shutil.rmtree(dest_path)
        shutil.copytree(source_directory, dest_path)
        print(f"Directory '{source_directory}' copied to '{SAMPLE_DIRECTORY}'")
    except shutil.Error as e:
        print(f"Error: {e}")
    except OSError as e:
        print(f"Error: {e.strerror}")

    # 添加到 CMakeLists.txt
    sample_dir_name = sample_name.split('-')[-1]
    delete_specific_content(
        SAMPLE_CMAKELISTS_FILE,
        f'add_subdirectory_if_exist({sample_dir_name})'
    )
    insert_content_before_line(
        SAMPLE_CMAKELISTS_FILE,
        CMAKE_SEARCH_STRING,
        f'add_subdirectory_if_exist({sample_dir_name})\n'
    )


def sample_build_cleanup_one(entry: dict) -> None:
    """
    单个 sample 编译后清理:
      1. 删除 sample 源码目录
      2. 从 CMakeLists.txt 中移除 add_subdirectory_if_exist
      3. 移除构建宏
    """
    sample_name = entry['sample_name']
    build_defs = entry.get('build_defs', [])

    sample_dir_name = sample_name.split('-')[-1]

    # 删除 sample 目录
    try:
        sample_path = os.path.join(SAMPLE_DIRECTORY, sample_dir_name)
        if os.path.exists(sample_path):
            shutil.rmtree(sample_path)
            print(f"Directory {sample_path} removed.")
    except OSError as e:
        print(f"Error removing {sample_dir_name}: {e.strerror}")

    # 从 CMakeLists.txt 中移除
    delete_specific_content(
        SAMPLE_CMAKELISTS_FILE,
        f'add_subdirectory_if_exist({sample_dir_name})'
    )

    # 移除构建宏
    if build_defs:
        _remove_build_defs(build_defs)


# ============================================================
# 编译
# ============================================================

def compile_sdk_and_save_log(build_target_name: str) -> None:
    """
    在 src/ 目录下执行编译，将日志保存到 archives/，
    构建产物 .fwpkg 移动到 archives/。
    """
    print("start compile_sdk_and_save_log")
    print(f"Current working directory: {os.getcwd()}")

    # 确保 archives 目录存在
    if not os.path.exists("./archives"):
        os.mkdir("./archives")

    os.chdir(BUILD_DIRECTORY)
    log_path = os.path.join('..', 'archives', f'build-{global_combined}.log')

    writer = os.fdopen(os.open(
        log_path,
        os.O_WRONLY | os.O_CREAT | os.O_TRUNC,
        stat.S_IWUSR | stat.S_IRUSR,
    ), 'wb')
    reader = os.fdopen(os.open(
        log_path,
        os.O_RDONLY,
        stat.S_IWUSR | stat.S_IRUSR,
    ), 'rb')
    os.chmod(log_path, stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)

    args = ['-c', build_target_name]
    try:
        process = subprocess.Popen(
            ['python', BUILD_SCRIPT] + args,
            text=False,
            stdout=writer,
            stderr=writer
        )
        start = time.time()
        while True:
            timeout = (time.time() - start) > DEFAULT_BUILD_TIMEOUT

            line = reader.readline()
            if line == b'':
                if process.poll() is not None:
                    break
                time.sleep(2)
                if not timeout:
                    continue
                else:
                    process.kill()
                    raise Exception("build exit cause: timeout")

            try:
                outs = line.decode('utf-8', errors='strict').rstrip()
            except UnicodeDecodeError:
                outs = line.decode('gbk', errors='replace').rstrip()
            if not outs:
                if not timeout:
                    continue
                else:
                    process.kill()
                    raise Exception("build timeout")
            print(outs)

        if process.returncode == 0:
            writer.write(b"Finished: SUCCESS")
            print(f"SDK compilation succeeded. Log saved to '{log_path}'.")
        else:
            print(f"SDK compilation failed. Check '{log_path}' for details.")
            writer.write(b"Finished: FAILURE")
            print(process.stderr.read().decode('utf-8'))

    except Exception as e:
        print(f"An error occurred: {str(e)}")
    finally:
        if writer:
            writer.close()
        if reader:
            reader.close()

    move_file(OUTPUT_FWPKG_PATH, global_combined)


# ============================================================
# 主入口
# ============================================================

def main() -> None:
    """
    门禁主流程:
      1. git diff 获取变更文件，分析受影响范围
      2. 仅 src/ 变更 → 全量编译 SDK
      3. 有 vendor/ 变更（不论是否有 src 变更）→ 匹配 build_config，逐个编译 sample
    """
    global global_combined
    print("start main")

    check_list = compare_with_remote_master()

    if check_list is None:
        print("No C/H files changed, skip build")
        sys.exit(0)

    if has_src_changes and not check_list:
        # 仅 src/ 下的 C/H 文件变更，vendor 无变更 → 全量编译 SDK
        print("Only src modifications detected, compiling SDK directly")
        global_combined = 'ws63-liteos-app'
        compile_sdk_and_save_log('ws63-liteos-app')
    else:
        # vendor 有变更（可能同时有 src 变更）→ 逐个编译受影响的 sample
        entries = process_build_info_files()
        matched = extract_exact_match(entries, check_list)

        for entry in matched:
            sample_build_prepare_one(entry)
            compile_sdk_and_save_log(entry['build_target'])
            # compile_sdk_and_save_log 内会 chdir 到 src/，回到项目根目录
            current_dir = os.getcwd()
            parent_dir = os.path.abspath(os.path.join(current_dir, os.pardir))
            os.chdir(parent_dir)
            sample_build_cleanup_one(entry)

    print("all build step execute end")


if __name__ == '__main__':
    sys.exit(main())
