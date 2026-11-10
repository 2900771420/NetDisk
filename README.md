# NetDisk 本地参考工程

当前学习单元 第27单元 deleting状态与删除恢复 本地参考已更新 Linux验证待反馈

第27单元DELETE先以SQLite事务把ready切换为deleting 再删除正式对象 最后以第二个事务删除deleting元数据
列表 详情 下载与重复DELETE只读取ready 因此删除意图提交后新请求立即看到404 中断后服务在监听前继续完成删除
启动恢复遇到对象缺失时只收尾元数据 遇到匹配普通文件时删除对象再收尾 类型或大小异常时保留证据并拒绝监听 ready对象不会被恢复器删除
新增deletion_recovery_tests覆盖对象存在 对象缺失 第二次运行幂等 大小异常保留与修复重试 ready对象保护 新集成测试覆盖真实服务多次重启和正常DELETE
同步src/deletion_recovery.h src/file_metadata_store.h src/file_catalog.h src/main.cpp tests/day24_file_pagination.cpp tests/day27_deletion_recovery.cpp tests/day27_deletion_recovery.py与CMakeLists.txt
复用build构建后只运行 ctest --test-dir build -R '^deletion_recovery_integration_tests$' -V 应通过一项并输出PASS: deletion recovery integration finished, 3 groups
本机Windows g++严格编译运行deletion_recovery_tests及分页 目录 改名删除 发布状态 发布恢复 SQLite生命周期六项回归通过 全部14个Python测试脚本语法通过 Linux生产服务启动删除恢复待反馈

以下为第26单元及更早的历史说明 不要求每日重跑

第26单元服务在监听端口前扫描全部publishing记录 根据data uploads临时名和data objects正式名完成幂等归类
只有临时文件时创建正式硬链接并删除临时名 只有正式对象时直接切换ready 两个名称指向同一文件时清理临时名 两者都缺失时取消记录
任一现存路径不是普通文件 大小与元数据不符 或两个名称指向不同文件时启动失败并保留publishing证据 不公开也不静默删除
新增publishing_recovery_tests覆盖完成 取消 重试 冲突保护与第二次运行幂等 新集成测试覆盖真实服务连续三次启动及恢复后下载
同步src/file_metadata_store.h src/publishing_recovery.h src/main.cpp tests/day26_publishing_recovery.cpp tests/day26_publishing_recovery.py与CMakeLists.txt
复用build构建后只运行 ctest --test-dir build -R '^publishing_recovery_integration_tests$' -V 应通过一项并输出PASS: publishing recovery integration finished, 2 groups
本机Windows g++严格编译运行publishing_recovery_tests及发布状态 分页 目录 改名删除 SQLite生命周期五项回归通过 全部13个Python测试脚本语法通过 Linux生产服务启动恢复待反馈

以下为第25单元及更早的历史说明 不要求每日重跑

第25单元上传完整接收后先以显式SQLite事务插入publishing记录 再通过硬链接发布正式对象 最后以第二个事务把状态切换为ready
列表 详情和下载现在都要求ready 未登记或publishing对象即使已经存在于data/objects也返回404 下载同时核对磁盘大小与元数据大小
发布文件失败时事务性撤销publishing记录 ready转换失败时保留对象与publishing记录供第26单元恢复 不删除提交结果可能不确定的对象
新增publish_state_tests覆盖事务提交 冲突回滚 非法状态转换 取消发布和公开可见性 新集成测试覆盖真实列表详情下载及正常上传最终状态
按活动源码标注规则DAY01至DAY24标注已清理 当前代码只标注DAY25真实变化
同步src/file_metadata_store.h src/temp_upload.h src/file_download.h src/main.cpp 相关C++和Python回归测试 新增tests/day25_publish_state.cpp tests/day25_publish_state.py并更新CMakeLists.txt
复用build构建后只运行 ctest --test-dir build -R '^publish_state_integration_tests$' -V 应通过一项并输出PASS: publishing state integration finished, 3 groups
本机Windows g++严格编译运行publish_state_tests及分页 目录 改名删除 SQLite生命周期四项回归通过 全部12个Python测试脚本语法通过 Linux生产服务与真实发布顺序待反馈 临时检查产物已删除

以下为第24单元及更早的历史说明 不要求每日重跑

第24单元GET HEAD /api/files支持limit与cursor limit默认100且范围1至100 排序键固定为created_at加id 有下一页时返回next_cursor
SQLite查询使用严格大于游标的复合条件和limit加一行探测 不使用OFFSET 删除已浏览记录不会让后续页位置前移 同秒记录由ID稳定打破平局
新增file_pagination_tests覆盖三页遍历 删除第一页记录后的第二页稳定性 同秒顺序 最后一页边界以及非法 重复 未知 溢出查询参数
按用户要求活动src tests和CMakeLists中的DAY01至DAY23标注已全部删除 历史教程和快照保留 当前代码只标注DAY24真实变化
同步src/file_metadata_store.h src/file_catalog.h tests/day18_file_catalog.py tests/day24_file_pagination.cpp与CMakeLists.txt
复用build构建后只运行 ctest --test-dir build -R '^file_pagination_tests$' -V 应通过一项并输出PASS: keyset pagination order, deletion stability and query bounds
本机Windows g++严格编译运行file_pagination_tests及三项直接相关回归通过 全部11个Python测试脚本语法通过 Linux构建与真实curl分页待反馈 临时检查产物已删除

以下为第23单元及更早的历史说明 不要求每日重跑

第23单元新增src/file_metadata_store.h 服务启动时一次准备INSERT SELECT UPDATE DELETE语句 所有ID与名称使用sqlite3_bind绑定 语句在请求间复用并由RAII重置和最终释放
上传发布后插入ready元数据 列表详情和改名改读写SQLite 删除移除对象后删除元数据 显示名称重启后保留 旧进程内map与磁盘目录扫描已删除
元数据插入失败时TempUpload尝试补偿删除刚发布对象 文件系统与SQLite仍不是同一原子事务 第25至27单元继续完善发布删除状态与恢复
CMake让目录和改名单测链接SQLite并注册metadata_persistence_integration_tests 新脚本覆盖含伪SQL名称精确保存 数据库行 重启列表详情下载 删除与再次重启
同步CMakeLists.txt src/main.cpp src/temp_upload.h src/file_catalog.h src/file_metadata_store.h tests/day18_file_catalog.cpp tests/day19_file_mutation.cpp tests/day19_file_mutation.py tests/day21_first_release.py tests/day23_metadata_persistence.py
复用build构建后只运行 ctest --test-dir build -R '^metadata_persistence_integration_tests$' -V 应实际通过一项并输出PASS: metadata persistence integration finished, 4 stages
本机Windows g++严格编译并运行SQLite生命周期 目录详情 改名删除三项C++测试通过 Python变化脚本语法与Windows跳过77检查通过 Linux生产服务重启持久化待反馈 临时检查产物已删除

以下为第22单元及更早的历史说明 不要求每日重跑

第22单元新增src/metadata_database.h 管理SQLite连接 busy timeout 外键开关 schema事务 版本检查和析构关闭 main在监听前打开data/netdisk.db并初始化schema 失败直接退出
初始schema包含schema_info和files两张表 files预留id name size state created_at 第23单元已经开始写入和读取真实文件元数据
CMake新增系统SQLite3依赖并注册sqlite_lifecycle_tests 测试覆盖首次建表 重复初始化 外键与版本 新版本拒绝且不降级 打开失败和损坏数据库诊断
首次使用在Debian Ubuntu安装libsqlite3-dev和sqlite3 同步CMakeLists.txt src/main.cpp src/metadata_database.h tests/day22_sqlite_lifecycle.cpp
复用build重新配置构建后只运行 ctest --test-dir build -R '^sqlite_lifecycle_tests$' -V 应实际通过一项并输出PASS: sqlite lifecycle, schema version and failure diagnostics
本机Windows g++以严格警告参数链接现有SQLite动态库并实际运行sqlite_lifecycle_tests通过 临时声明头和测试产物已删除 另用Python sqlite3确认schema执行 Linux构建CTest与真实服务启动待反馈

以下为第21单元及更早的历史说明 不要求每日重跑

第21单元不修改生产C++ 新增tests/day21_first_release.py并在CMake注册first_release_integration_tests 固化docs/api-v0.1.md
测试使用一个独立服务依次验证空列表 64KiB二进制上传 列表详情 中文改名 HEAD GET逐字节下载 DELETE 删除后404与最终health 共七步
同步CMakeLists.txt和tests/day21_first_release.py 保留tests/day16_download_integration.py与tests/day19_file_mutation.py 无新依赖 src目录保持不变
复用build构建后只运行 ctest --test-dir build -R '^first_release_integration_tests$' -V 应实际通过一项 最后输出PASS: first release workflow finished, 7 steps Linux无SKIP 8080需空闲
本机完成Python语法 依赖导入 Windows跳过77与CMake标注检查 Linux真实TCP与磁盘闭环未执行

以下为第20单元及更早的历史说明 不要求每日重跑

第20单元不修改生产C++ 现有TempUpload析构清理 link发布提交点 UniqueFd与MSG_NOSIGNAL已经形成正确恢复路径 本次新增tests/day20_failure_recovery.py并在CMake注册failure_recovery_integration_tests
测试在独立目录验证缺失对象404 不可读对象500 临时目录缺失与发布目录缺失500且无泄漏 上传提交前RST不发布 提交后客户端丢失不回滚 下载中RST后服务仍可处理health
同步CMakeLists.txt和tests/day20_failure_recovery.py 保留第16单元Python辅助脚本 生产src目录与旧测试保持不变 无新依赖
复用build构建后只运行 ctest --test-dir build -R '^failure_recovery_integration_tests$' -V 应实际通过一项 最后输出PASS: failure recovery integration finished, 7 groups Linux无SKIP 8080需空闲
本机仅完成Python语法 导入与Windows跳过77检查 用户反馈第20单元测试完成但未提供详细输出 不扩展为所有旧测试 并发或持久化能力通过

以下为第19单元及更早的历史说明 不要求每日重跑

PATCH /api/files/{id}正文为仅含name字符串的JSON 名称1至255 UTF8字节 拒绝路径分隔符控制字符空白名称 点及双点 名称只存本进程map 重启恢复ID加.bin 不移动磁盘对象 下载建议名仍ID加.bin
DELETE /api/files/{id}只删除合法ID普通对象 成功204无Content-Length或正文 之后列表无该项 详情与下载404 重复删除404 无回收站 无断电持久化保证
同步src/file_catalog.h src/http_response.h src/file_download.h CMakeLists.txt tests/day18_file_catalog.py tests/day19_file_mutation.cpp tests/day19_file_mutation.py 保留第16单元辅助脚本 main和解析器不变 无新依赖
复用build构建后只运行 ctest --test-dir build -R '^file_mutation_integration_tests$' -V 应实际通过一项 输出PASS: file mutation integration finished, 6 groups Linux无SKIP 8080需空闲 测试使用独立目录并自动启动停止重启自己的进程
本机Windows改名删除单测与响应目录回归通过 Python语法导入跳过及DELETE响应辅助检查通过 Linux新服务未编译执行 不宣称并发删除下载或优雅退出

以下为第18单元及更早的历史说明 不要求每日重跑

GET HEAD /api/files返回files数组 GET HEAD /api/files/{id}返回id name size nlohmann/json负责序列化 name目前仍为ID加.bin 不保存原始上传名称 无摘要时间数据库分页 查询参数继续忽略
只扫描正式对象目录合法ID普通文件 按ID排序 最多1000个 超限或存储目录错误返回500 不静默截断 POST上传与文件下载继续可用
首次安装系统JSON开发包 Debian Ubuntu为nlohmann-json3-dev CMake查找nlohmann_json至少3.2.0 同步src/file_catalog.h src/main.cpp tests/day18_file_catalog.cpp tests/day18_file_catalog.py及CMakeLists.txt 保留第16单元Python脚本
复用build配置构建后只运行 ctest --test-dir build -R '^file_catalog_integration_tests$' -V 应实际通过一项 输出PASS: file catalog integration finished, 5 groups Linux无SKIP 8080需空闲 自动服务使用独立临时目录
本机Windows以官方3.11.3头完成JSON转义与磁盘目录单测 脚本语法导入跳过验证通过 Linux新服务未编译运行 原始特殊名称仅在序列化单测中验证 不是已经保存的上传名称

以下为第17单元及更早的历史说明 不要求每日重跑

第17单元新增download_headers.h统一GET HEAD成功响应头 Content-Disposition为attachment 建议名为合法ID加.bin 不改变对象磁盘名称 查询name仍忽略 类型仍为application/octet-stream
本机download_headers_tests编译运行通过 脚本语法及Windows跳过77验证通过 无Linux新服务编译或运行证据
同步src/download_headers.h src/file_download.h tests/day17_download_headers.cpp tests/day17_download_headers.py与CMakeLists.txt 保留第16单元脚本供新脚本导入
复用build配置构建 只运行 ctest --test-dir build -R '^download_headers_integration_tests$' -V 应实际通过一项 输出PASS: download header integration finished, 6 groups Linux无SKIP
第17单元脚本自动启动停止独立临时目录内的服务 8080需空闲 覆盖GET HEAD头一致 安全建议名 错误响应不附下载名以及HEAD错误无正文

以下第16单元及更早内容为历史说明 不要求每日重跑

GET与HEAD /api/files/{id}/content复用ID校验 只发送普通文件 GET使用16KiB缓冲区 HEAD只发送头部 声明长度来自已打开文件的fstat
非法ID400 文件缺失或最终符号链接及非普通文件404 POST405 其他未实现方法501 非空请求体400 发送头后读取失败关闭连接 不追加另一份响应
下载不缓存整个文件 上传仍最多64KiB 未实现Range 原始文件名元数据或并发 第17单元已经新增ID加.bin建议名
第16单元配置构建后只运行 ctest --test-dir build -R '^download_integration_tests$' -V
测试需8080空闲 自动启动停止独立服务 使用临时目录 应实际通过一项 输出PASS: download integration finished, 8 groups Linux不应SKIP
同步src/main.cpp src/file_download.h CMakeLists.txt与tests/day16_download_integration.py Python实现不用阅读 Windows参考不会自动同步虚拟机
本机仅脚本语法与响应辅助验证通过 无可用Linux执行环境 尚未编译或运行Linux新服务 第15单元用户结果未反馈 第14单元原通过记录保留

以下第14和第15单元内容为历史验证说明 不要求每日重跑

第15单元新增object_path.h集中校验upload-加六位ASCII字母数字的ID 发布路径统一由make_object_path构造 非法输入不改变输出 查询参数仍忽略
复用build配置构建后运行 ctest --test-dir build -R '^object_path_tests$' -V 应实际运行一项并输出 PASS: object path isolation, 17 invalid IDs rejected
本机Windows纯C++测试通过 Linux发布和真实上传待用户反馈 第14单元已经通过 无需每日重复

本目录是 Windows 工作区内的参考源码 不会自动同步到 Linux 虚拟机
程序监听 127.0.0.1:8080 串行处理GET与HEAD /health和临时POST /echo 每连接只处理一次请求再关闭
health只接受零长度正文 echo原样返回定长正文 请求上限仍64KiB
POST /dev/upload将原始正文分段写临时文件并在本次处理作用域退出时清理 上传正文不进入parser.body
POST /api/files使用同样的分段写入 完整接收且关闭成功后通过link发布到data/objects 返回201 X-File-Id及固定JSON正文created
临时目录与对象目录必须在支持硬链接的同一文件系统 目标已有文件时拒绝覆盖 正式对象在响应后保留 当前已有下载 仍无原始名称元数据
无Content-Length时正文长度为零 拒绝重复长度 逗号列表 非十进制 溢出 超限 Transfer-Encoding与Expect
Host要求单个非空 仍无完整Host语法校验 无超时 无并发
格式错误400 未知路径404 已知路径方法不允许405并带Allow 正文超限413 Expect不支持417 传输编码与未实现方法501
错误使用固定JSON正文 HEAD只发送响应头 路径按问号分开查询部分 当前不解释查询参数 不做解码或归一化

教程在对话完整发送 本地 docs/lessons/day01.md 至 day25.md 用于助手保留上下文 固定复盘日取消 每日仅验证核心功能

在 Linux 工程目录 `~/projects/netdisk` 执行

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j2
ctest --test-dir build -R '^upload_integration_tests$' -V
```

若此前一直使用build-debug 则复用build-debug 将上述build统一替换 不另建目录

本次测试需要Python3.8或以上 只使用标准库 CMake负责查找解释器
运行前让8080空闲 若已有手动启动的服务器 在其原终端停止 脚本会启动自己的服务且自动结束
CTest应实际运行upload_integration_tests一项并通过 最后输出PASS: upload integration finished, 6 checks, 4 published files Linux不应显示SKIP
覆盖空文件 UTF8文本 64KiB二进制 分段接收时不提前发布 提前EOF清理以及之后health成功
测试使用独立临时目录 不使用工程data 结束时停止自己启动的进程再清理临时目录 不需要额外手动curl
本机Windows已检查脚本语法 响应读取辅助逻辑和退出77跳过行为 用户已明确反馈Linux上传端到端测试百分百通过
后续Python测试脚本由助手维护 对话只给命令 范围与成功条件 不要求阅读实现 C++功能源码与必要CMake仍完整讲解
没有fsync与启动恢复 不承诺断电持久化 已发布后响应丢失不删除对象 未发布的临时文件在正常析构时清理

实际进度见工作区根目录的 `LEARNING_PROGRESS.md` 第14单元集成验证已按用户反馈记录通过
