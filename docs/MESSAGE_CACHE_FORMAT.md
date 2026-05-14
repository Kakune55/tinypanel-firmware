# 消息缓存格式

固件将消息缓存以二进制记录的形式存储在 SD 卡上，以避免将整个大型 JSON 文件加载到内存堆中。

## 文件

```text
/tinypanel/cache/messages/index.bin
/tinypanel/cache/messages/msg_<id>.bin
```

## 索引文件

`index.bin` 用于存储消息的排序信息。

```text
magic      4 bytes   "TPMI"
version    u8        当前为 1
count      u16-le    消息 ID 数量
ids        u32-le[]  按显示顺序排列的消息 ID
```

## 消息记录

每个 `msg_<id>.bin` 文件存储一条消息。

```text
magic           4 bytes   "TPMG"
version         u8        当前为 1
id              u32-le
created_at_len  u16-le
author_len      u16-le
channel_len     u16-le
body_len        u32-le
created_at      bytes
author          bytes
channel         bytes
body            bytes
```

字符串以原始 UTF-8 字节序列存储，不包含空终止符。

## 注意事项

- 文件通过临时文件实现原子写入。
- 加载时每次仅读取一条消息记录。
- 读取缓冲区优先使用 PSRAM，不足时回退至内部 RAM。
- 消息正文目前按原始文本存储，其中包含 `[[BMP:...]]` 位图标签。
- 未来的格式版本可能会将位图标签解析为紧凑的位图数据块。

## 本地操作

- 删除单条消息时会重写索引文件及剩余的消息记录。
- 清空消息时会写入一个空的索引文件。
- 执行删除或清空操作后，旧的 `msg_<id>.bin` 文件可能仍残留在 SD 卡上，但若未收录于 `index.bin` 中，则会被系统忽略。