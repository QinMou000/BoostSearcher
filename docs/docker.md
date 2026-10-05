# Docker 部署

本文用于 Linux 云服务器部署。项目根目录是镜像构建上下文，服务监听容器内的 `8080` 端口。

## 范围与实施计划

本次工作包含 Dockerfile 和 Docker Compose 部署配置，复用现有 CMake 和 Python unittest，不新增构建或启动脚本。
当前工作区的 `.trellis/` 和相关技能文件已被删除，保留这些已有改动，在本文记录计划和验证结果。

- [x] 研读现有构建、服务入口、数据解析、词典加载和测试实现。
- [x] 编写 Dockerfile 和 `.dockerignore`。
- [x] 将 HTTP 服务调整为前台运行，日志写入标准输出。
- [x] 执行现有测试和服务冒烟测试，记录镜像实测的环境限制。
- [x] 补充 Compose 服务、数据解析工具和配置验证。

## 依赖与集成

```text
engine/ + tests/
       │ CMake、C++17、Threads；Release 构建及本地测试
       ▼
parser、http_server + engine/third/dict/ + wwwroot/
       │ 运行目录固定为 /app，编译时 DICT_DIR 同步固定为 /app/engine/third/dict
       ▼
宿主机 data/raw/md/*.md → parser → data/raw.txt → http_server
                                               │
                                               ├─ GET /                 静态首页
                                               ├─ GET /s?word=关键词     JSON 搜索结果
                                               └─ GET /doc?path=文档路径 Markdown 原文
```

实现依据：`engine/CMakeLists.txt` 定义构建目标、词典目录和 CTest；
`engine/src/util.cpp` 在进程启动时加载分词词典；`engine/src/parser.cc` 规定数据目录和
`title\3content\3url\n` 语料格式；`engine/src/http_server.cc` 规定工作目录、接口和监听地址。

镜像采用 Debian Bookworm 的两个阶段：构建阶段安装 CMake、g++、make；
运行阶段保留二进制、词典、静态页面，以及 C++ 运行库、健康检查使用的 curl 和转发停止信号的 tini。
这样可以复用项目现有构建方式，并减少运行镜像的依赖。首次构建需要联网下载基础镜像和 Debian 软件包。
Dockerfile 默认通过 `APT_MIRROR` 使用中科大 Debian 镜像站，并为软件包下载设置 30 秒超时和 3 次重试；
下载失败会明确结束构建，而不会无限等待。`debian:bookworm-slim` 初始没有根证书，因此首次安装依赖使用 HTTP；
APT 仍会校验 Debian 签名的 InRelease 与软件包索引。需要使用其他镜像站时，在 Compose 的构建参数中设置 `APT_MIRROR`。

镜像默认用户为 `10001:10001`。飞牛的 ZIP 解压环境可能将普通文件权限清为不可读，
Compose 中的在线服务因此显式使用 root 读取只读挂载的数据；解析工具仍按宿主机用户运行，避免生成的 `raw.txt` 改变归属。
数据通过挂载提供，排除在镜像和构建上下文之外，更新文章不必重建镜像。
程序在启动时一次性建立内存索引，大语料会增加启动时间和内存占用；Compose 默认单任务构建，内存充足时可提高 `BUILD_JOBS`。

## 数据准备

以下命令在 Linux 云服务器的项目根目录执行。先创建 `data/raw/md/`，将 Markdown 文档放入该目录，
至少需要一篇有正文的文章。解析工具通过 `--user "$(id -u):$(id -g)"` 写入 `raw.txt`，
在线服务以只读方式挂载整个 `data/` 目录。

## Docker Compose 部署

使用 Docker Compose V2 的 `docker compose` 命令。`docker-compose.yml` 包含在线服务 `boost-searcher`
和按需运行的 `parser` 工具，二者共用一个镜像。在线服务只读挂载数据，解析工具可写挂载数据，
避免在线进程改动文章；镜像的健康检查和 tini 启动方式直接继承 Dockerfile。

```bash
# 检查包括工具服务在内的 Compose 配置。
docker compose --profile tools config --quiet

# 构建镜像，成功解析语料后才启动在线服务。
docker compose build && \
docker compose run --rm --user "$(id -u):$(id -g)" parser && \
docker compose up -d
```

`parser` 使用 `tools` profile，直接运行该服务时会自动启用；普通 `docker compose up -d`
只启动在线服务。解析使用宿主机当前用户，以便在原有数据目录中写入 `raw.txt`。
挂载目录不存在时明确报错，不自动创建空的数据目录。

默认访问 `http://服务器IP:8080/`。可通过环境变量 `HTTP_PORT` 修改宿主机端口，通过 `BUILD_JOBS`
修改编译并行数；这两个变量也可以写入项目根目录的 `.env`。例如：

```bash
HTTP_PORT=8081 docker compose up -d
BUILD_JOBS=1 docker compose build
```

服务使用 `unless-stopped` 重启策略，容器日志按单个文件最大 10 MB、最多 3 个文件轮转，限制磁盘占用。
镜像不包含业务数据，Compose 删除容器不会删除宿主机的 `data/`。

查看服务、日志及健康状态：

```bash
docker compose ps
docker compose logs -f boost-searcher
docker compose stop
```

更新 Markdown 后重新解析，成功后重启在线服务：

```bash
docker compose run --rm --user "$(id -u):$(id -g)" parser && \
docker compose restart boost-searcher
```

## Docker 命令行部署

也可直接使用 Docker 命令操作同一镜像：

```bash
docker build -t boost-searcher:latest .

# 用宿主机当前用户生成语料，避免挂载目录中产生其他用户拥有的文件。
docker run --rm --user "$(id -u):$(id -g)" \
  --mount "type=bind,src=$(pwd)/data,dst=/app/data" \
  boost-searcher:latest parser

# 在线服务只读取数据；宿主机文章和 raw.txt 必须允许容器用户读取。
docker run -d --name boost-searcher --restart unless-stopped \
  -p 8080:8080 \
  --mount "type=bind,src=$(pwd)/data,dst=/app/data,readonly" \
  boost-searcher:latest
```

必须先确认 `parser` 返回成功再启动服务。镜像没有内置文章，也不会自动下载 Gitee 内容。
云服务器安全组和系统防火墙需要允许宿主机的 `8080` 端口。

构建内存不足时：

```bash
docker build --build-arg BUILD_JOBS=1 -t boost-searcher:latest .
```

查看日志和健康状态：

```bash
docker logs -f boost-searcher
docker inspect --format '{{.State.Health.Status}}' boost-searcher
curl --fail http://127.0.0.1:8080/
curl --fail --get --data-urlencode 'word=网络协议' http://127.0.0.1:8080/s
```

浏览器访问 `http://服务器IP:8080/`。健康检查确认首页能够响应，不衡量搜索召回质量。
Docker 的健康状态本身不会触发 `--restart unless-stopped`，该策略用于进程退出后重启。
更新 Markdown 后，重新运行 `parser` 并执行 `docker restart boost-searcher`；在线进程不会自动重载索引。

## 启动行为变更

HTTP 服务统一前台运行，日志写到标准输出，供 `docker logs` 采集。
Linux 直接运行二进制时也采用这一行为，原来的自动后台化和 `http.log` 文件输出不再使用。
已有裸机部署应通过 systemd 或其他进程管理器管理服务和日志。

## 可重复的本地验证

镜像构建过程运行不依赖监听端口的 CTest。HTTP 冒烟测试在开发机本地执行；飞牛 BuildKit 的构建环境不再
启动临时 HTTP 服务，以免其端口网络限制阻断镜像构建。镜像启动后由健康检查确认首页响应，再通过浏览器完成
搜索与 Markdown 原文读取验收。

Windows 现有构建环境也可以验证源码行为：

```powershell
cmake -S engine -B build
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
python -B -m unittest discover -s tests -p test_http_server.py -v
git diff --check
```

如使用其他构建目录，可通过 `BOOST_SEARCHER_BIN_DIR` 指定 `parser` 和 `http_server` 所在目录。

Compose 配置使用本机已有的 PyYAML 和 jsonschema，对照官方 Compose JSON Schema 验证。
下载的规范缓存位于 `build/docker/compose-spec.json`，可在本机重复执行以下离线校验：

```powershell
python -c "import json, yaml, jsonschema; from pathlib import Path; jsonschema.validate(yaml.safe_load(Path('docker-compose.yml').read_text(encoding='utf-8')), json.loads(Path('build/docker/compose-spec.json').read_text(encoding='utf-8'))); print('Compose 配置规范校验通过')"
git diff --check
```

该检查验证配置格式；在具备 Docker Compose 的本地环境中，仍需运行
`docker compose --profile tools config --quiet`，验证环境变量插值和 Compose 命令行为。

### 本次验证记录

2026-10-04 在本机执行：

- `cmake -S engine -B build` 和 `cmake --build build --config Release --parallel 2`：成功。
- `ctest --test-dir build -C Release --output-on-failure`：2 项测试全部通过。
- `python -B -m unittest discover -s tests -p test_http_server.py -v`：3 项测试全部通过；该测试在本机执行，不在飞牛 BuildKit 构建阶段执行。
- `git diff --check`：通过；新增和修改文件均验证为 UTF-8 无 BOM。
- `docker-compose.yml`：YAML 解析和官方 Compose JSON Schema 校验通过；
  使用的规范 SHA-256 为 `d61cc3df8c6e6a727043e84f3405c69ffd63d341f629db8480f1d2ee5405b10c`。

冒烟测试首次执行发现 Windows 文本写入默认使用 CRLF，测试文章的标题会保留回车字符；
随后将容器测试输入明确固定为 LF，并重新执行，全部通过。该调整只限定测试夹具的输入格式。
构建仍有第三方 cppjieba 的既有 `size_t` 转 `int` 警告，未修改第三方库。

2026-10-05 已在飞牛 NAS 的真实 Docker Engine 完成部署验收：

- `docker compose up -d --build` 成功构建并启动镜像；构建阶段 CTest 使用缓存的成功结果。
- 发现 ZIP 解压后的词典、静态页面与 `data/` 文件均无读取位；镜像补充读取权限修复，在线服务以 root 读取只读数据挂载。
- 容器持续运行并通过健康检查；NAS 本机与局域网浏览器访问首页均返回 HTTP 200，查询“实习”能够返回实际文章。

## 外部资料

- [Docker 官方多阶段构建文档](https://docs.docker.com/build/building/multi-stage/)：用于区分构建阶段和运行阶段。
- [Dockerfile 官方参考](https://docs.docker.com/reference/dockerfile/)：用于确认用户、工作目录、启动命令和健康检查语义。
- [Docker 构建上下文文档](https://docs.docker.com/build/concepts/context/#dockerignore-files)：用于排除本地构建产物和数据。
- [tini 项目说明](https://github.com/krallin/tini)：用于容器内的停止信号转发和子进程回收。
- [Docker Compose 服务配置参考](https://docs.docker.com/reference/compose-file/services/)：用于构建、挂载、重启和日志配置。
- [Docker Compose profile 文档](https://docs.docker.com/compose/how-tos/profiles/)：用于按需运行解析工具。
- [中科大开源镜像站 Debian 说明](https://mirrors.ustc.edu.cn/help/debian.html)：用于 NAS 构建阶段的软件包下载。
- [Compose 官方 JSON Schema](https://raw.githubusercontent.com/compose-spec/compose-spec/master/schema/compose-spec.json)：用于本机自动配置校验。
