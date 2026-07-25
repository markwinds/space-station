// 在 Web 插件管理中填写账号、密码和可选代理，然后启用插件。
// 该文件独立于 Space Station 主程序编译，OA/PSH 登录流程只存在于插件源码中。
const CONFIG = {
  username: "zhoubangtong",
  password: "Freeman111*",
  // 支持 http://、https://、socks4://、socks5:// 和 socks5h://。
  proxy: "http://10.117.58.15:10808",
  verifyTls: false,
  ssoCacheMs: 60 * 60 * 1000,
};

const SSO_URL =
  "https://sso.hikvision.com/login?service=http%3A%2F%2Fpsh.hikvision.com.cn%2Fquery-tools%2Fpsh-shell";
const TOKEN_URL = "https://psh.hikvision.com.cn/api/web/password/shell";
const OA_PUBLIC_KEY = `-----BEGIN PUBLIC KEY-----
MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQCqc90wxTr7Biug8nciEMrSygRg
Yvo31+shw+gxp0LqVbVCeGklV/Mwx7HXeGbbK9HHHAORE6lDgIb7tgCPZHygA2vJ
tBhlhIy2IgHDpsp8Hv0UjwCML8/6KvChE3YChPffs6UUUgwJOQiDWOe/i2dCT4J2
p/AR1kFcd2UFEGaW1QIDAQAB
-----END PUBLIC KEY-----`;

const sessions = new Map();
let cachedSso = null;

class CookieJar {
  constructor(values) {
    this.values = values ? { ...values } : {};
  }

  absorb(response) {
    for (const line of response.setCookies || []) {
      const pair = line.split(";", 1)[0];
      const separator = pair.indexOf("=");
      if (separator <= 0) continue;
      const name = pair.slice(0, separator).trim();
      const value = pair.slice(separator + 1).trim();
      if (!name) continue;
      if (value) this.values[name] = value;
      else delete this.values[name];
    }
  }

  has(name) {
    return Object.prototype.hasOwnProperty.call(this.values, name);
  }

  header() {
    return Object.keys(this.values)
      .map((name) => `${name}=${this.values[name]}`)
      .join("; ");
  }
}

function assertConfigured() {
  if (!CONFIG.username || !CONFIG.password) {
    throw new Error("请先在 PSH 插件源码顶部填写 OA 用户名和密码");
  }
  if (!/^[a-zA-Z0-9.]+$/.test(CONFIG.username)) {
    throw new Error("OA 用户名必须使用英文名");
  }
}

function resolveUrl(base, location) {
  if (/^https?:\/\//i.test(location)) return location;
  const origin = base.match(/^(https?):\/\/([^/]+)/i);
  if (!origin) throw new Error("无法解析重定向基准地址");
  if (location.startsWith("//")) return `${origin[1]}:${location}`;
  if (location.startsWith("/")) return `${origin[1]}://${origin[2]}${location}`;
  const cleanBase = base.split("#", 1)[0].split("?", 1)[0];
  return `${cleanBase.slice(0, cleanBase.lastIndexOf("/") + 1)}${location}`;
}

function formEncode(values) {
  return Object.keys(values)
    .map((name) => `${encodeURIComponent(name)}=${encodeURIComponent(values[name])}`)
    .join("&");
}

function localDate() {
  const now = new Date();
  const year = String(now.getFullYear());
  const month = String(now.getMonth() + 1).padStart(2, "0");
  const day = String(now.getDate()).padStart(2, "0");
  return `${year}-${month}-${day}`;
}

async function requestWithRedirects(options, jar, stopBeforeRedirect) {
  let url = options.url;
  let method = options.method || "GET";
  let body = options.body || "";
  let headers = { ...(options.headers || {}) };
  const allSetCookies = [];

  for (let redirectCount = 0; redirectCount <= 10; redirectCount += 1) {
    const cookie = jar.header();
    const requestHeaders = { ...headers };
    if (cookie) requestHeaders.Cookie = cookie;
    const response = await space.http.request({
      url,
      method,
      body,
      headers: requestHeaders,
      proxy: CONFIG.proxy,
      verifyTls: CONFIG.verifyTls,
      timeoutMs: 15000,
    });
    jar.absorb(response);
    allSetCookies.push(...(response.setCookies || []));

    const location = response.headers.location;
    if (response.status < 300 || response.status >= 400 || !location) {
      response.allSetCookies = allSetCookies;
      return response;
    }
    const nextUrl = resolveUrl(url, location);
    if (stopBeforeRedirect && stopBeforeRedirect(nextUrl, response)) {
      response.allSetCookies = allSetCookies;
      return response;
    }
    if (response.status === 303 || ((response.status === 301 || response.status === 302) && method === "POST")) {
      method = "GET";
      body = "";
      headers = { ...headers };
      delete headers["Content-Type"];
      delete headers["content-type"];
    }
    url = nextUrl;
  }
  throw new Error("SSO 重定向次数超过限制");
}

function hiddenValue(html, id, name) {
  const pattern = new RegExp(
    `<input[^>]*(?:id=["']${id}["']|name=["']${name}["'])[^>]*value=["']([^"']*)["']`,
    "i",
  );
  const reversePattern = new RegExp(
    `<input[^>]*value=["']([^"']*)["'][^>]*(?:id=["']${id}["']|name=["']${name}["'])`,
    "i",
  );
  const match = html.match(pattern) || html.match(reversePattern);
  return match ? match[1] : "";
}

async function loadLoginPage() {
  const maximumAttempts = 3;
  let lastError = null;

  for (let attempt = 1; attempt <= maximumAttempts; attempt += 1) {
    const jar = new CookieJar();
    try {
      const response = await requestWithRedirects(
        {
          url: SSO_URL,
          headers: {
            Accept: "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
            "User-Agent": "Mozilla/5.0",
          },
        },
        jar,
      );
      if (response.status < 500 || response.status >= 600) return { jar, response };
      lastError = new Error(`SSO 登录页返回 HTTP ${response.status}`);
    } catch (error) {
      lastError = error;
    }

    if (attempt < maximumAttempts) {
      space.log("warn", `SSO 登录页请求失败，正在重试 (${attempt}/${maximumAttempts})`);
    }
  }

  throw new Error(`SSO 登录页连续 ${maximumAttempts} 次请求失败: ${String(lastError)}`);
}

async function loginSso() {
  assertConfigured();
  if (cachedSso && cachedSso.expiresAt > Date.now()) {
    return new CookieJar(cachedSso.cookies);
  }

  const loaded = await loadLoginPage();
  const jar = loaded.jar;
  const loginPage = loaded.response;
  if (loginPage.status >= 400) throw new Error(`SSO 登录页返回 HTTP ${loginPage.status}`);
  const salt = hiddenValue(loginPage.body, "salt", "salt");
  const lt = hiddenValue(loginPage.body, "lt", "lt");
  if (!salt || !lt || !jar.has("SESSION")) throw new Error("SSO 页面格式不匹配或缺少 SESSION");

  const encryptedPassword = space.crypto.rsaEncryptPkcs1v15({
    publicKey: OA_PUBLIC_KEY,
    data: salt + CONFIG.password,
  });
  const body = formEncode({
    username: CONFIG.username,
    localDate: localDate(),
    password: encryptedPassword,
    lt,
    execution: "e1s1",
    _eventId: "submit",
    ver: "2.0",
  });
  let receivedTicket = false;
  const loginResponse = await requestWithRedirects(
    {
      url: SSO_URL,
      method: "POST",
      body,
      headers: {
        "Content-Type": "application/x-www-form-urlencoded",
        "User-Agent": "Mozilla/5.0",
      },
    },
    jar,
    (nextUrl) => {
      if (/[?&]ticket=/.test(nextUrl)) {
        receivedTicket = true;
        return false;
      }
      return receivedTicket;
    },
  );
  if (!receivedTicket) throw new Error("OA 认证失败，请检查用户名、密码或账号状态");
  if (loginResponse.status >= 400) throw new Error(`PSH 登录跳转返回 HTTP ${loginResponse.status}`);

  cachedSso = {
    cookies: { ...jar.values },
    expiresAt: Date.now() + CONFIG.ssoCacheMs,
  };
  return jar;
}

async function getPshToken(challenge) {
  const jar = await loginSso();
  const response = await requestWithRedirects(
    {
      url: TOKEN_URL,
      method: "POST",
      body: JSON.stringify({ customMsg: challenge, source: challenge }),
      headers: {
        "Content-Type": "application/json",
        "User-Agent": "Mozilla/5.0",
      },
    },
    jar,
  );
  if (!response.ok) throw new Error(`PSH 服务返回 HTTP ${response.status}`);
  let payload;
  try {
    payload = JSON.parse(response.body);
  } catch (_) {
    throw new Error("PSH 服务返回了无效 JSON");
  }
  const token = typeof payload.data === "string" ? payload.data.replace(/\r?\n/g, "") : "";
  if (token.length !== 172) throw new Error("PSH 服务返回的口令格式无效");
  return token;
}

async function onTerminalData(event) {
  const state = sessions.get(event.sessionId) || {
    buffered: "",
    inFlight: false,
    lastChallenge: "",
    lastAttemptAt: 0,
  };
  state.buffered = (state.buffered + event.data).slice(-512);
  // 一些串口设备会在提示符后补 NUL，匹配前去掉这些填充字节。
  const matchBuffer = state.buffered.replace(/\0+$/, "");
  const match = matchBuffer.match(/(?:^|\n)#?\s*([^\r\n]{20}).?\r?\nPassword:?\s*$/);
  if (!match || state.inFlight) {
    sessions.set(event.sessionId, state);
    return;
  }

  const challenge = match[1];
  const now = Date.now();
  if (challenge === state.lastChallenge && now - state.lastAttemptAt < 3000) {
    sessions.set(event.sessionId, state);
    return;
  }
  state.inFlight = true;
  state.lastChallenge = challenge;
  state.lastAttemptAt = now;
  sessions.set(event.sessionId, state);

  try {
    const token = await getPshToken(challenge);
    space.terminal.write(`${token}\n`);
  } catch (error) {
    // 不向终端打印认证细节，避免干扰设备输入；错误只进入后端日志。
    space.log("error", `PSH 自动认证失败: ${String(error)}`);
    cachedSso = null;
  } finally {
    state.inFlight = false;
    sessions.set(event.sessionId, state);
  }
}

function onTerminalClose(event) {
  sessions.delete(event.sessionId);
}
