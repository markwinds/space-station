# 终端 JS 插件

终端插件从应用数据目录下的 `plugins` 目录加载。实际数据目录可通过
`GET /api/config` 的 `dataPath` 查看。每个插件占用一个子目录：

```text
plugins/
  my-plugin/
    manifest.json
    index.js
```

`manifest.json` 支持以下字段：

- `id`：插件唯一标识，只能包含字母、数字、点、下划线和短横线。
- `name`：显示名称。
- `enabled`：只有显式设置为 `true` 才会加载。
- `entry`：入口脚本，默认 `index.js`，不能离开插件目录。
- `transports`：生效终端类型，支持 `serial`、`ssh` 和 `browser-serial`。
- `targets`：目标白名单，`*` 表示全部；服务器串口使用设备路径，SSH 使用主机 ID，浏览器共享串口使用共享 ID。
- `allowedHosts`：HTTP 主机白名单，支持精确主机及 `*.example.com` 形式。
- `memoryLimitMiB`：QuickJS 内存上限，范围 1–64 MiB。
- `executionTimeoutMs`：单次 JS 执行上限，范围 5–1000 ms。
- `maxConcurrentRequests`：并发 HTTP 请求上限，范围 1–16。
- `maxResponseBytes`：单个 HTTP 响应上限，最大 4 MiB。

脚本必须定义全局 `onTerminalData(event)` 函数，也可以定义
`onTerminalOpen(event)` 和 `onTerminalClose(event)`。事件字段包括：

- `sessionId`：本次后端终端会话标识。
- `transport`：终端类型。
- `target`：服务器串口路径。
- `data`：本次读取的文本数据。
- `bytes`：同一数据的 `Uint8Array` 表示。

后端提供受限的 `space` API：

- `await space.http.request(options)`：异步请求，返回
  `{ status, ok, body, url, headers, headerValues, setCookies }`。支持
  `url`、`method`、`headers`、`body`、`timeoutMs`、`proxy` 和
  `verifyTls`。`proxy` 可使用 HTTP、HTTPS、SOCKS4、SOCKS5 或
  SOCKS5H 地址；重定向不会自动跟随，插件可读取 `headers.location`
  后再次请求，让每一步地址都经过 `allowedHosts` 校验。
- `space.terminal.write(data)`：向触发事件所属的终端写入字符串或
  `ArrayBuffer`。
- `space.crypto.randomBytes(size)`：生成安全随机字节。
- `space.crypto.rsaEncryptPkcs1v15({ publicKey, data })`：使用 PEM RSA
  公钥执行 PKCS#1 v1.5 加密，返回 Base64 字符串。
- `space.log(level, message)`：写入带插件标识的后端日志。

QuickJS 不开放文件系统、命令执行和原生网络能力。HTTP 由 libcurl 后端
执行，并受到 `allowedHosts`、超时、请求体、响应体和并发数限制。

插件可通过 Web 端“终端插件”工具管理，保存、启停和删除后会自动热重载；也可以直接编辑文件后调用
`POST /api/tools/terminal/plugins/reload`。可以复制
`config/plugins/http-trigger-example` 到数据目录，修改 URL 和白名单后，
将 `enabled` 改为 `true`。`GET /api/tools/terminal/plugins` 可查看加载结果。
`config/plugins/psh-auth` 是独立保存、不会编译进主程序的 PSH 认证插件；
可以在 Web 管理中以它的 manifest 和源码新建插件，填写配置后再启用。

SSH 和带回显的串口会把用户输入再次出现在输出流里；插件写回数据后，Shell 的错误信息也可能再次包含原标记。
因此标记插件应按 `sessionId` 保存解析缓冲区，并在标记中携带请求 ID、对已经处理的 ID 去重；时间窗口只能
降低重复概率，不能提供严格的一次性语义。Web 管理界面的新建模板使用 `@@PING:<请求ID>@@`，同时兼容每个
会话只处理一次的旧格式 `@@PING@@`。`http-trigger-example` 也包含基础防回显逻辑。

浏览器本地串口只有在开启共享通道后，数据才会经过后端并触发 `browser-serial` 插件；未共享的 Web Serial 连接完全位于浏览器中，后端无法观察。
