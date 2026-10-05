# 构建和运行阶段使用同一发行版，避免 C++ 动态库版本不一致。
FROM debian:bookworm-slim AS build

# slim 基础镜像尚无根证书，首次安装依赖必须使用 HTTP 镜像站；软件包索引仍由 Debian 签名校验。
ARG APT_MIRROR=http://mirrors.ustc.edu.cn

# 控制编译并行数，降低小内存云服务器上的构建峰值。
ARG BUILD_JOBS=2

# 构建阶段只安装 CMake 与 C++ 编译工具；HTTP 冒烟测试由开发机执行。
RUN sed -i "s|http://deb.debian.org|${APT_MIRROR}|g" /etc/apt/sources.list.d/debian.sources \
    && apt-get -o Acquire::Retries=3 -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30 update \
    && apt-get install -y --no-install-recommends cmake g++ make \
    && rm -rf /var/lib/apt/lists/*

# 程序使用相对数据路径；两阶段统一工作目录和编译时词典目录。
WORKDIR /app
COPY engine/ ./engine/
COPY tests/ ./tests/

# 将配置、编译和 CTest 拆为独立步骤，飞牛界面可准确显示失败阶段。
RUN cmake -S engine -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DDICT_DIR=/app/engine/third/dict

RUN cmake --build build --parallel "${BUILD_JOBS}"

# 仅运行不依赖监听端口的 CTest；HTTP 服务在容器启动后由健康检查和浏览器验收。
RUN ctest --test-dir build --output-on-failure

FROM debian:bookworm-slim AS runtime

# 运行阶段沿用可在无根证书环境启动的镜像站，避免首次 apt-get 证书校验失败。
ARG APT_MIRROR=http://mirrors.ustc.edu.cn

# curl 用于健康检查；tini 负责转发容器停止信号并回收子进程。
RUN sed -i "s|http://deb.debian.org|${APT_MIRROR}|g" /etc/apt/sources.list.d/debian.sources \
    && apt-get -o Acquire::Retries=3 -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30 update \
    && apt-get install -y --no-install-recommends libstdc++6 curl tini \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid 10001 searcher \
    && useradd --uid 10001 --gid 10001 --no-create-home --shell /usr/sbin/nologin searcher

WORKDIR /app
ENV LANG=C.UTF-8

# 运行镜像只复制必要产物；parser 用于单独生成挂载目录中的语料。
COPY --from=build /app/build/http_server /app/build/parser /usr/local/bin/
COPY --from=build /app/engine/third/dict/ ./engine/third/dict/
COPY wwwroot/ ./wwwroot/

# 飞牛从 ZIP 解压构建上下文时可能清除普通文件的读取位；服务用户必须能读取词典和静态页面。
RUN chmod -R a+rX /app/engine/third/dict /app/wwwroot

# 文章和 raw.txt 由宿主机挂载提供，镜像不包含本地业务数据。
RUN mkdir -p /app/data/raw/md \
    && chown -R 10001:10001 /app/data

# 服务只需读取数据，使用普通用户即可监听 8080 端口。
USER 10001:10001
EXPOSE 8080

# 为启动时的内存索引构建预留时间，之后检查首页是否能正常响应。
HEALTHCHECK --interval=30s --timeout=5s --start-period=60s --retries=3 \
    CMD curl --fail --silent --show-error --max-time 4 http://127.0.0.1:8080/ > /dev/null || exit 1

# 使用可执行文件形式启动，避免额外的 shell 干扰信号转发。
ENTRYPOINT ["/usr/bin/tini", "--"]
CMD ["/usr/local/bin/http_server"]
