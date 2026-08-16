#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从指定 Gitee 文章仓库补齐本地缺失的 Markdown 文件。"""

from __future__ import annotations

import argparse
import base64
import binascii
import hashlib
import json
import logging
import os
import re
import sys
import tempfile
from concurrent.futures import Future, ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Callable, Iterator, Sequence
from urllib.error import HTTPError, URLError
from urllib.parse import quote, unquote, urlsplit, urlunsplit
from urllib.request import Request, urlopen


REPOSITORY_OWNER = "wang-qin928"
REPOSITORY_NAME = "personal_post"
BRANCH = "main"
SOURCE_DIRECTORY = PurePosixPath("source/_posts")
API_HOST = "gitee.com"
RAW_HOST = "gitee.com"
LIST_URL = (
    "https://gitee.com/api/v5/repos/"
    f"{REPOSITORY_OWNER}/{REPOSITORY_NAME}/contents/{SOURCE_DIRECTORY}?ref={BRANCH}"
)
DEFAULT_TIMEOUT_SECONDS = 20.0
DEFAULT_MAX_NEW_FILES = 500
DEFAULT_WORKERS = 4
MAX_FILE_BYTES = 5 * 1024 * 1024
LOG_FORMAT = "[%(levelname)s] [%(asctime)s] [%(message)s] [%(filename)s : %(lineno)d]"
LOG_DATE_FORMAT = "%Y-%m-%d %H:%M:%S"


class SyncError(RuntimeError):
    """同步过程可预期的失败，调用方据此返回非零退出码。"""


@dataclass(frozen=True)
class RemotePost:
    """通过远端目录接口确认过的一篇 Markdown 文章。"""

    source_path: PurePosixPath
    download_url: str
    blob_sha: str

    @property
    def relative_path(self) -> PurePosixPath:
        """返回文章相对 `_posts` 目录的路径，用作本地落盘路径。"""

        return self.source_path.relative_to(SOURCE_DIRECTORY)


@dataclass
class SyncStats:
    """汇总本次同步结果，便于终端和后续定时任务读取。"""

    remote_count: int = 0
    up_to_date_count: int = 0
    planned_new_count: int = 0
    planned_update_count: int = 0
    new_count: int = 0
    updated_count: int = 0
    failed_count: int = 0


@dataclass(frozen=True)
class PostSyncResult:
    """单篇文章的后台下载和原子发布结果，主线程据此有序输出日志。"""

    outcome: str
    error_message: str = ""


FetchBytes = Callable[[str, float, int], bytes]
PrintLine = Callable[[str], None]


def repository_root() -> Path:
    """根据脚本位置定位仓库根目录，避免依赖调用时的工作目录。"""

    return Path(__file__).resolve().parents[1]


def log_file_path(script_path: Path | None = None) -> Path:
    """按脚本文件名生成独立日志路径，避免工具之间混写同一个日志文件。"""

    resolved_script_path = (script_path or Path(__file__)).resolve()
    return repository_root() / f"{resolved_script_path.stem}.log"


def configure_file_logger(log_path: Path | None = None) -> logging.Logger:
    """创建或复用指定文件的日志器，确保同次进程运行不会重复写入。"""

    resolved_log_path = (log_path or log_file_path()).resolve()
    # 日志器名称包含完整路径摘要，避免临时目录测试与默认日志器互相复用处理器。
    logger_name = f"{__name__}.{hashlib.sha1(str(resolved_log_path).encode()).hexdigest()}"
    logger = logging.getLogger(logger_name)
    logger.setLevel(logging.INFO)
    logger.propagate = False

    if logger.handlers:
        return logger

    # 指定 UTF-8，保证同步文章名等中文文本在不同 Windows 代码页下保持可读。
    file_handler = logging.FileHandler(resolved_log_path, encoding="utf-8")
    file_handler.setFormatter(logging.Formatter(LOG_FORMAT, datefmt=LOG_DATE_FORMAT))
    logger.addHandler(file_handler)
    return logger


def close_file_logger(logger: logging.Logger) -> None:
    """关闭并移除文件处理器，避免 Windows 在进程内持续锁定日志文件。"""

    for handler in logger.handlers[:]:
        # 先解绑再关闭，使同一进程后续初始化能够创建可用的新处理器。
        logger.removeHandler(handler)
        handler.close()


def print_and_log(message: str, logger: logging.Logger) -> None:
    """保持原终端输出，同时把同步事件写入独立日志文件。"""

    print(message)
    # 单篇失败需提升为 ERROR，其余既有进度行使用 INFO 保持语义稳定。
    if message.startswith("[失败]"):
        logger.error(message)
    else:
        logger.info(message)


def build_download_url(source_path: PurePosixPath) -> str:
    """按固定仓库协议构造原文地址，避免信任任意外部下载地址。"""

    encoded_path = quote(source_path.as_posix(), safe="/")
    return (
        f"https://{RAW_HOST}/{REPOSITORY_OWNER}/{REPOSITORY_NAME}/raw/"
        f"{BRANCH}/{encoded_path}"
    )


def build_contents_url(source_path: PurePosixPath) -> str:
    """构造同一仓库文章的 Contents API 地址，作为原始地址失败时的备用通道。"""

    encoded_path = quote(source_path.as_posix(), safe="/")
    return (
        "https://gitee.com/api/v5/repos/"
        f"{REPOSITORY_OWNER}/{REPOSITORY_NAME}/contents/{encoded_path}?ref={BRANCH}"
    )


def git_blob_sha(content: bytes) -> str:
    """计算 Git Blob SHA，和 Gitee Contents API 的 sha 字段保持同一语义。"""

    digest = hashlib.sha1()
    digest.update(f"blob {len(content)}\0".encode("ascii"))
    digest.update(content)
    return digest.hexdigest()


def normalized_markdown_chunks(path: Path) -> Iterator[bytes]:
    """按 Git 文本检出规则把 CRLF 规范化为 LF，并正确处理分块边界。"""

    pending_carriage_return = b""
    with path.open("rb") as source_file:
        while chunk := source_file.read(64 * 1024):
            chunk = pending_carriage_return + chunk
            pending_carriage_return = b""
            if chunk.endswith(b"\r"):
                pending_carriage_return = b"\r"
                chunk = chunk[:-1]
            normalized = chunk.replace(b"\r\n", b"\n")
            if normalized:
                yield normalized
    if pending_carriage_return:
        yield pending_carriage_return


def local_git_blob_sha(path: Path) -> str:
    """流式计算本地 Markdown 的规范化 Git Blob SHA，避免 CRLF 造成误更新。"""

    try:
        normalized_size = sum(len(chunk) for chunk in normalized_markdown_chunks(path))
        digest = hashlib.sha1()
        digest.update(f"blob {normalized_size}\0".encode("ascii"))
        for chunk in normalized_markdown_chunks(path):
            digest.update(chunk)
    except OSError as exc:
        raise SyncError(f"无法读取本地文章：{path}，原因={exc}") from exc
    return digest.hexdigest()


def fetch_url(url: str, timeout_seconds: float, max_bytes: int) -> bytes:
    """请求一个受大小限制的 HTTPS 资源，并将网络错误转换为业务错误。"""

    request = Request(
        url,
        headers={
            "Accept": "application/json, text/markdown, text/plain;q=0.9",
            "User-Agent": "boost-searcher-content-sync/1.0",
        },
    )
    try:
        with urlopen(request, timeout=timeout_seconds) as response:
            content = response.read(max_bytes + 1)
    except HTTPError as exc:
        raise SyncError(f"请求失败，HTTP 状态码={exc.code}，地址={url}") from exc
    except (URLError, TimeoutError, OSError) as exc:
        raise SyncError(f"请求失败，地址={url}，原因={exc}") from exc

    if len(content) > max_bytes:
        raise SyncError(f"响应超过大小限制 {max_bytes} 字节，地址={url}")
    return content


def decode_json(content: bytes, url: str) -> object:
    """将 Gitee API 的 UTF-8 响应解析为 JSON，拒绝不完整或异常数据。"""

    try:
        return json.loads(content.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise SyncError(f"接口返回的 JSON 无法解析，地址={url}，原因={exc}") from exc


def validate_source_path(value: object) -> PurePosixPath:
    """校验远端路径仅能位于预期文章目录，防止目录回退写入本地。"""

    if not isinstance(value, str) or not value:
        raise SyncError("远端文章缺少有效 path 字段。")

    source_path = PurePosixPath(value)
    if source_path.is_absolute() or ".." in source_path.parts:
        raise SyncError(f"远端文章路径非法：{value}")
    if source_path.parent != SOURCE_DIRECTORY or source_path.suffix.lower() != ".md":
        raise SyncError(f"远端文章不在受支持目录或不是 Markdown：{value}")
    return source_path


def validate_download_url(value: object, source_path: PurePosixPath) -> str:
    """只接受精确匹配本仓库、分支和文章路径的 HTTPS 原文地址。"""

    if not isinstance(value, str) or not value:
        raise SyncError(f"远端文章缺少下载地址：{source_path}")

    try:
        parsed = urlsplit(value)
        port = parsed.port
    except ValueError as exc:
        raise SyncError(f"远端文章下载地址格式非法：{value}") from exc
    expected_path = (
        f"/{REPOSITORY_OWNER}/{REPOSITORY_NAME}/raw/{BRANCH}/"
        f"{source_path.as_posix()}"
    )
    decoded_path = unquote(parsed.path)
    if (
        parsed.scheme != "https"
        or parsed.hostname != RAW_HOST
        or port is not None
        or parsed.username is not None
        or parsed.password is not None
        or parsed.query
        or parsed.fragment
        or decoded_path != expected_path
    ):
        raise SyncError(f"远端文章下载地址不在白名单内：{value}")

    # URL 中的中文路径需要编码后再交给 urllib，避免依赖系统默认编码。
    return urlunsplit((parsed.scheme, parsed.netloc, quote(decoded_path, safe="/"), "", ""))


def validate_blob_sha(value: object, source_path: PurePosixPath) -> str:
    """只接受 Git SHA-1 Blob 摘要，避免错误元数据触发无意义覆盖。"""

    if not isinstance(value, str) or re.fullmatch(r"[0-9a-f]{40}", value) is None:
        raise SyncError(f"远端文章缺少有效 Git Blob SHA：{source_path}")
    return value


def list_remote_posts(fetch_bytes: FetchBytes, timeout_seconds: float) -> list[RemotePost]:
    """枚举文章目录，跳过非文件和非 Markdown 条目。"""

    payload = decode_json(fetch_bytes(LIST_URL, timeout_seconds, MAX_FILE_BYTES), LIST_URL)
    if not isinstance(payload, list):
        raise SyncError("文章目录接口返回格式错误：预期为数组。")

    posts: list[RemotePost] = []
    for entry in payload:
        if not isinstance(entry, dict) or entry.get("type") != "file":
            continue
        path_value = entry.get("path")
        if isinstance(path_value, str) and not path_value.lower().endswith(".md"):
            continue
        source_path = validate_source_path(path_value)
        download_url = validate_download_url(entry.get("download_url"), source_path)
        blob_sha = validate_blob_sha(entry.get("sha"), source_path)
        posts.append(RemotePost(source_path, download_url, blob_sha))

    # 路径是唯一标识，远端重复条目代表接口数据异常，不能静默重复下载。
    if len({post.source_path for post in posts}) != len(posts):
        raise SyncError("远端文章目录包含重复路径。")
    return sorted(posts, key=lambda post: post.source_path.as_posix())


def destination_for(target_directory: Path, post: RemotePost) -> Path:
    """把已经验证的 POSIX 相对路径映射为本地目标文件。"""

    return target_directory.joinpath(*post.relative_path.parts)


def publish_file(destination: Path, content: bytes, expected_blob_sha: str) -> str:
    """将远端完整内容原子发布到本地，返回新增、更新或无需处理的状态。"""

    if not content:
        raise SyncError(f"远端文章内容为空：{destination.name}")
    if len(content) > MAX_FILE_BYTES:
        raise SyncError(f"远端文章超过 {MAX_FILE_BYTES} 字节限制：{destination.name}")
    if git_blob_sha(content) != expected_blob_sha:
        raise SyncError(f"文章正文与目录接口提供的 SHA 不一致：{destination.name}")

    temporary_path: Path | None = None
    try:
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(
            mode="wb",
            dir=destination.parent,
            prefix=f".{destination.name}.",
            suffix=".tmp",
            delete=False,
        ) as temporary_file:
            temporary_file.write(content)
            temporary_file.flush()
            os.fsync(temporary_file.fileno())
            temporary_path = Path(temporary_file.name)
    except OSError as exc:
        raise SyncError(f"无法写入文章临时文件：{destination}，原因={exc}") from exc

    try:
        if destination.exists():
            if not destination.is_file():
                raise SyncError(f"目标路径不是普通文件：{destination}")
            if local_git_blob_sha(destination) == expected_blob_sha:
                return "unchanged"
            # 已存在但 SHA 不同，os.replace 让读者只能看到旧完整文件或新完整文件。
            os.replace(temporary_path, destination)
            return "updated"

        # 目标原本不存在时用硬链接发布完整临时文件，避免出现半写入的新文件。
        os.link(temporary_path, destination)
        return "new"
    except FileExistsError:
        # 并发任务可能在检查与发布之间完成写入；重新比较后决定是否需要更新。
        if destination.is_file() and local_git_blob_sha(destination) == expected_blob_sha:
            return "unchanged"
        os.replace(temporary_path, destination)
        return "updated"
    except OSError as exc:
        raise SyncError(f"无法原子发布文章：{destination}，原因={exc}") from exc
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def fetch_content_from_api(
    post: RemotePost,
    timeout_seconds: float,
    fetch_bytes: FetchBytes,
) -> bytes:
    """从 Contents API 解码同路径原文，并复核路径和 SHA 防止备用通道越权。"""

    contents_url = build_contents_url(post.source_path)
    payload = decode_json(fetch_bytes(contents_url, timeout_seconds, MAX_FILE_BYTES * 2), contents_url)
    if not isinstance(payload, dict):
        raise SyncError(f"备用接口返回格式错误：{post.source_path}")
    if validate_source_path(payload.get("path")) != post.source_path:
        raise SyncError(f"备用接口返回了错误文章路径：{post.source_path}")
    if validate_blob_sha(payload.get("sha"), post.source_path) != post.blob_sha:
        raise SyncError(f"备用接口返回的文章 SHA 不一致：{post.source_path}")
    if payload.get("encoding") != "base64" or not isinstance(payload.get("content"), str):
        raise SyncError(f"备用接口未返回 Base64 正文：{post.source_path}")

    try:
        # Gitee 可能在 Base64 中插入换行，去除空白后严格解码仍能拒绝异常字符。
        encoded_content = "".join(payload["content"].split())
        content = base64.b64decode(encoded_content, validate=True)
    except (binascii.Error, ValueError, TypeError) as exc:
        raise SyncError(f"备用接口正文无法 Base64 解码：{post.source_path}") from exc
    if len(content) > MAX_FILE_BYTES:
        raise SyncError(f"备用接口正文超过 {MAX_FILE_BYTES} 字节限制：{post.source_path}")
    return content


def fetch_post_content(
    post: RemotePost,
    timeout_seconds: float,
    fetch_bytes: FetchBytes,
) -> bytes:
    """优先读取原始 Markdown；失败后才使用受路径和 SHA 约束的备用接口。"""

    try:
        return fetch_bytes(post.download_url, timeout_seconds, MAX_FILE_BYTES)
    except SyncError as raw_error:
        try:
            return fetch_content_from_api(post, timeout_seconds, fetch_bytes)
        except SyncError as api_error:
            raise SyncError(
                f"原始地址失败：{raw_error}；Contents API 备用下载失败：{api_error}"
            ) from api_error


def sync_one_post(
    post: RemotePost,
    destination: Path,
    timeout_seconds: float,
    fetch_bytes: FetchBytes,
) -> PostSyncResult:
    """在工作线程中下载一篇文章；异常转换为结果，避免中断其他文章。"""

    try:
        content = fetch_post_content(post, timeout_seconds, fetch_bytes)
        return PostSyncResult(publish_file(destination, content, post.blob_sha))
    except SyncError as exc:
        return PostSyncResult("failed", str(exc))
    except Exception as exc:  # noqa: BLE001 - 后台任务不能因未预期异常终止整批同步。
        return PostSyncResult("failed", f"未预期异常：{exc}")


def sync_posts(
    target_directory: Path,
    apply: bool,
    timeout_seconds: float,
    max_new_files: int,
    workers: int = DEFAULT_WORKERS,
    fetch_bytes: FetchBytes = fetch_url,
    print_line: PrintLine = print,
) -> SyncStats:
    """同步文章并返回统计信息；预览模式不会请求文章正文或写入本地。"""

    if timeout_seconds <= 0:
        raise SyncError("超时必须大于 0 秒。")
    if max_new_files <= 0:
        raise SyncError("单次最大新增数量必须大于 0。")
    if workers <= 0 or workers > 16:
        raise SyncError("并发下载数必须在 1 到 16 之间。")

    posts = list_remote_posts(fetch_bytes, timeout_seconds)
    stats = SyncStats(remote_count=len(posts))
    pending_posts: list[tuple[RemotePost, Path, str]] = []

    for post in posts:
        destination = destination_for(target_directory, post)
        if destination.exists():
            if destination.is_file():
                if local_git_blob_sha(destination) == post.blob_sha:
                    stats.up_to_date_count += 1
                else:
                    stats.planned_update_count += 1
                    pending_posts.append((post, destination, "更新"))
                continue
            stats.failed_count += 1
            print_line(f"[失败] 目标路径不是普通文件：{destination}")
            continue
        stats.planned_new_count += 1
        pending_posts.append((post, destination, "新增"))

    if len(pending_posts) > max_new_files:
        raise SyncError(
            f"待同步文章数={len(pending_posts)}，超过单次上限={max_new_files}；"
            "请确认后使用更大的 --max-new-files。"
        )

    print_line(
        "[汇总] "
        f"远端={stats.remote_count} 已同步={stats.up_to_date_count} "
        f"待新增={stats.planned_new_count} 待更新={stats.planned_update_count}"
    )

    if not apply:
        for post, destination, action in pending_posts:
            print_line(f"[预览] {action} {post.relative_path.as_posix()} -> {destination}")
    elif pending_posts:
        print_line(f"[同步] 并发下载数={workers}，待处理文章数={len(pending_posts)}")
        futures: dict[Future[PostSyncResult], PurePosixPath] = {}
        results: dict[PurePosixPath, PostSyncResult] = {}
        with ThreadPoolExecutor(max_workers=workers) as executor:
            for post, destination, _ in pending_posts:
                future = executor.submit(
                    sync_one_post, post, destination, timeout_seconds, fetch_bytes
                )
                futures[future] = post.source_path
            for future, source_path in futures.items():
                results[source_path] = future.result()

        # 任务可并行完成，但按远端路径稳定输出，方便比较两次定时任务日志。
        for post, destination, _ in pending_posts:
            result = results[post.source_path]
            if result.outcome == "new":
                stats.new_count += 1
                print_line(f"[新增] {post.relative_path.as_posix()} -> {destination}")
            elif result.outcome == "updated":
                stats.updated_count += 1
                print_line(f"[更新] {post.relative_path.as_posix()} -> {destination}")
            elif result.outcome == "unchanged":
                stats.up_to_date_count += 1
                print_line(f"[跳过] 并发写入已完成：{destination}")
            else:
                stats.failed_count += 1
                print_line(f"[失败] {post.relative_path.as_posix()}：{result.error_message}")

    mode = "已执行" if apply else "仅预览"
    print_line(
        "[结果] "
        f"{mode}，新增={stats.new_count} 更新={stats.updated_count} "
        f"已同步={stats.up_to_date_count} 失败={stats.failed_count}"
    )
    return stats


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    """解析命令行参数，默认真实同步到仓库内的原始 Markdown 目录。"""

    parser = argparse.ArgumentParser(description="从 Gitee 真实同步 Markdown 文章到本地目录。")
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="仅预览新增和更新，不下载正文，也不修改输出目录。",
    )
    parser.add_argument(
        "--output-dir",
        default=str(repository_root() / "data" / "raw" / "md"),
        help="Markdown 输出目录，默认仓库 data/raw/md。",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=DEFAULT_TIMEOUT_SECONDS,
        help=f"单次网络请求超时秒数，默认 {DEFAULT_TIMEOUT_SECONDS:g}。",
    )
    parser.add_argument(
        "--max-new-files",
        type=int,
        default=DEFAULT_MAX_NEW_FILES,
        help=f"单次允许新增的最大文章数，默认 {DEFAULT_MAX_NEW_FILES}。",
    )
    parser.add_argument(
        "--workers",
        type=int,
        default=DEFAULT_WORKERS,
        help=f"同时下载文章的工作线程数，范围 1 到 16，默认 {DEFAULT_WORKERS}。",
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    """运行独立同步工具，并把同步失败转换为适合调度器判断的退出码。"""

    args = parse_args(argv)
    try:
        logger = configure_file_logger()
    except OSError as exc:
        print(f"[错误] 无法初始化日志文件：{exc}", file=sys.stderr)
        return 1

    logger.info(
        "工具启动："
        f"输出目录={Path(args.output_dir).resolve()} 预览={args.dry_run} "
        f"超时={args.timeout:g} 最大新增={args.max_new_files} 并发数={args.workers}"
    )
    try:
        try:
            stats = sync_posts(
                target_directory=Path(args.output_dir).resolve(),
                apply=not args.dry_run,
                timeout_seconds=args.timeout,
                max_new_files=args.max_new_files,
                workers=args.workers,
                print_line=lambda message: print_and_log(message, logger),
            )
        except SyncError as exc:
            print(f"[错误] {exc}", file=sys.stderr)
            logger.error("同步失败：%s", exc)
            return 1
        return 1 if stats.failed_count else 0
    finally:
        # 独立命令结束后释放文件句柄，便于 Windows 的调度器、测试和清理任务继续操作日志。
        close_file_logger(logger)


if __name__ == "__main__":
    sys.exit(main())
