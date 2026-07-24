# 差分传输配置

`transfer-server.example.json` 和 `transfer-client.example.json` 可复制后通过 `--config` 使用。
配置文件中的相对路径以配置文件所在目录为基准；命令行参数会覆盖配置文件中的值。

```sh
space-transfer-server --config transfer-server.json
space-transfer-client --config transfer-client.json /path/to/another-file.bin
```

服务端 TLS 监听仅允许 TLS 1.3，并要求客户端证书。普通端口默认关闭，不提供传输加密或客户端身份认证。

终端 JavaScript 插件的目录结构、清单字段和运行时 API 见
[`terminal-plugins.md`](terminal-plugins.md)。插件也可以在 Web 端“终端插件”工具中管理。
