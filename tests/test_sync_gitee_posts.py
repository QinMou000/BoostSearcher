#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Gitee 文章同步工具的离线单元测试。"""

from __future__ import annotations

import json
import base64
import io
import re
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path, PurePosixPath
from unittest import mock

from tools import sync_gitee_posts as sync


class FakeFetcher:
    """按 URL 返回固定字节，避免测试访问真实 Gitee 服务。"""

    def __init__(self, responses: dict[str, bytes]) -> None:
        self.responses = responses
        self.calls: list[str] = []

    def __call__(self, url: str, timeout_seconds: float, max_bytes: int) -> bytes:
        self.calls.append(url)
        if url not in self.responses:
            raise sync.SyncError(f"测试网络未提供响应：{url}")
        return self.responses[url]


def entry(
    path: str,
    content: bytes = "# 测试文章\n正文".encode("utf-8"),
    download_url: str | None = None,
) -> dict[str, str]:
    """构造符合 Gitee Contents API 形状的单篇文章记录。"""

    source_path = PurePosixPath(path)
    return {
        "type": "file",
        "path": path,
        "download_url": download_url or sync.build_download_url(source_path),
        "sha": sync.git_blob_sha(content),
    }


def listing(entries: list[dict[str, str]]) -> bytes:
    """把测试文章清单编码为真实接口使用的 UTF-8 JSON。"""

    return json.dumps(entries, ensure_ascii=False).encode("utf-8")


def contents_api_response(path: str, content: bytes) -> bytes:
    """构造 Gitee Contents API 的 Base64 单文件响应。"""

    return json.dumps(
        {
            "type": "file",
            "path": path,
            "encoding": "base64",
            "content": base64.b64encode(content).decode("ascii"),
            "sha": sync.git_blob_sha(content),
        },
        ensure_ascii=False,
    ).encode("utf-8")


class SyncGiteePostsTests(unittest.TestCase):
    """覆盖预览、实际下载、重复执行和安全边界。"""

    def run_sync(self, target: Path, apply: bool, fetcher: FakeFetcher) -> tuple[sync.SyncStats, list[str]]:
        output: list[str] = []
        stats = sync.sync_posts(
            target_directory=target,
            apply=apply,
            timeout_seconds=1.0,
            max_new_files=10,
            fetch_bytes=fetcher,
            print_line=output.append,
        )
        return stats, output

    def test_default_cli_real_sync_and_output_directory_option(self) -> None:
        """命令行默认真实同步，且输出目录可由 --output-dir 明确指定。"""

        default_args = sync.parse_args([])
        custom_args = sync.parse_args(
            ["--dry-run", "--output-dir", "D:/同步测试", "--workers", "8"]
        )

        self.assertFalse(default_args.dry_run)
        self.assertEqual(sync.DEFAULT_WORKERS, default_args.workers)
        self.assertTrue(custom_args.dry_run)
        self.assertEqual("D:/同步测试", custom_args.output_dir)
        self.assertEqual(8, custom_args.workers)

    def test_rejects_invalid_worker_count(self) -> None:
        """并发数越界应在发起网络请求前失败，避免定时任务耗尽本机线程。"""

        with tempfile.TemporaryDirectory() as temporary_directory:
            with self.assertRaises(sync.SyncError):
                sync.sync_posts(
                    target_directory=Path(temporary_directory),
                    apply=True,
                    timeout_seconds=1.0,
                    max_new_files=1,
                    workers=0,
                    fetch_bytes=FakeFetcher({}),
                )

    def test_log_file_path_uses_script_name_instead_of_http_log(self) -> None:
        """同步工具日志必须按自身脚本命名，不能与 HTTP 服务共用。"""

        expected_path = sync.repository_root() / "sync_gitee_posts.log"
        self.assertEqual(expected_path, sync.log_file_path())
        self.assertNotEqual(sync.repository_root() / "http.log", sync.log_file_path())

    def test_file_logger_uses_expected_format_without_duplicate_handlers(self) -> None:
        """重复初始化同一文件时应复用处理器，并保持完整日志字段顺序。"""

        with tempfile.TemporaryDirectory() as temporary_directory:
            log_path = Path(temporary_directory) / "sync_gitee_posts.log"
            logger = sync.configure_file_logger(log_path)
            try:
                same_logger = sync.configure_file_logger(log_path)
                logger.info("独立日志格式测试")

                self.assertIs(logger, same_logger)
                self.assertEqual(1, len(logger.handlers))
                log_line = log_path.read_text(encoding="utf-8")
                self.assertRegex(
                    log_line,
                    re.compile(
                        r"\[INFO\] \[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\] "
                        r"\[独立日志格式测试\] \[.+\.py : \d+\]\n"
                    ),
                )
            finally:
                sync.close_file_logger(logger)

    def test_main_logs_success_events_to_independent_file(self) -> None:
        """入口应把正常同步事件同时写入终端和独立日志文件。"""

        with tempfile.TemporaryDirectory() as temporary_directory:
            log_path = Path(temporary_directory) / "sync_gitee_posts.log"

            def fake_sync_posts(**kwargs: object) -> sync.SyncStats:
                print_line = kwargs["print_line"]
                self.assertTrue(callable(print_line))
                print_line("[汇总] 远端=1 已同步=1 待新增=0 待更新=0")
                print_line("[结果] 已执行，新增=0 更新=0 已同步=1 失败=0")
                return sync.SyncStats(remote_count=1, up_to_date_count=1)

            stdout = io.StringIO()
            with (
                mock.patch.object(sync, "log_file_path", return_value=log_path),
                mock.patch.object(sync, "sync_posts", side_effect=fake_sync_posts),
                redirect_stdout(stdout),
            ):
                exit_code = sync.main(["--output-dir", temporary_directory])

            self.assertEqual(0, exit_code)
            self.assertIn("[汇总]", stdout.getvalue())
            log_content = log_path.read_text(encoding="utf-8")
            self.assertIn("工具启动：", log_content)
            self.assertIn("[汇总] 远端=1 已同步=1 待新增=0 待更新=0", log_content)

    def test_main_logs_sync_error_to_independent_file(self) -> None:
        """入口捕获同步错误后，应同时写入标准错误和独立日志文件。"""

        with tempfile.TemporaryDirectory() as temporary_directory:
            log_path = Path(temporary_directory) / "sync_gitee_posts.log"
            stderr = io.StringIO()
            with (
                mock.patch.object(sync, "log_file_path", return_value=log_path),
                mock.patch.object(sync, "sync_posts", side_effect=sync.SyncError("离线测试失败")),
                redirect_stderr(stderr),
            ):
                exit_code = sync.main(["--output-dir", temporary_directory])

            self.assertEqual(1, exit_code)
            self.assertIn("[错误] 离线测试失败", stderr.getvalue())
            self.assertRegex(
                log_path.read_text(encoding="utf-8"),
                r"\[ERROR\].*\[同步失败：离线测试失败\]",
            )

    def test_preview_only_lists_missing_post_without_writing(self) -> None:
        """预览模式只读取目录清单，不能请求正文或写入目标目录。"""

        remote_entry = entry("source/_posts/新文章.md")
        fetcher = FakeFetcher({sync.LIST_URL: listing([remote_entry])})
        with tempfile.TemporaryDirectory() as temporary_directory:
            target = Path(temporary_directory) / "md"
            stats, output = self.run_sync(target, apply=False, fetcher=fetcher)

            self.assertEqual(1, stats.planned_new_count)
            self.assertEqual(0, stats.new_count)
            self.assertFalse((target / "新文章.md").exists())
            self.assertEqual([sync.LIST_URL], fetcher.calls)
            self.assertTrue(any(line.startswith("[预览]") for line in output))

    def test_apply_downloads_then_second_run_skips_existing_post(self) -> None:
        """实际下载写入完整内容，重复同步必须保留原文件且不再请求正文。"""

        article_content = "# 新增文章\n正文".encode("utf-8")
        remote_entry = entry("source/_posts/新增文章.md", article_content)
        article_url = remote_entry["download_url"]
        with tempfile.TemporaryDirectory() as temporary_directory:
            target = Path(temporary_directory) / "md"
            first_fetcher = FakeFetcher(
                {sync.LIST_URL: listing([remote_entry]), article_url: article_content}
            )
            first_stats, _ = self.run_sync(target, apply=True, fetcher=first_fetcher)

            destination = target / "新增文章.md"
            self.assertEqual(1, first_stats.new_count)
            self.assertEqual("# 新增文章\n正文", destination.read_text(encoding="utf-8"))
            self.assertFalse(list(target.glob("*.tmp")))

            second_fetcher = FakeFetcher({sync.LIST_URL: listing([remote_entry])})
            second_stats, _ = self.run_sync(target, apply=True, fetcher=second_fetcher)
            self.assertEqual(1, second_stats.up_to_date_count)
            self.assertEqual([sync.LIST_URL], second_fetcher.calls)
            self.assertEqual("# 新增文章\n正文", destination.read_text(encoding="utf-8"))

    def test_apply_updates_existing_post_when_remote_blob_changes(self) -> None:
        """同路径文章的远端 SHA 改变后，工具必须以新内容原子替换旧内容。"""

        remote_content = "# 修订后的文章\n远端正文".encode("utf-8")
        remote_entry = entry("source/_posts/已有文章.md", remote_content)
        article_url = remote_entry["download_url"]
        with tempfile.TemporaryDirectory() as temporary_directory:
            target = Path(temporary_directory) / "md"
            target.mkdir(parents=True)
            destination = target / "已有文章.md"
            destination.write_text("# 旧文章\n本地旧正文", encoding="utf-8")
            fetcher = FakeFetcher(
                {sync.LIST_URL: listing([remote_entry]), article_url: remote_content}
            )

            stats, _ = self.run_sync(target, apply=True, fetcher=fetcher)
            self.assertEqual(1, stats.planned_update_count)
            self.assertEqual(1, stats.updated_count)
            self.assertEqual("# 修订后的文章\n远端正文", destination.read_text(encoding="utf-8"))

    def test_falls_back_to_contents_api_when_raw_download_fails(self) -> None:
        """原始下载地址受限时，同路径 Contents API 仍可完成受 SHA 保护的同步。"""

        article_content = "# 备用通道\n正文".encode("utf-8")
        remote_entry = entry("source/_posts/备用通道.md", article_content)
        contents_url = sync.build_contents_url(PurePosixPath(remote_entry["path"]))
        fetcher = FakeFetcher(
            {
                sync.LIST_URL: listing([remote_entry]),
                contents_url: contents_api_response(remote_entry["path"], article_content),
            }
        )
        with tempfile.TemporaryDirectory() as temporary_directory:
            target = Path(temporary_directory) / "md"
            stats, _ = self.run_sync(target, apply=True, fetcher=fetcher)

            self.assertEqual(1, stats.new_count)
            self.assertEqual(article_content, (target / "备用通道.md").read_bytes())
            self.assertIn(contents_url, fetcher.calls)

    def test_crlf_local_file_matches_lf_remote_blob_without_download(self) -> None:
        """Windows 检出的 CRLF 文件与同内容 LF 远端文章应被判定为已同步。"""

        remote_content = "# 换行测试\n第一行\n第二行\n".encode("utf-8")
        remote_entry = entry("source/_posts/换行测试.md", remote_content)
        with tempfile.TemporaryDirectory() as temporary_directory:
            target = Path(temporary_directory) / "md"
            target.mkdir(parents=True)
            destination = target / "换行测试.md"
            destination.write_bytes(remote_content.replace(b"\n", b"\r\n"))
            fetcher = FakeFetcher({sync.LIST_URL: listing([remote_entry])})

            stats, _ = self.run_sync(target, apply=True, fetcher=fetcher)
            self.assertEqual(1, stats.up_to_date_count)
            self.assertEqual([sync.LIST_URL], fetcher.calls)

    def test_rejects_untrusted_download_url(self) -> None:
        """目录记录即使是 Markdown，也不能把下载请求导向任意主机。"""

        remote_entry = entry(
            "source/_posts/危险文章.md",
            download_url="https://example.com/danger.md",
        )
        fetcher = FakeFetcher({sync.LIST_URL: listing([remote_entry])})

        with self.assertRaises(sync.SyncError):
            sync.list_remote_posts(fetcher, timeout_seconds=1.0)

    def test_rejects_download_url_with_invalid_port(self) -> None:
        """畸形端口不能让 URL 解析异常越过统一的同步错误边界。"""

        remote_entry = entry(
            "source/_posts/异常端口.md",
            download_url="https://gitee.com:invalid/wang-qin928/personal_post/raw/main/source/_posts/异常端口.md",
        )
        fetcher = FakeFetcher({sync.LIST_URL: listing([remote_entry])})

        with self.assertRaises(sync.SyncError):
            sync.list_remote_posts(fetcher, timeout_seconds=1.0)

    def test_rejects_path_traversal(self) -> None:
        """路径回退不能借助远端清单逃出 data/raw/md 目录。"""

        remote_entry = entry("source/_posts/../越界.md")
        fetcher = FakeFetcher({sync.LIST_URL: listing([remote_entry])})

        with self.assertRaises(sync.SyncError):
            sync.list_remote_posts(fetcher, timeout_seconds=1.0)

    def test_download_failure_leaves_no_partial_file(self) -> None:
        """请求正文失败只计为失败，不会创建空文件或临时残留。"""

        remote_entry = entry("source/_posts/网络失败.md")
        with tempfile.TemporaryDirectory() as temporary_directory:
            target = Path(temporary_directory) / "md"
            fetcher = FakeFetcher({sync.LIST_URL: listing([remote_entry])})
            stats, _ = self.run_sync(target, apply=True, fetcher=fetcher)

            self.assertEqual(1, stats.failed_count)
            self.assertFalse((target / "网络失败.md").exists())
            self.assertFalse(target.exists())

    def test_rejects_too_large_content_without_writing(self) -> None:
        """超出单篇限制的响应不能落盘，也不能留下临时文件。"""

        remote_entry = entry("source/_posts/大文章.md")
        article_url = remote_entry["download_url"]
        oversized = b"x" * (sync.MAX_FILE_BYTES + 1)
        with tempfile.TemporaryDirectory() as temporary_directory:
            target = Path(temporary_directory) / "md"
            fetcher = FakeFetcher({sync.LIST_URL: listing([remote_entry]), article_url: oversized})
            stats, _ = self.run_sync(target, apply=True, fetcher=fetcher)

            self.assertEqual(1, stats.failed_count)
            self.assertFalse((target / "大文章.md").exists())
            self.assertFalse(list(target.glob("*.tmp")) if target.exists() else [])


if __name__ == "__main__":
    unittest.main()
