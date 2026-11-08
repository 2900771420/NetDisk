# 第十四天 上传端到端集成验证

今天只做一项具体工程工作 自动启动真实服务 发送真实TCP请求 比较落盘内容并检查中断清理
不是固定复盘 服务C++源码和旧测试不变 新增tests/day14_upload_integration.py 修改CMakeLists.txt
本地文件不会自动同步Linux 第13单元实际Linux结果尚未反馈 不据此回填验收通过

## 要验证的6项

空文件 UTF8文本 含全部字节值的65536字节二进制 真实分段正文在完成前不发布 提前EOF不保留半成品 中断后health仍可用
前三个成功上传加分段成功上传 共4个正式对象 检查201 文件ID JSON 字节长度以及按ID读回的完整内容
测试在自己的临时目录运行服务 使用与真实程序相同的TCP 解析 路由 write close link路径 不直接调用TempUpload代替真实服务
验证成功后临时测试目录被删除 不把测试数据混入工程data

## 关键设计与接口

TemporaryDirectory生成并拥有测试根目录 上下文退出时删除 cwd使服务的相对data路径落在测试目录
Popen接收参数列表而非shell命令 cwd为测试目录 stdin关闭 stdout与stderr合并到文件 避免未读取的PIPE填满阻塞服务
服务可执行文件先resolve为绝对路径 再改变服务工作目录 否则相对build/netdisk会在新目录下找不到
先检查自己启动的子进程仍存活 并等它的listening日志 再发请求 8080被占用会失败并打印本进程bind日志
不自动停止其他服务 运行前如已有旧服务器在其原终端停止 RUN_SERIAL避免本次CTest同时运行其他测试造成争用 不能阻止另一个独立进程占用端口
socket.create_connection返回连接对象 timeout限制网络等待 sendall提交完整字节片段 with退出关闭socket
HTTPResponse读取成熟HTTP响应格式 receive返回状态 头字段字典和bytes正文 限制响应最多4096字节 核对Content-Length和Connection
upload_header接收字节长度而不是字符数量 UTF8文本必须先encode再取len
check_created按安全文件ID定位测试目录中的对象 比较read_bytes 确认没有额外对象并等待临时名字清理
wait_partial先观察到临时文件含正确前缀且正式目录未变化 才允许发送剩余正文 这证明中途数据已被服务处理 不通过sleep猜测TCP分片
SHUT_WR只关闭客户端发送方向 服务器会读到EOF 客户端仍能读取400响应 此处验证提前EOF 不模拟RST或强杀
finally中terminate并wait自己启动的进程 超时才kill wait只负责回收进程 此过程不是服务器优雅退出能力验证
参考 https://docs.python.org/3/library/subprocess.html#subprocess.Popen
参考 https://docs.python.org/3/library/socket.html#socket.socket.shutdown

## CTest接入

find_package只查找已有Python3.8或以上解释器 不安装依赖 脚本仅使用标准库
TARGET_FILE生成实际netdisk可执行文件绝对路径 CTest本身不负责构建 所以先cmake --build
TIMEOUT30为外层失败保护 RUN_SERIAL不与其他CTest项目并行 SKIP_RETURN_CODE77让非Linux显示Skipped而不是伪造通过
Linux必须实际执行全部检查并返回0 No tests were found与Skipped都不算本次目标通过

## 完整代码

### tests\day14_upload_integration.py

```python
# ===== DAY 14 ADDED BEGIN 真实服务的上传发布与提前EOF集成测试 =====
import argparse
import http.client
import json
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import time


ADDRESS = ("127.0.0.1", 8080)
TIMEOUT = 5


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def wait_for(predicate, message):
    deadline = time.monotonic() + TIMEOUT
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.01)
    raise RuntimeError(message)


def upload_header(length):
    return (
        "POST /api/files HTTP/1.1\r\n"
        "Host: localhost\r\n"
        f"Content-Length: {length}\r\n"
        "Connection: close\r\n\r\n"
    ).encode("ascii")


def receive(sock):
    with http.client.HTTPResponse(sock) as response:
        response.begin()
        body = response.read(4097)
        headers = {name.lower(): value for name, value in response.getheaders()}
        require(len(body) <= 4096, "response exceeds test limit")
        require(headers.get("content-length") == str(len(body)), "response length mismatch")
        require(headers.get("connection", "").lower() == "close", "missing Connection close")
        return response.status, headers, body


def exchange(request):
    with socket.create_connection(ADDRESS, timeout=TIMEOUT) as sock:
        sock.sendall(request)
        return receive(sock)


def run_checks(root):
    uploads = root / "data" / "uploads"
    objects = root / "data" / "objects"
    expected = set()

    def object_names():
        return {path.name for path in objects.iterdir()}

    def clean():
        return not list(uploads.iterdir()) and object_names() == expected

    def check_created(result, payload):
        status, headers, body = result
        require(status == 201, f"upload returned {status}")
        require(headers.get("content-type") == "application/json", "wrong response type")
        require(json.loads(body) == {"status": "created"}, "wrong creation result")
        file_id = headers.get("x-file-id", "")
        require(re.fullmatch(r"upload-[A-Za-z0-9]{6}", file_id), "unsafe or missing file ID")
        require(file_id not in expected, "file ID reused")
        require((objects / file_id).read_bytes() == payload, "published bytes differ")
        expected.add(file_id)
        wait_for(clean, "temporary file leaked or unexpected object appeared")

    for payload in (b"", "网盘测试\n".encode("utf-8"), bytes(range(256)) * 256):
        check_created(exchange(upload_header(len(payload)) + payload), payload)
    print("PASS: empty, UTF-8 text and 65536-byte binary uploads")

    def wait_partial(prefix):
        def written():
            require(object_names() == expected, "incomplete body was published")
            paths = list(uploads.iterdir())
            return len(paths) == 1 and paths[0].read_bytes() == prefix
        wait_for(written, "server did not write the partial body")

    payload = b"begin\x00body\r\nend"
    with socket.create_connection(ADDRESS, timeout=TIMEOUT) as sock:
        sock.sendall(upload_header(len(payload)) + payload[:3])
        wait_partial(payload[:3])
        sock.sendall(payload[3:])
        check_created(receive(sock), payload)
    print("PASS: partial body stayed temporary until completion")

    with socket.create_connection(ADDRESS, timeout=TIMEOUT) as sock:
        sock.sendall(upload_header(10) + b"AB")
        wait_partial(b"AB")
        sock.shutdown(socket.SHUT_WR)
        status, headers, body = receive(sock)
        require(status == 400 and "x-file-id" not in headers, "incomplete upload accepted")
        require(json.loads(body)["error"]["code"] == "bad_request", "wrong EOF error")
    wait_for(clean, "incomplete upload left temporary or published data")
    status, _, body = exchange(b"GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n")
    require(status == 200 and body == b"OK\n", "service failed after incomplete upload")
    require(clean(), "health check changed stored files")
    print("PASS: incomplete upload cleaned and later health succeeded")
    return len(expected)


def stop_server(process):
    if process.poll() is None:
        process.terminate()
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=3)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("server", type=Path)
    args = parser.parse_args()
    if not sys.platform.startswith("linux"):
        print("SKIP: real NetDisk upload integration requires Linux")
        return 77
    server = args.server.resolve(strict=True)
    require(server.is_file(), "server executable is not a file")
    with tempfile.TemporaryDirectory(prefix="netdisk-day14-") as directory:
        root = Path(directory)
        (root / "data" / "uploads").mkdir(parents=True)
        (root / "data" / "objects").mkdir(parents=True)
        log_path = root / "server.log"
        with log_path.open("wb") as log:
            process = subprocess.Popen(
                [str(server)], cwd=root, stdin=subprocess.DEVNULL,
                stdout=log, stderr=subprocess.STDOUT,
            )
            try:
                def ready():
                    require(process.poll() is None, "test server exited before listening")
                    return "listening on 127.0.0.1:8080" in log_path.read_text(
                        encoding="utf-8", errors="replace"
                    )
                wait_for(ready, "test server did not start listening")
                published = run_checks(root)
            except Exception:
                print("Server log:", file=sys.stderr)
                print(log_path.read_text(encoding="utf-8", errors="replace"), file=sys.stderr)
                raise
            finally:
                stop_server(process)
    print(f"PASS: upload integration finished, 6 checks, {published} published files")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
# ===== DAY 14 ADDED END =====

```

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)

project(netdisk LANGUAGES CXX)

add_executable(netdisk src/main.cpp)
set(NETDISK_TARGETS netdisk)

include(CTest)
if(BUILD_TESTING)
    add_executable(io_tests tests/day06_io.cpp)
    target_include_directories(io_tests PRIVATE src)
    list(APPEND NETDISK_TARGETS io_tests)
    add_test(NAME io_tests COMMAND io_tests)

    add_executable(request_line_tests tests/day08_request_line.cpp)
    target_include_directories(request_line_tests PRIVATE src)
    list(APPEND NETDISK_TARGETS request_line_tests)
    add_test(NAME request_line_tests COMMAND request_line_tests)

    add_executable(request_headers_tests tests/day09_request_headers.cpp)
    target_include_directories(request_headers_tests PRIVATE src)
    list(APPEND NETDISK_TARGETS request_headers_tests)
    add_test(NAME request_headers_tests COMMAND request_headers_tests)

    add_executable(request_body_tests tests/day10_request_body.cpp)
    target_include_directories(request_body_tests PRIVATE src)
    list(APPEND NETDISK_TARGETS request_body_tests)
    add_test(NAME request_body_tests COMMAND request_body_tests)

    add_executable(http_response_tests tests/day11_http_response.cpp)
    target_include_directories(http_response_tests PRIVATE src)
    list(APPEND NETDISK_TARGETS http_response_tests)
    add_test(NAME http_response_tests COMMAND http_response_tests)

    add_executable(stream_upload_tests tests/day12_stream_upload.cpp)
    target_include_directories(stream_upload_tests PRIVATE src)
    list(APPEND NETDISK_TARGETS stream_upload_tests)
    add_test(NAME stream_upload_tests COMMAND stream_upload_tests)

    add_executable(publish_tests tests/day13_publish.cpp)
    target_include_directories(publish_tests PRIVATE src)
    list(APPEND NETDISK_TARGETS publish_tests)
    add_test(NAME publish_tests COMMAND publish_tests)

    # ===== DAY 14 ADDED BEGIN 注册自动启动服务的上传集成测试 =====
    find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
    add_test(NAME upload_integration_tests
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/day14_upload_integration.py"
            "$<TARGET_FILE:netdisk>"
    )
    set_tests_properties(upload_integration_tests PROPERTIES
        TIMEOUT 30
        RUN_SERIAL TRUE
        SKIP_RETURN_CODE 77
    )
    # ===== DAY 14 ADDED END =====
endif()

foreach(target IN LISTS NETDISK_TARGETS)
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )
endforeach()

```

## Linux虚拟机执行

更新以上两个文件 在已有工程目录执行 以下使用建议路径 若已有build-debug就统一替换build 不新建目录
运行前8080需空闲 如果旧服务仍在运行 在它的原终端停止 本次无需另开终端启动服务或手动curl
```sh
cd ~/projects/netdisk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j2
ctest --test-dir build -R '^upload_integration_tests$' -V
```
应实际运行upload_integration_tests一项 并以0退出 核心输出
PASS: empty, UTF-8 text and 65536-byte binary uploads
PASS: partial body stayed temporary until completion
PASS: incomplete upload cleaned and later health succeeded
PASS: upload integration finished, 6 checks, 4 published files

失败先看脚本打印的Server log bind地址已用则停止旧服务 找不到netdisk则先构建 找不到Python3则配置可用的Python3解释器
published bytes differ说明落盘字节不一致 incomplete body was published说明过早发布 temporary file leaked说明清理未完成
不要用程序只返回201替代文件字节核对 不通过关闭测试检查绕过失败

## 本机实际检查与范围

Python ast语法检查通过 使用socketpair验证receive对正确长度 空体和截断长度的处理通过 Windows脚本确实返回77
这些是脚本本身检查 没有执行Linux NetDisk服务 CTest或真实上传整链路 不宣称第13或14单元Linux通过
没有修改C++程序 不需要因此重跑全部旧单元测试
本次不验证并发 压力 断电 恢复 磁盘满 真正短写或EINTR 64KiB上限不变
下一步 第15单元 文件ID与存储路径隔离