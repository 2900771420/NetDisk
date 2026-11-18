# NetDisk 测试资产基线与标准

更新日期 2026-11-18

本文固定第33A完成后的现有测试资产 并规定后续测试如何持续扩展
本次只建立规则 不修改任何现有测试文件或CTest注册

## 1 现有冻结基线

现有22个C++测试文件 20个Python测试脚本和39个CTest名称保持原路径
20个Python脚本中17个已注册CTest 3个是未注册的历史脚本
原名称和原业务语义 未经用户明确批准不得修改 移动 重命名 合并或删除

### 1.1 C++测试

| CTest名称 | 文件 |
| --- | --- |
| io_tests | tests/unit/io/posix_io_tests.cpp |
| request_line_tests | tests/unit/http/request_line_tests.cpp |
| request_headers_tests | tests/unit/http/request_headers_tests.cpp |
| request_body_tests | tests/unit/http/request_parser_tests.cpp |
| http_response_tests | tests/unit/http/response_tests.cpp |
| stream_upload_tests | tests/unit/storage/temp_upload_tests.cpp |
| publish_tests | tests/unit/storage/publication_tests.cpp |
| object_path_tests | tests/unit/storage/object_path_tests.cpp |
| download_headers_tests | tests/unit/http/download_response_tests.cpp |
| file_catalog_tests | tests/unit/server/file_catalog_tests.cpp |
| file_mutation_tests | tests/unit/server/file_mutation_tests.cpp |
| sqlite_lifecycle_tests | tests/unit/metadata/sqlite_lifecycle_tests.cpp |
| file_pagination_tests | tests/unit/server/file_pagination_tests.cpp |
| publish_state_tests | tests/unit/server/publish_visibility_tests.cpp |
| publishing_recovery_tests | tests/unit/maintenance/publishing_recovery_tests.cpp |
| deletion_recovery_tests | tests/unit/maintenance/deletion_recovery_tests.cpp |
| storage_audit_tests | tests/unit/maintenance/storage_audit_tests.cpp |
| sha256_tests | tests/unit/crypto/sha256_tests.cpp |
| digest_backfill_tests | tests/unit/maintenance/digest_backfill_tests.cpp |
| range_tests | tests/unit/http/range_tests.cpp |
| if_range_tests | tests/unit/http/if_range_tests.cpp |
| resume_download_tests | tests/unit/client/resume_download_tests.cpp |

### 1.2 Python集成测试

| CTest名称 | 文件 |
| --- | --- |
| upload_integration_tests | tests/integration/day14_upload_integration.py |
| download_integration_tests | tests/integration/day16_download_integration.py |
| download_headers_integration_tests | tests/integration/day17_download_headers.py |
| file_catalog_integration_tests | tests/integration/day18_file_catalog.py |
| file_mutation_integration_tests | tests/integration/day19_file_mutation.py |
| failure_recovery_integration_tests | tests/integration/day20_failure_recovery.py |
| first_release_integration_tests | tests/integration/day21_first_release.py |
| metadata_persistence_integration_tests | tests/integration/day23_metadata_persistence.py |
| publish_state_integration_tests | tests/integration/day25_publish_state.py |
| publishing_recovery_integration_tests | tests/integration/day26_publishing_recovery.py |
| deletion_recovery_integration_tests | tests/integration/day27_deletion_recovery.py |
| storage_audit_integration_tests | tests/integration/day28_storage_audit.py |
| sha256_integration_tests | tests/integration/day29_sha256.py |
| digest_backfill_integration_tests | tests/integration/day30_digest_backfill.py |
| range_integration_tests | tests/integration/day31_range.py |
| if_range_integration_tests | tests/integration/day32_if_range.py |
| resume_download_integration_tests | tests/integration/day33_resume_download.py |

### 1.3 未注册的历史Python测试

| 文件 | 当前状态 |
| --- | --- |
| tests/integration/day04_echo.py | 保留但未注册CTest |
| tests/integration/day05_http.py | 保留但未注册CTest |
| tests/integration/day06_disconnect.py | 保留但未注册CTest |

现有dayXX文件名仅作为历史兼容例外保留 新文件禁止继续使用这种命名

## 2 未来测试结构

```text
tests/
├── unit/
│   └── <module>/
│       └── <stable_capability>_tests.cpp
├── integration/
│   └── <stable_workflow>_integration.py
└── support/
    └── <shared_test_responsibility>.*
```

- 单元测试围绕一个稳定模块能力组织
- 集成测试围绕一个完整外部工作流组织
- support只保存多个套件共同需要的夹具和进程控制 不注册为独立CTest
- 文件名 CTest名和职责均不得包含DAY编号 日期或教程编号

## 3 复用与新增判断

后续功能先回答以下问题

1. 新行为属于哪个稳定模块或工作流
2. 是否已有未来标准套件覆盖同一职责
3. 新场景能否共享相同依赖 进程生命周期 临时目录和超时模型
4. 新场景是否会让一个套件跨越两个无共同变化原因的职责

前三项允许且第四项为否时 扩展同一个未来稳定套件
只有依赖 生命周期 隔离方式或职责边界确实不同才建立新套件
学习单元编号和实现日期永远不是新增套件的理由

## 4 C++单元测试标准

- 文件路径为tests/unit/<module>/<capability>_tests.cpp
- CTest名称为<capability>_tests
- 只链接被测能力需要的最小CMake模块目标
- 从公开接口验证行为 不直接包含生产cpp文件
- 一个可执行程序可覆盖正常路径 边界 失败和资源释放多个相关场景
- 测试必须独立于执行顺序 不使用工程真实data目录

## 5 Python集成测试标准

- 文件路径为tests/integration/<workflow>_integration.py
- CTest名称为<workflow>_integration_tests
- 通过netdisk或netdisk_download及公开HTTP行为验证完整工作流
- 使用独立临时目录和独立测试数据
- 明确超时 自动停止自己启动的进程并清理自己的资源
- 不依赖其他测试先运行 不修改工程真实data目录
- 公共启动 停止 端口等待和断言逻辑进入tests/support复用

## 6 教程中的测试影响

每个后续教程必须明确说明

- 复用的稳定测试套件
- 新增的场景及其证明目标
- 是否新增CTest目标
- 如果新增 为什么现有未来套件无法承载
- 用户实际需要运行的最少测试命令和成功条件

没有新增测试目标时直接写明复用既有稳定套件
不得为了让每天看起来有新产物而创建dayXX测试文件

## 7 冻结基线后新增套件

| CTest名称 | 文件 | 稳定职责 |
| --- | --- | --- |
| download_timeout_integration_tests | tests/integration/download_timeout_integration.py | 可配置下载端点 网络无进展超时 临时产物恢复与原子公开 |

该套件不修改第33A冻结的39个CTest 新总数为40
它使用两个临时回环端口和可控假服务端 不读写工程data目录
第35单元复用该套件 增加旧最终文件在超时后保持不变 临时前缀可恢复 完成后最终路径包含完整新版本且临时名称消失的场景 不新增文件或CTest名称
