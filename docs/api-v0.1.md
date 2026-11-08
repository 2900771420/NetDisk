# NetDisk HTTP API v0.1

本文件记录第24单元当前功能基线 不表示完整实现HTTP/1.1 不表示公网安全评估通过

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

上传先写入`data/uploads`临时文件 完整接收并关闭后通过硬链接发布到`data/objects` 随后使用预编译语句插入`ready`元数据 插入失败时尝试补偿删除刚发布的对象

对象和SQLite不属于同一个原子事务 元数据提交成功后即使客户端未收到响应也不回滚 第25单元继续完善跨资源发布状态

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

当前文件JSON仍只返回`id name size` 创建时间暂未公开到HTTP接口 列表只显示`ready`记录

列表接受`limit`和`cursor`两个查询参数 `limit`默认为100且范围为1至100 游标是服务端返回的不透明字符串 客户端应原样传回 不自行修改

结果按`created_at`和`id`升序排列 有后续记录时响应增加`next_cursor` 没有后续记录时省略该字段 未知参数 重复参数 非十进制limit 非法或溢出cursor返回400 当前不执行百分号解码

键集分页能避免删除已浏览记录导致后续页行号前移 但不是数据库快照 分页期间新插入且排序键早于或等于当前游标的记录不会出现在这次遍历中

名称按UTF-8字节计1至255字节 拒绝纯空格 点 双点 ASCII控制字符 DEL 斜杠和反斜杠

## 下载

下载类型固定为`application/octet-stream` 建议文件名固定为`ID.bin` 不使用显示名称构造响应头

GET以16KiB缓冲区读取和发送 HEAD发送与GET相同的Content-Length Content-Type和Content-Disposition但不发送正文

当前不支持Range ETag If-Range 下载续传和内容摘要

## 删除

DELETE先移除对象文件再删除`ready`元数据 成功返回204 无Content-Length Transfer-Encoding或正文 成功后新的列表 详情和下载请求不再看到对象 重复删除返回404

当前没有回收站 文件删除与数据库删除还不是原子事务 删除成功即移除对象和元数据 不保证断电持久化

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
- SQLite元数据与键集分页已实现 仍无摘要和HTTP创建时间字段
- 无用户 权限 文件夹 分享 配额 回收站和版本历史
- 无Range 上传续传 并行上传和秒传
- 只监听回环地址 没有TLS 不用于公网
