FROM ubuntu:22.04

# 1. 安装构建和运行依赖
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential \
        libboost-all-dev \
        libjsoncpp-dev \
        ca-certificates && \
    rm -rf /var/lib/apt/lists/*

# 2. 拷贝源码并编译
WORKDIR /app
COPY . .

# 如果以后引入 CMake，可以在这里改为 cmake/make
RUN make

# 3. 运行时配置
# 默认 HTTP 监听 8080 端口
EXPOSE 8080

# 启动前，如果没有 raw.txt，则先执行 parser
COPY docker-entrypoint.sh /app/docker-entrypoint.sh
RUN chmod +x /app/docker-entrypoint.sh

CMD ["/app/docker-entrypoint.sh"]

