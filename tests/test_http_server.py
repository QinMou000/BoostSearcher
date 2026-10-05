"""用临时语料验证容器所需的前台服务、标准输出日志和现有 HTTP 接口。"""

from __future__ import annotations

import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import unittest
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import ProxyHandler, build_opener


class HttpServerTests(unittest.TestCase):
    """复用实际构建产物，不修改项目真实文章和语料。"""

    @classmethod
    def setUpClass(cls) -> None:
        # Windows 使用多配置生成器，Linux 镜像使用单配置的 build 目录。
        root = Path(__file__).resolve().parents[1]
        default_dir = root / "build" / "Release" if os.name == "nt" else root / "build"
        bin_dir = Path(os.environ.get("BOOST_SEARCHER_BIN_DIR", default_dir)).resolve()
        suffix = ".exe" if os.name == "nt" else ""
        cls.parser = bin_dir / f"parser{suffix}"
        cls.http_server = bin_dir / f"http_server{suffix}"
        # 本机 HTTP 验证不经代理，避免构建环境的代理变量把请求送到外部网络。
        cls.opener = build_opener(ProxyHandler({}))
        for executable in (cls.parser, cls.http_server):
            # 缺少产物必须失败，不能以跳过测试掩盖构建问题。
            if not executable.is_file():
                raise RuntimeError(f"缺少测试产物，请先完成 Release 构建：{executable}")

    def setUp(self) -> None:
        # 每个用例拥有独立工作目录，也验证词典不依赖源码所在的工作目录。
        self.temp_dir = tempfile.TemporaryDirectory(prefix="boost-searcher-http-")
        self.workdir = Path(self.temp_dir.name)
        self.article = self.workdir / "data" / "raw" / "md" / "容器测试.md"
        self.article.parent.mkdir(parents=True)
        self.markdown = "# 容器部署测试\n网络协议用于容器中的搜索验证。\n"
        # 固定 Linux 文本换行，保证不同宿主系统生成相同的 Markdown 测试输入。
        self.article.write_text(self.markdown, encoding="utf-8", newline="\n")
        # 小型静态页面可隔离验证首页路由，避免依赖前端外部资源。
        wwwroot = self.workdir / "wwwroot"
        wwwroot.mkdir()
        (wwwroot / "index.html").write_text("容器首页测试", encoding="utf-8")
        self.server = None
        self.output_file = None
        self.output_path = self.workdir / "server-output.txt"

    def tearDown(self) -> None:
        # 即使用例失败也回收进程，保证后续测试能够继续使用固定的 8080 端口。
        if self.server is not None and self.server.poll() is None:
            self.server.terminate()
            try:
                self.server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.server.kill()
                self.server.wait(timeout=5)
        # 关闭文件后才删除临时目录，兼容 Windows 文件锁定语义。
        if self.output_file is not None:
            self.output_file.close()
        self.temp_dir.cleanup()

    def run_parser(self) -> subprocess.CompletedProcess:
        # 使用与部署命令相同的解析程序，而非手工构造索引语料。
        return subprocess.run(
            [str(self.parser)], cwd=self.workdir, capture_output=True,
            encoding="utf-8", errors="replace", timeout=15,
        )

    def start_server(self) -> None:
        # 固定端口被占用时明确失败，避免把其他服务的响应当作测试成功。
        with socket.socket() as probe:
            try:
                probe.bind(("127.0.0.1", 8080))
            except OSError as error:
                self.fail(f"HTTP 冒烟测试需要空闲的 8080 端口：{error}")
        parsed = self.run_parser()
        self.assertEqual(0, parsed.returncode, f"解析失败：{parsed.stdout}{parsed.stderr}")
        # 输出落到临时文件，防止未消费的管道阻塞服务线程。
        self.output_file = self.output_path.open("w", encoding="utf-8")
        self.server = subprocess.Popen(
            [str(self.http_server)], cwd=self.workdir, stdin=subprocess.DEVNULL,
            stdout=self.output_file, stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
        )
        # 同时确认入口进程存活；Linux 自动后台化会导致这里立即失败。
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            self.assertIsNone(self.server.poll(), "HTTP 服务入口进程提前退出")
            try:
                with self.opener.open("http://127.0.0.1:8080/", timeout=0.5) as response:
                    if response.status == 200:
                        return
            except (URLError, TimeoutError):
                time.sleep(0.05)
        self.fail("HTTP 服务未在 10 秒内就绪")

    def request(self, route: str) -> str:
        # 每次关闭响应，保证错误恢复测试不会耗尽服务连接。
        with self.opener.open(f"http://127.0.0.1:8080{route}", timeout=3) as response:
            return response.read().decode("utf-8")

    def test_foreground_search_document_and_stdout(self) -> None:
        """解析、首页、搜索和原文读取组成完整部署冒烟流程。"""
        self.start_server()
        self.assertEqual("容器首页测试", self.request("/"))
        results = json.loads(self.request("/s?" + urlencode({"word": "网络协议"})))
        self.assertTrue(results, "搜索应命中临时文章")
        self.assertEqual("容器部署测试", results[0]["title"])
        doc_route = "/doc?" + urlencode({"path": results[0]["url"]})
        self.assertEqual(self.markdown, self.request(doc_route))
        self.assertIsNone(self.server.poll(), "服务应持续前台运行")
        # 日志必须进入容器可采集的标准输出，不再另写 http.log。
        self.assertIn("用户搜索", self.output_path.read_text(encoding="utf-8"))
        self.assertFalse((self.workdir / "http.log").exists(), "服务日志不应写入工作目录")
        # 停止信号应结束入口进程，不能遗留后台服务。
        self.server.terminate()
        self.server.wait(timeout=3)

    def test_invalid_paths_and_missing_document_recover(self) -> None:
        """错误请求之后仍然能够处理正常查询。"""
        self.start_server()
        for path, status in ((None, 400), ("data/raw/md/../secret.md", 400),
                             ("data/raw/md/不存在.md", 404)):
            # 分别覆盖缺少参数、目录回退和不存在的合法路径。
            route = "/doc" if path is None else "/doc?" + urlencode({"path": path})
            with self.subTest(path=path):
                with self.assertRaises(HTTPError) as error:
                    self.request(route)
                self.assertEqual(status, error.exception.code)
                error.exception.close()
        self.assertEqual("请输入搜索关键字", self.request("/s"))
        self.assertEqual([], json.loads(self.request("/s?word=")))
        self.assertTrue(json.loads(self.request("/s?" + urlencode({"word": "网络协议"}))))

    def test_parser_empty_directory_can_retry(self) -> None:
        """空文章目录解析失败，补齐文章后可以正常生成语料。"""
        self.article.unlink()
        failed = self.run_parser()
        self.assertNotEqual(0, failed.returncode, "空文章目录必须阻止语料生成")
        self.assertFalse((self.workdir / "data" / "raw.txt").exists())
        # 失败后直接补齐输入重试，验证部署数据准备步骤能够恢复。
        self.article.write_text(self.markdown, encoding="utf-8", newline="\n")
        recovered = self.run_parser()
        self.assertEqual(0, recovered.returncode, recovered.stdout + recovered.stderr)
        self.assertIn("data/raw/md/容器测试.md", (self.workdir / "data" / "raw.txt").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
