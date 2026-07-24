const sessions = new Map();
const replayWindowMs = 1500;

async function onTerminalData(event) {
  const state = sessions.get(event.sessionId) || { buffered: "", lastQuery: "", lastTriggerAt: 0 };
  state.buffered += event.data;
  const prefix = "@@HTTP:";
  const suffix = "@@";

  while (true) {
    const start = state.buffered.indexOf(prefix);
    if (start < 0) {
      state.buffered = state.buffered.slice(-prefix.length);
      break;
    }
    const end = state.buffered.indexOf(suffix, start + prefix.length);
    if (end < 0) {
      state.buffered = state.buffered.slice(start);
      break;
    }

    const query = state.buffered.slice(start + prefix.length, end);
    state.buffered = state.buffered.slice(end + suffix.length);
    const now = Date.now();
    if (query === state.lastQuery && now - state.lastTriggerAt < replayWindowMs) continue;
    state.lastQuery = query;
    state.lastTriggerAt = now;
    // 在等待网络前先保存状态，避免终端回显趁请求未完成时再次触发。
    sessions.set(event.sessionId, state);
    try {
      const response = await space.http.request({
        url: `https://api.example.com/lookup?q=${encodeURIComponent(query)}`,
        method: "GET",
        timeoutMs: 5000,
      });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      space.terminal.write(`${response.body}\r\n`);
    } catch (error) {
      space.log("error", `请求失败: ${String(error)}`);
      space.terminal.write("ERROR\r\n");
    }
  }
  sessions.set(event.sessionId, state);
}

function onTerminalClose(event) {
  sessions.delete(event.sessionId);
}
