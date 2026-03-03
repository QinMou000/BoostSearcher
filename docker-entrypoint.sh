#!/usr/bin/env bash
set -e

RAW_FILE="./data/raw_html/raw.txt"

if [ ! -f "$RAW_FILE" ]; then
  echo "[entrypoint] raw.txt 不存在，先运行 parser 生成原始语料..."
  ./parser
fi

echo "[entrypoint] 启动 HTTP 搜索服务..."
exec ./http_server

