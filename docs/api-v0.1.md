# NetDisk HTTP API v0.1

本文件记录第27单元当前功能基线 不表示完整实现HTTP/1.1 不表示公网安全评估通过

服务地址 `http://127.0.0.1:8080` 每个连接只处理一个请求 响应后关闭连接

## 接口

| 方法 | 路径 | 成功 | 正文或作用 |
| --- | --- | --- | --- |
| GET HEAD | `/health` | 200 | GET返回`OK\n` HEAD只返回头部 |
| POST | `/api/files` | 201 | 原始二进制正文上传 返回固定JSON与`X-File-Id` |
| GET HEAD | `/api/files` | 200 | 返回分页列表和可选`next_cursor` |
| GET HEAD | `/api/files/{id}` | 200 | 返回`id name size`详情 |
| PATCH | `/api/files/{id}` | 200 | 正文必须为仅含字符串`name`的JSON |
| DELETE | `/api/files/{id}` | 204 | 删除正式对象 无正文和Content-Length |
| GET HEAD | `/api/files/{id}/content` | 200 | GET流式下载 HEAD只返回相同头部 |

## 上传

请求正文是原始文件字节 必须使用Content-Length 当前最大65536字节 不支持Transfer-Encoding和Expect

成功响应正文固定为`{"status":"created"}` 文件ID从`X-File-Id`响应头获取 ID格式为`upload-`加六位ASCII字母数字

上传先写入`data/uploads`临时文件 完整接收并关闭后取得稳定ID 第一个SQLite事务插入`publishing`元数据并提交 随后通过硬链接把对象发布到`data/objects` 第二个SQLite事务只把对应`publishing`记录切换为`ready`

对象发布失败时尝试删除`publishing`记录 切换`ready`失败时保留对象与`publishing`记录供后续恢复 不删除可能已经成功提交的对象 只有第二个事务成功后才返回201

对象和SQLite不属于同一个原子事务 进程可能留下不同磁盘阶段的`publishing`记录 这些状态不会被公开读取 服务在下一次启动监听前执行恢复 元数据提交为`ready`后即使客户端未收到响应也不回滚

## 启动恢复

服务按`created_at id`顺序读取全部`publishing`记录 同时检查`data/uploads/{id}`和`data/objects/{id}`

- 只有临时普通文件且大小匹配时创建正式硬链接 删除临时名并切换`ready`
- 只有正式普通文件且大小匹配时切换`ready`
- 两个名称均存在且为同一文件的硬链接时删除临时名并切换`ready`
- 两个名称均缺失时删除`publishing`记录
- 路径类型错误 大小不符 或两个名称不是同一文件时保留证据并终止启动

每条状态变化使用独立SQLite事务 已完成记录不会在重试时重复处理 恢复过程中途失败后再次启动可继续 未实现多服务进程同时操作同一数据目录的协调

发布恢复完成后 服务按相同稳定顺序读取全部`deleting`记录

- 正式对象是大小匹配的普通文件时删除对象 再事务性删除`deleting`记录
- 正式对象已经缺失时直接事务性删除`deleting`记录
- 对象不是普通文件或大小与元数据不符时保留对象与记录并终止启动

删除恢复不扫描无元数据对象 也不删除任何`ready`对象

## 元数据

列表和详情字段如下

```json
{
  "id": "upload-Abc123",
  "name": "upload-Abc123.bin",
  "size": 3
}
```

文件ID 显示名称 大小 状态和创建时间保存在`data/netdisk.db` 显示名称修改通过绑定参数的预编译语句写入SQLite 服务重启后仍保留

当前文件JSON仍只返回`id name size` 创建时间暂未公开到HTTP接口 列表 详情和下载都只接受`ready`记录

列表接受`limit`和`cursor`两个查询参数 `limit`默认为100且范围为1至100 游标是服务端返回的不透明字符串 客户端应原样传回 不自行修改

结果按`created_at`和`id`升序排列 有后续记录时响应增加`next_cursor` 没有后续记录时省略该字段 未知参数 重复参数 非十进制limit 非法或溢出cursor返回400 当前不执行百分号解码

键集分页能避免删除已浏览记录导致后续页行号前移 但不是数据库快照 分页期间新插入且排序键早于或等于当前游标的记录不会出现在这次遍历中

名称按UTF-8字节计1至255字节 拒绝纯空格 点 双点 ASCII控制字符 DEL 斜杠和反斜杠

## 下载

下载类型固定为`application/octet-stream` 建议文件名固定为`ID.bin` 不使用显示名称构造响应头

GET先查询`ready`元数据 再以16KiB缓冲区读取和发送 未登记对象和`publishing`对象即使已存在于磁盘也返回404 文件实际大小与元数据大小不一致返回500 HEAD发送与GET相同的Content-Length Content-Type和Content-Disposition但不发送正文

当前不支持Range ETag If-Range 下载续传和内容摘要

## 删除

DELETE先确认正式对象是普通文件且大小匹配 第一个SQLite事务把`ready`切换为`deleting` 然后移除对象文件 第二个SQLite事务删除`deleting`元数据 成功返回204 无Content-Length Transfer-Encoding或正文

`deleting`不会被列表 详情或下载读取 因此第一个事务提交后新的请求立即看到404 重复删除也返回404 进程在后两步之间退出时 服务下次启动继续完成删除

当前没有回收站 文件删除与数据库事务仍不是跨资源原子操作 `deleting`只提供进程重启恢复 没有文件和目录`fsync` 不保证断电持久化

## 主要错误

| 状态 | 含义 |
| --- | --- |
| 400 | 请求格式 正文策略 ID或名称非法 |
| 404 | 路由或合法ID对应文件不存在 |
| 405 | 方法已实现但不允许用于该资源 响应包含Allow |
| 413 | 请求正文超过65536字节 |
| 417 | 不支持Expect |
| 500 | 文件系统或内部处理失败 |
| 501 | 方法或Transfer-Encoding未实现 |

错误正文是固定JSON 不返回内部磁盘路径

## 第一版限制

- 串行阻塞 一次只处理一个客户端
- 无读写超时 连接数限制和优雅退出
- SQLite元数据 发布与删除状态事务 启动恢复与键集分页已实现 仍无摘要和HTTP创建时间字段
- 无用户 权限 文件夹 分享 配额 回收站和版本历史
- 无Range 上传续传 并行上传和秒传
- 只监听回环地址 没有TLS 不用于公网
