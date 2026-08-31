#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# 搜索项目压力测试脚本。
#
# 设计目标：
# 1. 只使用 Python 标准库，避免给项目引入额外依赖。
# 2. 复用现有 CMake 构建系统，避免维护第二套构建入口。
# 3. 在临时工作目录中生成 data/raw.txt，保护仓库真实数据。
# 4. 同时覆盖 debug 命令行入口和 /s HTTP 搜索入口。

from __future__ import annotations

import argparse
import json
import os
import platform
import random
import shutil
import socket
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Optional, Sequence
from urllib.error import URLError
from urllib.parse import urlencode
from urllib.request import urlopen


SEP = "\3"
DEFAULT_SEED = 20260704
DEFAULT_HTTP_PORT = 8080


class StressError(RuntimeError):
    """压测流程中的可预期失败，用于统一转换为非零退出码。"""


@dataclass(frozen=True)
class Topic:
    """一类可生成语料的主题，标题词用于构造可验证的查询。"""

    title: str
    words: Sequence[str]


@dataclass(frozen=True)
class QueryCase:
    """单个查询用例，expected 为 None 时只要求流程不中断。"""

    name: str
    query: str
    expected: Optional[str] = None
    allow_empty_body: bool = False


@dataclass
class Metric:
    """压测指标汇总，所有耗时统一用毫秒输出，便于人工横向比较。"""

    name: str
    total: int
    elapsed_ms: float
    failures: int
    latencies_ms: Optional[List[float]] = None

    def print_summary(self) -> None:
        qps = self.total / (self.elapsed_ms / 1000.0) if self.elapsed_ms > 0 else 0.0
        avg_ms = self.elapsed_ms / self.total if self.total else 0.0
        parts = [
            f"[指标] {self.name}",
            f"总查询数={self.total}",
            f"耗时_ms={self.elapsed_ms:.2f}",
            f"QPS={qps:.2f}",
            f"平均延迟_ms={avg_ms:.2f}",
            f"失败数={self.failures}",
        ]
        if self.latencies_ms:
            actual_avg_ms = sum(self.latencies_ms) / len(self.latencies_ms)
            parts.append(f"实际平均响应延迟_ms={actual_avg_ms:.2f}")
            parts.append(f"P95_ms={percentile(self.latencies_ms, 95):.2f}")
            parts.append(f"P99_ms={percentile(self.latencies_ms, 99):.2f}")
        print(" ".join(parts))


TOPICS = [
    Topic("网络协议", ("TCP", "IP", "路由", "套接字", "数据链路", "拥塞控制", "握手")),
    Topic("C语言计算器", ("函数指针", "递归", "位运算", "表达式", "栈", "词法分析", "语法树")),
    Topic("单片机LED点阵", ("定时器", "扫描", "中断", "显示", "动画", "GPIO", "驱动")),
    Topic("Linux进程管理", ("fork", "exec", "信号", "守护进程", "文件描述符", "管道", "线程")),
    Topic("HTTP服务器", ("请求", "响应", "状态码", "路由", "静态文件", "连接", "并发")),
    Topic("Markdown搜索引擎", ("索引", "分词", "摘要", "高亮", "召回", "排序", "JSON")),
    Topic("数据库索引优化", ("B树", "哈希", "事务", "锁", "缓存", "查询计划", "扫描")),
    Topic("Python并发脚本", ("线程池", "超时", "统计", "队列", "压测", "采样", "标准库")),
]


VALIDATION_CASES = [
    QueryCase("精确中文命中", "网络协议", "网络协议"),
    QueryCase("模糊中文召回", "网络协义", "网络协议"),
    QueryCase("英文大小写折叠", "tcp", "网络协议"),
    QueryCase("搜索主题命中", "搜索引擎", "Markdown搜索引擎"),
    QueryCase("HTTP主题命中", "HTTP服务器", "HTTP服务器"),
]


STRESS_CASES = [
    QueryCase("高频精确词", "网络协议", "网络协议"),
    QueryCase("短语查询", "Markdown 搜索 引擎", "Markdown搜索引擎"),
    QueryCase("模糊错字", "网络协义", "网络协议"),
    QueryCase("英文大写", "TCP", "网络协议"),
    QueryCase("英文小写", "tcp", "网络协议"),
    QueryCase("服务端查询", "HTTP服务器", "HTTP服务器"),
    QueryCase("并发脚本", "线程池", "Python并发脚本"),
    QueryCase("数据库主题", "查询计划", "数据库索引优化"),
    QueryCase("单字边界", "义", None, allow_empty_body=True),
    QueryCase("停用词边界", "的 和 是", None, allow_empty_body=True),
    QueryCase("不存在词", "不存在词XYZ", None, allow_empty_body=True),
    QueryCase("长查询", "网络协议 TCP 路由 数据链路 搜索引擎 JSON 高亮", None),
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="为 boost-searcher 生成临时语料并执行本地压力测试。"
    )
    parser.add_argument("--docs", type=int, default=1000, help="生成的文档数量，默认 1000。")
    parser.add_argument("--queries", type=int, default=600, help="命令行入口总查询数，默认 600。")
    parser.add_argument("--threads", type=int, default=8, help="HTTP 并发线程数，默认 8。")
    parser.add_argument("--http-requests", type=int, default=0, help="HTTP 请求数，默认等于 --queries。")
    parser.add_argument("--build-dir", default="build/stress", help="CMake 构建目录，默认 build/stress。")
    parser.add_argument("--config", default="Release", help="多配置生成器的配置名，默认 Release。")
    parser.add_argument("--skip-build", action="store_true", help="跳过 CMake 构建，直接使用已有产物。")
    parser.add_argument("--skip-http", action="store_true", help="跳过 HTTP 压测，只测 debug 入口。")
    parser.add_argument("--http-url", default="", help="压测已运行 HTTP 服务，例如 http://127.0.0.1:8080。")
    parser.add_argument("--timeout", type=float, default=180.0, help="单阶段超时时间秒数，默认 180。")
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED, help="随机种子，默认固定以保证可复现。")
    parser.add_argument("--work-dir", default="", help="指定临时工作目录；为空时自动创建并清理。")
    parser.add_argument("--keep-temp", action="store_true", help="保留自动创建的临时工作目录，便于排查。")
    return parser.parse_args()


def repo_root() -> Path:
    # 脚本位于 tests/ 下，父目录的父目录即仓库根目录。
    return Path(__file__).resolve().parents[1]


def validate_args(args: argparse.Namespace) -> None:
    # 先在脚本边界拦截明显错误，避免启动构建后才失败。
    if args.docs <= 0:
        raise StressError("--docs 必须大于 0。")
    if args.queries <= 0:
        raise StressError("--queries 必须大于 0。")
    if args.threads <= 0:
        raise StressError("--threads 必须大于 0。")
    if args.timeout <= 0:
        raise StressError("--timeout 必须大于 0。")


def executable_name(name: str) -> str:
    return f"{name}.exe" if os.name == "nt" else name


def executable_candidates(build_dir: Path, config: str, name: str) -> List[Path]:
    exe = executable_name(name)
    # 同时兼容 Visual Studio 多配置、Ninja 单配置和少量自定义布局。
    return [
        build_dir / config / exe,
        build_dir / config.lower() / exe,
        build_dir / exe,
        build_dir / "bin" / config / exe,
        build_dir / "bin" / exe,
    ]


def find_executable(build_dir: Path, config: str, name: str) -> Path:
    for candidate in executable_candidates(build_dir, config, name):
        if candidate.exists():
            return candidate
    checked = "\n".join(f"  - {path}" for path in executable_candidates(build_dir, config, name))
    raise StressError(f"找不到可执行文件 {name}，已检查：\n{checked}")


def run_command(command: Sequence[str], cwd: Path, timeout: float) -> None:
    # capture_output 让失败时能输出上下文，同时避免构建日志淹没压测指标。
    print(f"[步骤] 执行命令：{' '.join(command)}")
    try:
        result = subprocess.run(
            list(command),
            cwd=str(cwd),
            text=True,
            encoding="utf-8",
            errors="replace",
            capture_output=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as exc:
        raise StressError(f"命令执行超时，已运行超过 {timeout:.1f} 秒：{' '.join(command)}") from exc
    if result.returncode != 0:
        if result.stdout:
            print("[标准输出]")
            print(result.stdout)
        if result.stderr:
            print("[标准错误]")
            print(result.stderr)
        raise StressError(f"命令执行失败，退出码={result.returncode}：{' '.join(command)}")


def build_targets(root: Path, build_dir: Path, args: argparse.Namespace, need_http: bool) -> None:
    if args.skip_build:
        print("[步骤] 已按参数跳过构建。")
        return

    cmake = shutil.which("cmake")
    if cmake is None:
        raise StressError("找不到 cmake；请安装 CMake，或先手动构建后使用 --skip-build。")

    run_command([cmake, "-S", "engine", "-B", str(build_dir)], root, args.timeout)
    run_command([cmake, "--build", str(build_dir), "--target", "debug", "--config", args.config], root, args.timeout)
    if need_http:
        run_command(
            [cmake, "--build", str(build_dir), "--target", "http_server", "--config", args.config],
            root,
            args.timeout,
        )


def make_work_dir(args: argparse.Namespace, root: Path) -> tuple[Path, bool]:
    # 用户指定目录时不做自动删除，避免误删人工保留的排查材料。
    if args.work_dir:
        work_dir = Path(args.work_dir).resolve()
        work_dir.mkdir(parents=True, exist_ok=True)
        return work_dir, False

    # 默认放在 build/ 下，利用既有忽略规则，同时避开部分环境不可写的系统临时目录。
    parent = root / "build" / "stress-work"
    parent.mkdir(parents=True, exist_ok=True)
    timestamp = time.strftime("%Y%m%d-%H%M%S")
    for attempt in range(100):
        work_dir = (parent / f"run-{timestamp}-{os.getpid()}-{attempt:02d}").resolve()
        try:
            work_dir.mkdir()
            return work_dir, True
        except FileExistsError:
            continue
    raise StressError("无法创建唯一的压力测试临时工作目录。")


def path_is_inside(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def build_corpus_lines(doc_count: int) -> List[str]:
    lines: List[str] = []
    # 每篇文档都包含主题词、英文词和唯一编号，方便同时压测高召回与低召回查询。
    for index in range(doc_count):
        topic = TOPICS[index % len(TOPICS)]
        unique = f"唯一标记{index:06d}"
        title = f"{topic.title} 压测文档 {index:06d}"
        repeated_words = " ".join(topic.words)
        content = (
            f"{topic.title} 用于压力测试。"
            f" {repeated_words} {repeated_words} {unique}。"
            f" 这段内容模拟 Markdown 文档正文，包含排序、摘要、召回和 JSON 输出场景。"
        )
        url = f"data/raw/md/stress/doc-{index:06d}.md"
        lines.append(f"{title}{SEP}{content}{SEP}{url}\n")
    return lines


def write_lines(path: Path, lines: Sequence[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as out:
        out.writelines(lines)


def generate_corpus(root: Path, work_dir: Path, doc_count: int) -> None:
    data_dir = work_dir / "data"
    data_dir.mkdir(parents=True, exist_ok=True)
    (work_dir / "wwwroot").mkdir(parents=True, exist_ok=True)
    raw_path = data_dir / "raw.txt"
    lines = build_corpus_lines(doc_count)

    write_lines(raw_path, lines)

    # 兼容旧构建产物：早期 debug/http_server 读取 ../../data/raw.txt。
    # 只允许在 build/ 忽略目录内写兼容副本，绝不覆盖仓库根 data/raw.txt。
    legacy_raw_path = (work_dir / ".." / ".." / "data" / "raw.txt").resolve()
    build_root = (root / "build").resolve()
    if path_is_inside(legacy_raw_path, build_root):
        write_lines(legacy_raw_path, lines)
        print(f"[提示] 已写入旧产物兼容语料：{legacy_raw_path}，下次运行会覆盖它。")

    print(f"[步骤] 已生成临时语料：{raw_path}，文档数={doc_count}")


def build_query_sequence(total: int, seed: int) -> List[QueryCase]:
    rng = random.Random(seed)
    queries = list(VALIDATION_CASES)
    while len(queries) < total:
        queries.append(rng.choice(STRESS_CASES))
    return queries[:total]


def extract_json_arrays(output: str) -> List[object]:
    decoder = json.JSONDecoder()
    arrays: List[object] = []
    index = 0

    # debug 输出里同时存在提示符和 [INFO] 日志，只在真正能解码为 JSON 数组时记录。
    while index < len(output):
        bracket = output.find("[", index)
        if bracket == -1:
            break
        try:
            value, offset = decoder.raw_decode(output[bracket:])
        except json.JSONDecodeError:
            index = bracket + 1
            continue
        if isinstance(value, list) and (not value or isinstance(value[0], dict)):
            arrays.append(value)
        index = bracket + max(offset, 1)
    return arrays


def result_contains(results: object, expected: str) -> bool:
    # 搜索结果结构来自 nlohmann/json，递归转字符串能兼容 title、desc、url、keywords。
    return expected in json.dumps(results, ensure_ascii=False)


def response_sample(value: object, limit: int = 160) -> str:
    # 失败日志只需要能定位响应来源，限制长度可避免大结果集淹没关键错误。
    text = json.dumps(value, ensure_ascii=False)
    compact = " ".join(text.split())
    return compact[:limit]


def validate_cli_results(results: Sequence[object]) -> int:
    failures = 0
    if len(results) < len(VALIDATION_CASES):
        print(f"[失败] 命令行输出 JSON 数组数量不足：实际={len(results)} 期望至少={len(VALIDATION_CASES)}")
        return len(VALIDATION_CASES)

    for index, case in enumerate(VALIDATION_CASES):
        if case.expected and not result_contains(results[index], case.expected):
            print(f"[失败] 命令行校验未命中：用例={case.name} 查询={case.query} 期望={case.expected}")
            failures += 1
    return failures


def run_cli_stress(debug_exe: Path, work_dir: Path, queries: Sequence[QueryCase], timeout: float) -> int:
    query_text = "\n".join(case.query for case in queries) + "\n"
    print(f"[步骤] 开始命令行入口压测：{debug_exe}")
    start = time.perf_counter()
    try:
        result = subprocess.run(
            [str(debug_exe)],
            cwd=str(work_dir),
            input=query_text,
            text=True,
            encoding="utf-8",
            errors="replace",
            capture_output=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as exc:
        raise StressError(f"命令行入口压测超时，已运行超过 {timeout:.1f} 秒。") from exc
    elapsed_ms = (time.perf_counter() - start) * 1000.0

    failures = 0
    if result.returncode != 0:
        print(result.stdout)
        print(result.stderr)
        failures += 1
        print(f"[失败] debug 退出码异常：{result.returncode}")

    arrays = extract_json_arrays(result.stdout)
    failures += validate_cli_results(arrays)
    Metric("命令行搜索入口", len(queries), elapsed_ms, failures).print_summary()
    return failures


def normalize_base_url(url: str) -> str:
    return url.rstrip("/")


def http_search(base_url: str, query: str, timeout: float) -> str:
    encoded = urlencode({"word": query})
    with urlopen(f"{normalize_base_url(base_url)}/s?{encoded}", timeout=timeout) as response:
        body = response.read()
        charset = response.headers.get_content_charset() or "utf-8"
        return body.decode(charset, errors="replace")


def is_port_open(host: str, port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(0.5)
        return sock.connect_ex((host, port)) == 0


def wait_http_ready(base_url: str, timeout: float) -> None:
    deadline = time.perf_counter() + timeout
    last_error: Optional[Exception] = None
    while time.perf_counter() < deadline:
        try:
            body = http_search(base_url, VALIDATION_CASES[0].query, 2.0)
            parsed = parse_http_body(body, VALIDATION_CASES[0])
            if result_contains(parsed, VALIDATION_CASES[0].expected or ""):
                return
            last_error = StressError("HTTP 服务已响应，但未命中本次压测语料")
        except Exception as exc:  # noqa: BLE001 - 这里需要保留最后一次启动失败原因。
            last_error = exc
        time.sleep(0.2)
    raise StressError(f"HTTP 服务未在 {timeout:.1f} 秒内就绪，最后错误：{last_error}")


def start_local_http_server(http_exe: Path, work_dir: Path, timeout: float) -> subprocess.Popen[str]:
    if is_port_open("127.0.0.1", DEFAULT_HTTP_PORT):
        raise StressError("本机 8080 端口已被占用；请使用 --http-url 指向现有服务，或使用 --skip-http。")

    print(f"[步骤] 启动本地 HTTP 服务：{http_exe}")
    process = subprocess.Popen(
        [str(http_exe)],
        cwd=str(work_dir),
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    wait_http_ready(f"http://127.0.0.1:{DEFAULT_HTTP_PORT}", min(timeout, 30.0))
    return process


def stop_process(process: Optional[subprocess.Popen[str]]) -> None:
    if process is None:
        return
    if process.poll() is not None:
        return

    # 先优雅终止，超时后再强制结束，防止压测脚本留下后台进程。
    process.terminate()
    try:
        process.wait(timeout=5.0)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5.0)


def parse_http_body(body: str, case: QueryCase) -> Optional[object]:
    if not body.strip():
        if case.allow_empty_body or case.expected is None:
            return []
        raise StressError(f"HTTP 响应为空：用例={case.name} 查询={case.query}")
    return json.loads(body)


def run_one_http_request(base_url: str, case: QueryCase, timeout: float, validate_content: bool) -> tuple[float, bool, str]:
    start = time.perf_counter()
    try:
        body = http_search(base_url, case.query, min(timeout, 10.0))
        parsed = parse_http_body(body, case)
        if validate_content and case.expected and not result_contains(parsed, case.expected):
            message = (
                f"用例={case.name} 查询={case.query} "
                f"期望={case.expected} 响应摘要={response_sample(parsed)}"
            )
            return (time.perf_counter() - start) * 1000.0, False, message
        return (time.perf_counter() - start) * 1000.0, True, ""
    except (URLError, TimeoutError, json.JSONDecodeError, StressError) as exc:
        return (time.perf_counter() - start) * 1000.0, False, str(exc)


def run_http_stress(base_url: str, queries: Sequence[QueryCase], threads: int, timeout: float, validate_content: bool) -> int:
    print(f"[步骤] 开始 HTTP 入口压测：{base_url}，并发={threads}")
    failures = 0
    latencies: List[float] = []
    start = time.perf_counter()

    with ThreadPoolExecutor(max_workers=threads) as executor:
        futures = [
            executor.submit(run_one_http_request, base_url, case, timeout, validate_content)
            for case in queries
        ]
        for future in as_completed(futures):
            latency_ms, ok, message = future.result()
            latencies.append(latency_ms)
            if not ok:
                failures += 1
                if failures <= 10:
                    print(f"[失败] HTTP 请求失败：{message}")

    elapsed_ms = (time.perf_counter() - start) * 1000.0
    Metric("HTTP搜索入口", len(queries), elapsed_ms, failures, latencies).print_summary()
    return failures


def should_auto_start_http(args: argparse.Namespace) -> bool:
    # 非 Windows 平台的 http_server 会 daemon 化，自动启动后无法可靠回收。
    return (not args.skip_http) and (not args.http_url) and platform.system() == "Windows"


def run(args: argparse.Namespace) -> int:
    validate_args(args)
    root = repo_root()
    build_dir = (root / args.build_dir).resolve()
    need_local_http_target = should_auto_start_http(args)

    build_targets(root, build_dir, args, need_local_http_target)
    debug_exe = find_executable(build_dir, args.config, "debug")
    http_exe = find_executable(build_dir, args.config, "http_server") if need_local_http_target else None

    work_dir, auto_created_work_dir = make_work_dir(args, root)
    server_process: Optional[subprocess.Popen[str]] = None
    total_failures = 0

    try:
        generate_corpus(root, work_dir, args.docs)
        queries = build_query_sequence(args.queries, args.seed)
        total_failures += run_cli_stress(debug_exe, work_dir, queries, args.timeout)

        if args.skip_http:
            print("[步骤] 已按参数跳过 HTTP 压测。")
        elif args.http_url:
            http_total = args.http_requests if args.http_requests > 0 else args.queries
            http_queries = build_query_sequence(http_total, args.seed + 1)
            total_failures += run_http_stress(args.http_url, http_queries, args.threads, args.timeout, False)
        elif platform.system() == "Windows":
            if http_exe is None:
                raise StressError("内部错误：HTTP 自动启动需要 http_server 可执行文件。")
            server_process = start_local_http_server(http_exe, work_dir, args.timeout)
            http_total = args.http_requests if args.http_requests > 0 else args.queries
            http_queries = build_query_sequence(http_total, args.seed + 1)
            total_failures += run_http_stress(
                f"http://127.0.0.1:{DEFAULT_HTTP_PORT}",
                http_queries,
                args.threads,
                args.timeout,
                True,
            )
        else:
            print("[提示] 当前平台的 http_server 会后台 daemon 化，未提供 --http-url 时默认跳过 HTTP 压测。")
    finally:
        stop_process(server_process)
        if auto_created_work_dir:
            if args.keep_temp:
                print(f"[提示] 已按参数保留临时目录：{work_dir}")
            else:
                shutil.rmtree(work_dir, ignore_errors=True)

    if total_failures:
        print(f"[结果] 压力测试失败，失败数={total_failures}")
        return 1
    print("[结果] 压力测试通过。")
    return 0


def percentile(values: Iterable[float], percent: int) -> float:
    sorted_values = sorted(values)
    if not sorted_values:
        return 0.0
    # 使用 nearest-rank 定义，输出直观，且不需要额外依赖 numpy。
    rank = max(1, round(percent / 100.0 * len(sorted_values)))
    return sorted_values[min(rank, len(sorted_values)) - 1]


def main() -> int:
    args = parse_args()
    try:
        return run(args)
    except StressError as exc:
        print(f"[错误] {exc}")
        return 1
    except KeyboardInterrupt:
        print("[错误] 用户中断压力测试。")
        return 130


if __name__ == "__main__":
    sys.exit(main())
