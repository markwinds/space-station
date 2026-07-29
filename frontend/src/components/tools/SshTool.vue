<template>
  <div class="ssh-app" :class="{ 'ssh-app--terminal-open': activeTabId }" :style="mobileViewportStyle">
    <aside class="ssh-sidebar">
      <div class="ssh-brand-row">
        <router-link class="ssh-home" to="/" aria-label="返回首页">SS</router-link>
        <div>
          <strong>SSH 终端</strong>
          <small>{{ hosts.length }} 台主机</small>
        </div>
        <n-button type="primary" size="small" aria-label="添加主机" @click="openHostEditor()">
          <template #icon><n-icon><AddOutline /></n-icon></template>
          添加
        </n-button>
      </div>

      <n-input v-model:value="keyword" clearable size="small" placeholder="搜索主机" />

      <div class="ssh-config-actions">
        <n-button secondary size="small" @click="openPortForwards">端口转发</n-button>
        <n-button secondary size="small" @click="openTerminalSettings">终端设置</n-button>
        <n-button secondary size="small" @click="exportConfiguration">导出</n-button>
        <n-button secondary size="small" @click="configurationInput?.click()">导入</n-button>
        <input ref="configurationInput" type="file" accept="application/json,.json" @change="importConfiguration" />
      </div>

      <section v-if="tabs.length" class="ssh-mobile-sessions" aria-label="已打开会话">
        <div class="ssh-mobile-sessions-title">
          <strong>已打开会话</strong>
          <span>{{ tabs.length }}</span>
        </div>
        <div class="ssh-mobile-session-list">
          <div v-for="tab in tabs" :key="`mobile-${tab.id}`" class="ssh-mobile-session">
            <button type="button" @click="resumeMobileSession(tab)">
              <span class="ssh-status-dot" :class="tab.status" />
              <span>{{ tab.host.name }}</span>
            </button>
            <button type="button" aria-label="关闭会话" @click="closeTab(tab.id)">×</button>
          </div>
        </div>
      </section>

      <n-scrollbar class="ssh-host-scroll" trigger="none" :theme-overrides="hostScrollbarTheme">
        <div class="ssh-host-list">
          <div v-if="loading" class="ssh-empty">正在载入主机…</div>
          <div v-else-if="filteredHosts.length === 0" class="ssh-empty">
            暂无主机
            <n-button text type="primary" @click="openHostEditor()">添加第一台</n-button>
          </div>
          <section v-for="section in hostSections" :key="section.key" class="ssh-host-section">
            <button
              class="ssh-host-section-title"
              type="button"
              :aria-expanded="!isHostSectionCollapsed(section.key)"
              @click="toggleHostSection(section.key)"
            >
              <span>{{ section.label }}</span>
              <span class="ssh-host-section-meta">
                <small>{{ section.hosts.length }}</small>
                <n-icon class="ssh-host-section-chevron" :class="{ collapsed: isHostSectionCollapsed(section.key) }" size="14"><ChevronDownOutline /></n-icon>
              </span>
            </button>
            <div v-show="!isHostSectionCollapsed(section.key)" class="ssh-host-section-items">
              <div
                v-for="host in section.hosts"
                :key="`${section.key}-${host.id}`"
                class="ssh-host"
                @contextmenu.prevent="openHostEditor(host)"
              >
                <button class="ssh-host-main" type="button" @click="openCredentials(host)">
                  <span class="ssh-host-icon">{{ host.name.slice(0, 1).toUpperCase() }}</span>
                  <span class="ssh-host-copy">
                    <strong>{{ host.name }}</strong>
                    <small>
                      {{ host.username }}@{{ host.host }}:{{ host.port }}
                      <template v-if="jumpHostName(host)"> · 经 {{ jumpHostName(host) }}</template>
                    </small>
                  </span>
                </button>
                <button
                  class="ssh-favorite-button"
                  :class="{ active: host.favorite }"
                  type="button"
                  :aria-label="host.favorite ? '取消收藏' : '收藏主机'"
                  :title="host.favorite ? '取消收藏' : '收藏主机'"
                  @click="toggleHostFavorite(host)"
                >
                  <n-icon size="16"><component :is="host.favorite ? Star : StarOutline" /></n-icon>
                </button>
                <n-button class="ssh-edit-button" secondary circle size="small" aria-label="编辑主机" @click="openHostEditor(host)">
                  <template #icon><n-icon><CreateOutline /></n-icon></template>
                </n-button>
              </div>
            </div>
          </section>
        </div>
      </n-scrollbar>
    </aside>

    <main class="ssh-workspace">
      <div class="ssh-tabs">
        <div class="ssh-tab-list" @wheel="handleTabListWheel" @dragover.prevent="handleTabListDragOver">
          <n-button class="ssh-mobile-hosts" quaternary size="small" @click="activeTabId = ''">
            <template #icon><n-icon><MenuOutline /></n-icon></template>
            主机
          </n-button>
          <button
            v-for="tab in tabs"
            :key="tab.id"
            type="button"
            class="ssh-tab"
            :class="{ active: tab.id === activeTabId, dragging: draggedTabId === tab.id }"
            draggable="true"
            @click="activateTab(tab.id)"
            @dragstart="handleTabDragStart($event, tab.id)"
            @dragover.prevent
            @drop.prevent="handleTabDrop($event, tab.id)"
            @dragend="draggedTabId = ''"
          >
            <span class="ssh-status-dot" :class="tab.status" />
            <span>{{ tab.host.name }}</span>
            <n-icon class="ssh-tab-close" size="14" @click.stop="closeTab(tab.id)"><CloseOutline /></n-icon>
          </button>
        </div>
        <div v-if="activeTab" class="ssh-tab-actions">
          <div class="ssh-pane-switch">
            <button type="button" :class="{ active: activePane === 'terminal' }" @click="showTerminalPane">终端</button>
            <button type="button" :class="{ active: activePane === 'sftp' }" @click="showSftpPane">文件</button>
          </div>
          <n-button v-if="activeTab.status === 'closed' || activeTab.status === 'error'" class="ssh-desktop-action" secondary size="tiny" @click="reconnectTab(activeTab)">重连</n-button>
          <terminal-action-bar
            v-if="activePane === 'terminal'"
            class="ssh-desktop-action"
            :recording="activeTab.recording"
            @search="openSearch"
            @snippets="openSnippets"
            @recording="toggleRecording(activeTab)"
          />
          <terminal-plugin-entry
            v-if="activePane === 'terminal'"
            transport="ssh"
            :target="activeTab.host.id"
          />
          <span class="ssh-status-text" :class="activeTab.status">{{ activeTab.message }}</span>
          <n-dropdown v-if="mobileActionOptions.length" trigger="click" :options="mobileActionOptions" @select="handleMobileAction">
            <n-button class="ssh-mobile-more" secondary size="tiny">更多</n-button>
          </n-dropdown>
          <terminal-renderer-badge
            v-if="activePane === 'terminal'"
            class="ssh-desktop-action ssh-renderer-status"
            :renderer="activeTab.renderer"
          />
        </div>
      </div>

      <terminal-search-bar
        v-if="activeTab && activePane === 'terminal' && showSearch"
        ref="searchInput"
        v-model:query="searchQuery"
        v-model:target-index="searchTargetIndex"
        v-model:case-sensitive="searchCaseSensitive"
        v-model:whole-word="searchWholeWord"
        v-model:regex="searchRegex"
        class="ssh-search-bar"
        :result-count="activeTab.searchResultCount"
        :count-text="searchCountText(activeTab)"
        @index-focus="searchIndexEditing = $event"
        @jump="jumpToSearchIndex"
        @previous="searchTerminal(true)"
        @next="searchTerminal(false)"
        @close="closeSearch"
      />

      <section v-if="tabs.length === 0" class="ssh-welcome">
        <div class="ssh-welcome-mark">›_</div>
        <template v-if="hosts.length === 0">
          <h1>添加第一台 SSH 主机</h1>
          <p>保存主机地址和用户名后，即可打开远程终端。</p>
          <n-button class="ssh-welcome-action" type="primary" size="large" @click="openHostEditor()">
            <template #icon><n-icon><AddOutline /></n-icon></template>
            添加 SSH 主机
          </n-button>
        </template>
        <template v-else>
          <h1>选择一台主机开始连接</h1>
          <p>凭据只用于本次连接，不会保存到服务器。</p>
        </template>
      </section>
      <div
        v-for="tab in tabs"
        v-show="tab.id === activeTabId && activePane === 'terminal'"
        :key="tab.id"
        class="ssh-terminal-pane"
      >
        <web-terminal
          class="ssh-terminal"
          :scrollback="terminalSettings.scrollbackLines"
          :font-size="terminalSettings.fontSize"
          :line-height="terminalSettings.lineHeight"
          :letter-spacing="terminalSettings.letterSpacing"
          :restore-buffer="tab.restoreBuffer"
          :search-highlight-limit="searchHighlightLimit"
          @ready="handleTerminalReady(tab, $event)"
          @data="handleTerminalData(tab, $event)"
          @resize="handleTerminalResize(tab, $event)"
          @renderer="updateRenderer(tab, $event)"
          @search-results="updateSearchResults(tab, $event)"
        />
        <terminal-special-key-bar
          :ctrl="tab.ctrlModifier"
          :alt="tab.altModifier"
          :disabled="tab.status !== 'connected'"
          @key="sendSpecialKey(tab, $event)"
          @modifier="toggleTerminalModifier(tab, $event)"
          @focus="tab.terminal?.focus()"
        />
        <terminal-command-panel
          :show-quick="showQuickSnippets"
          :show-composer="terminalSettings.showCommandComposer"
          :snippets="pinnedSnippets"
          @toggle-quick="toggleQuickSnippets"
          @toggle-composer="toggleCommandComposer"
          @use-snippet="useSnippet(tab, $event, false)"
        >
          <template #composer><div class="ssh-command-editor">
            <n-input
              v-model:value="tab.commandDraft"
              type="textarea"
              :autosize="{ minRows: 1, maxRows: 4 }"
              placeholder="输入要发送的命令，Ctrl/⌘ + Enter 发送"
              enterkeyhint="enter"
              @keydown="handleCommandKeydown(tab, $event)"
            />
            <n-dropdown
              trigger="click"
              :options="commandHistoryOptions(tab)"
              :disabled="tab.commandHistory.length === 0"
              @select="selectCommandHistory(tab, $event)"
            >
              <n-button class="ssh-history-button" secondary :disabled="tab.commandHistory.length === 0">历史</n-button>
            </n-dropdown>
            <n-button type="primary" :disabled="!tab.commandDraft.trim()" @click="sendCommand(tab)">发送</n-button>
          </div></template>
        </terminal-command-panel>
      </div>
      <sftp-panel
        v-for="tab in openedSftpTabs"
        v-show="tab.id === activeTabId && activePane === 'sftp'"
        :key="`sftp-${tab.id}`"
        :host="tab.host"
        @request-credentials="requestPersistentCredentials(tab.host)"
      />
    </main>

    <n-modal
      v-model:show="showHostEditor"
      preset="card"
      :title="editingId ? '编辑 SSH 主机' : '添加 SSH 主机'"
      class="ssh-dialog"
      :style="dialogStyle"
    >
      <n-form label-placement="top">
        <div class="ssh-form-grid">
          <n-form-item label="名称"><n-input v-model:value="hostDraft.name" placeholder="生产服务器" /></n-form-item>
          <n-form-item label="分组"><n-input v-model:value="hostDraft.group" placeholder="可选" /></n-form-item>
        </div>
        <n-form-item label="主机"><n-input v-model:value="hostDraft.host" placeholder="example.com 或 IP" /></n-form-item>
        <div class="ssh-form-grid ssh-form-grid--connection">
          <n-form-item label="端口"><n-input-number v-model:value="hostDraft.port" :min="1" :max="65535" /></n-form-item>
          <n-form-item label="用户名"><n-input v-model:value="hostDraft.username" placeholder="root" /></n-form-item>
        </div>
        <n-checkbox v-model:checked="hostDraft.useAgent">默认使用服务端 SSH Agent 认证</n-checkbox>
        <n-form-item label="跳板机（可选）">
          <n-select v-model:value="hostDraft.jumpHostId" clearable :options="jumpHostOptions" placeholder="直接连接" />
        </n-form-item>
        <n-form-item v-if="hostDraft.hostKeySha256" label="已信任主机指纹">
          <n-input :value="hostDraft.hostKeySha256" readonly />
          <n-button class="ssh-reset-key" size="small" @click="hostDraft.hostKeySha256 = ''">重置信任</n-button>
        </n-form-item>
        <n-alert v-if="hostDraft.hasCredential" type="success" :show-icon="true">
          这台主机已保存加密凭据。
          <n-button class="ssh-forget-credential" text type="error" @click="forgetCredential">删除已保存凭据</n-button>
        </n-alert>
      </n-form>
      <template #footer>
        <div class="ssh-dialog-actions">
          <n-button v-if="editingId" type="error" secondary @click="deleteHost">删除</n-button>
          <span class="ssh-dialog-spacer" />
          <n-button @click="showHostEditor = false">取消</n-button>
          <n-button type="primary" :loading="saving" @click="saveHost">保存</n-button>
        </div>
      </template>
    </n-modal>

    <n-modal v-model:show="showCredentials" preset="card" title="连接认证" class="ssh-dialog" :style="dialogStyle">
      <p class="ssh-connect-target">{{ selectedHost?.username }}@{{ selectedHost?.host }}:{{ selectedHost?.port }}</p>
      <n-tabs v-model:value="credentialDraft.method" type="segment">
        <n-tab-pane name="password" tab="密码">
          <n-form-item label="密码">
            <n-input v-model:value="credentialDraft.password" type="password" show-password-on="click" @keyup.enter="connect" />
          </n-form-item>
        </n-tab-pane>
        <n-tab-pane name="privateKey" tab="私钥">
          <n-upload :show-file-list="false" @change="loadPrivateKey">
            <n-button size="small">读取私钥文件</n-button>
          </n-upload>
          <p class="ssh-field-hint">支持无后缀的 id_rsa、id_ed25519，以及 PEM、KEY 等私钥文件。</p>
          <n-input v-model:value="credentialDraft.privateKey" class="ssh-private-key" type="textarea" :rows="7" placeholder="-----BEGIN OPENSSH PRIVATE KEY-----" />
          <n-form-item label="私钥口令（可选）">
            <n-input v-model:value="credentialDraft.passphrase" type="password" show-password-on="click" />
          </n-form-item>
        </n-tab-pane>
        <n-tab-pane name="agent" tab="SSH Agent">
          <n-alert type="info" :show-icon="true">
            使用运行 Space Station 服务进程可访问的 SSH Agent（SSH_AUTH_SOCK）进行认证，无需上传私钥。
          </n-alert>
        </n-tab-pane>
      </n-tabs>
      <n-checkbox v-if="credentialDraft.method !== 'agent'" v-model:checked="credentialDraft.remember">本次打开期间记住凭据</n-checkbox>
      <n-checkbox v-if="credentialDraft.method !== 'agent'" v-model:checked="credentialDraft.persist" class="ssh-persist-credential">
        加密保存凭据，刷新页面后仍可直接连接
      </n-checkbox>
      <template #footer>
        <div class="ssh-dialog-actions ssh-dialog-actions--end">
          <n-button @click="showCredentials = false">取消</n-button>
          <n-button type="primary" @click="connect">连接</n-button>
        </div>
      </template>
    </n-modal>

    <n-modal v-model:show="showFingerprint" preset="card" title="确认主机身份" class="ssh-dialog" :style="dialogStyle">
      <n-alert type="warning" :show-icon="true">
        这是第一次连接该主机。请核对服务器上的主机密钥指纹后再信任。
      </n-alert>
      <dl class="ssh-fingerprint">
        <dt>主机</dt><dd>{{ fingerprintRequest?.host }}:{{ fingerprintRequest?.port }}</dd>
        <dt>SHA256</dt><dd>{{ fingerprintRequest?.fingerprint }}</dd>
      </dl>
      <template #footer>
        <div class="ssh-dialog-actions ssh-dialog-actions--end">
          <n-button @click="answerFingerprint(false)">拒绝</n-button>
          <n-button type="warning" @click="answerFingerprint(true)">信任并连接</n-button>
        </div>
      </template>
    </n-modal>

    <n-modal v-model:show="showSnippets" preset="card" title="命令片段" class="ssh-dialog" :style="dialogStyle">
      <n-tabs v-model:value="snippetLibraryTab" type="line" animated>
        <n-tab-pane name="local" tab="本地片段">
          <div class="ssh-snippet-editor">
            <n-input v-model:value="snippetDraft.name" placeholder="名称，例如：查看磁盘" />
            <n-input v-model:value="snippetDraft.command" type="textarea" :rows="3" placeholder="df -h" />
            <n-select v-model:value="snippetDraft.action" :options="snippetActionOptions" />
            <n-checkbox v-model:checked="snippetDraft.pinned">显示为终端快捷按钮</n-checkbox>
            <div class="ssh-snippet-editor-actions">
              <n-button v-if="editingSnippetId" @click="cancelSnippetEdit">取消编辑</n-button>
              <n-button type="primary" @click="saveSnippet">{{ editingSnippetId ? '保存修改' : '添加片段' }}</n-button>
            </div>
          </div>
          <div class="ssh-snippet-list">
            <p v-if="snippets.length" class="ssh-snippet-order-hint">SSH 与串口共用此本地列表；拖动左侧手柄调整顺序。</p>
            <div
              v-for="snippet in snippets"
              :key="snippet.id"
              class="ssh-snippet-row"
              :data-snippet-id="snippet.id"
              :class="{
                'ssh-snippet-row--dragging': draggedSnippetId === snippet.id,
                'ssh-snippet-row--drop-before': snippetDropTargetId === snippet.id && snippetDropPosition === 'before',
                'ssh-snippet-row--drop-after': snippetDropTargetId === snippet.id && snippetDropPosition === 'after',
              }"
            >
              <button
                class="ssh-snippet-drag-handle"
                type="button"
                aria-label="拖动调整片段顺序"
                title="按住拖动排序"
                @pointerdown="startSnippetPointerDrag($event, snippet.id)"
                @pointermove="updateSnippetPointerDrag"
                @pointerup="dropSnippetPointerDrag"
                @pointercancel="cancelSnippetPointerDrag"
              >⠿</button>
              <button class="ssh-snippet-content" type="button" @click="activeTab && useSnippet(activeTab, snippet)">
                <span class="ssh-snippet-title"><strong>{{ snippet.name }}</strong><small>{{ snippet.action === 'insert' ? '插入' : '执行' }}</small></span>
                <code>{{ snippet.command }}</code>
              </button>
              <n-button text type="primary" :disabled="sharedActionId === snippet.id || localShareStatus(snippet) === '已共享'" @click="storeSnippetInLibrary(snippet)">{{ localShareStatus(snippet) }}</n-button>
              <n-button text type="primary" @click="editSnippet(snippet)">编辑</n-button>
              <n-button text type="error" @click="deleteSnippet(snippet.id)">删除</n-button>
            </div>
            <p v-if="snippets.length === 0" class="ssh-field-hint">还没有本地片段，可新建或从共享库添加。</p>
          </div>
        </n-tab-pane>
        <n-tab-pane name="shared" tab="共享片段库">
          <div class="ssh-shared-snippet-heading">
            <p>共享库保存在服务器；按需把单条片段添加到当前浏览器。</p>
            <n-button size="tiny" secondary :loading="libraryLoading" @click="loadSharedSnippetLibrary(true)">刷新</n-button>
          </div>
          <n-alert type="warning" :show-icon="false">共享片段对能访问本服务的客户端可见，请勿保存口令、令牌等秘密。</n-alert>
          <div class="ssh-snippet-list ssh-shared-snippet-list">
            <div v-for="snippet in librarySnippets" :key="snippet.id" class="ssh-snippet-row">
              <div class="ssh-snippet-content">
                <span class="ssh-snippet-title"><strong>{{ snippet.name }}</strong><small>{{ snippet.action === 'insert' ? '插入' : '执行' }}</small></span>
                <code>{{ snippet.command }}</code>
              </div>
              <n-button text type="primary" :disabled="sharedActionId === snippet.id || sharedImportStatus(snippet) === '已在本地'" @click="addSharedSnippetToLocal(snippet)">{{ sharedImportStatus(snippet) }}</n-button>
              <n-popconfirm @positive-click="deleteSnippetFromLibrary(snippet.id)">
                <template #trigger><n-button text type="error" :disabled="sharedActionId === snippet.id">删除</n-button></template>
                只会从共享库删除，不影响各浏览器已有的本地片段。确认删除吗？
              </n-popconfirm>
            </div>
            <p v-if="libraryLoading && librarySnippets.length === 0" class="ssh-field-hint">正在读取共享片段…</p>
            <p v-else-if="librarySnippets.length === 0" class="ssh-field-hint">共享库还没有片段。</p>
          </div>
        </n-tab-pane>
      </n-tabs>
    </n-modal>

    <n-modal v-model:show="showPortForwards" preset="card" title="本地端口转发" class="ssh-dialog" :style="dialogStyle">
      <n-alert type="info" :show-icon="true">监听地址固定为服务端的 127.0.0.1，不会暴露到公网。</n-alert>
      <div class="ssh-forward-form">
        <n-select v-model:value="forwardDraft.hostId" :options="hostOptions" placeholder="SSH 主机" />
        <n-input-number v-model:value="forwardDraft.localPort" :min="1" :max="65535" placeholder="本地端口" />
        <n-input v-model:value="forwardDraft.remoteHost" placeholder="远端地址，例如 127.0.0.1" />
        <n-input-number v-model:value="forwardDraft.remotePort" :min="1" :max="65535" placeholder="远端端口" />
        <n-button type="primary" @click="startForward">启动转发</n-button>
      </div>
      <div class="ssh-forward-list">
        <div v-for="forward in portForwards" :key="forward.id" class="ssh-forward-row">
          <div><strong>127.0.0.1:{{ forward.localPort }}</strong><small> → {{ forward.remoteHost }}:{{ forward.remotePort }} · {{ forward.message }}</small></div>
          <n-button text type="error" @click="stopForward(forward.id)">停止</n-button>
        </div>
        <p v-if="portForwards.length === 0" class="ssh-field-hint">没有运行中的端口转发。</p>
      </div>
    </n-modal>

    <n-modal v-model:show="showRecordingOptions" preset="card" title="开始会话录制" class="ssh-dialog" :style="dialogStyle">
      <div class="ssh-recording-options">
        <n-checkbox v-model:checked="recordingDraft.stripAnsi">过滤 ANSI 颜色和终端控制字符（推荐）</n-checkbox>
        <n-checkbox v-model:checked="recordingDraft.timestamps">为每行添加时间戳</n-checkbox>
        <p>录制内容仅缓存在当前浏览器标签内，停止后下载为日志文件。</p>
      </div>
      <template #footer>
        <div class="ssh-dialog-actions ssh-dialog-actions--end">
          <n-button @click="showRecordingOptions = false">取消</n-button>
          <n-button type="primary" @click="startRecording">开始录制</n-button>
        </div>
      </template>
    </n-modal>

    <n-modal v-model:show="showTerminalSettings" preset="card" title="终端设置" class="ssh-dialog" :style="dialogStyle">
      <n-form label-placement="top">
        <n-form-item label="终端回滚缓冲区（输出行）">
          <n-input-number v-model:value="terminalSettingsDraft.scrollbackLines" :min="1000" :max="500000" :step="10000" />
          <template #feedback>按 120 列终端折算；窄屏自动增加内部屏幕行容量，避免自动折行占满缓存。</template>
        </n-form-item>
        <p class="ssh-field-hint">保存后对新打开或重新连接的终端生效。</p>
        <n-form-item label="字体大小">
          <n-input-number v-model:value="terminalSettingsDraft.fontSize" :min="10" :max="28" :step="1" />
        </n-form-item>
        <n-form-item label="行高">
          <n-input-number v-model:value="terminalSettingsDraft.lineHeight" :min="1" :max="2" :step="0.05" />
        </n-form-item>
        <n-form-item label="字符间距">
          <n-input-number v-model:value="terminalSettingsDraft.letterSpacing" :min="0" :max="4" :step="0.5" />
        </n-form-item>
        <n-form-item label="命令编辑区">
          <n-checkbox v-model:checked="terminalSettingsDraft.showCommandComposer">显示命令编辑和发送框</n-checkbox>
        </n-form-item>
        <p class="ssh-field-hint">字体和间距会立即应用到已打开终端；较小字体和行高可以显示更多行。</p>
        <n-form-item class="ssh-settings-recording" label="单次录制缓冲区上限（MiB）">
          <n-input-number v-model:value="terminalSettingsDraft.recordingMaxMiB" :min="1" :max="500" :step="10" />
        </n-form-item>
        <p class="ssh-field-hint">录制内容仅保存在当前浏览器标签内；达到上限后会停止追加并提示。</p>
        <n-form-item class="ssh-settings-recording" label="剪贴板操作">
          <div class="ssh-settings-switches">
            <n-checkbox v-model:checked="terminalSettingsDraft.copyOnSelect">选中终端文本后自动复制</n-checkbox>
            <n-checkbox v-model:checked="terminalSettingsDraft.pasteOnRightClick">在终端内右键时自动粘贴</n-checkbox>
            <div class="ssh-clipboard-permission">
              <span>读取权限：{{ clipboardPermissionLabel }}</span>
              <n-button size="small" secondary :loading="clipboardPermissionState === 'checking'" @click="requestClipboardAccess">检测/授权</n-button>
            </div>
          </div>
        </n-form-item>
        <p class="ssh-field-hint">已授权时右键自动粘贴；无权限时保留浏览器原生右键菜单。不会额外发送回车，但多行内容仍可能被远程 Shell 执行。</p>
      </n-form>
      <template #footer>
        <div class="ssh-dialog-actions ssh-dialog-actions--end">
          <n-button @click="showTerminalSettings = false">取消</n-button>
          <n-button type="primary" @click="saveTerminalSettings">保存</n-button>
        </div>
      </template>
    </n-modal>
  </div>
</template>

<script setup lang="ts">
import type { Terminal } from "@xterm/xterm";
import { AddOutline, ChevronDownOutline, CloseOutline, CreateOutline, MenuOutline, Star, StarOutline } from "@vicons/ionicons5";
import {
  NAlert,
  NButton,
  NCheckbox,
  NDropdown,
  NForm,
  NFormItem,
  NIcon,
  NInput,
  NInputNumber,
  NModal,
  NPopconfirm,
  NScrollbar,
  NSelect,
  NTabPane,
  NTabs,
  NUpload,
  useMessage,
  type UploadFileInfo,
} from "naive-ui";
import { computed, nextTick, onBeforeUnmount, onMounted, reactive, ref, watch } from "vue";
import { writeClipboard } from "@/utils/clipboard";
import {
  persistCommandSnippets,
  useCommandSnippets,
  useCommandSnippetReorder,
  useSharedCommandSnippetLibrary,
  type CommandSnippet,
  type SnippetAction,
} from "@/utils/commandSnippets";
import {
  deleteSshCredential,
  fetchSshHosts,
  fetchSshPortForwards,
  saveSshHosts,
  startSshPortForward,
  stopSshPortForward,
  type SharedCommandSnippet,
  type SshHost,
  type SshPortForward,
} from "@/api";
import SftpPanel from "./SftpPanel.vue";
import WebTerminal from "../terminal/WebTerminal.vue";
import TerminalActionBar from "../terminal/TerminalActionBar.vue";
import TerminalSearchBar from "../terminal/TerminalSearchBar.vue";
import TerminalCommandPanel from "../terminal/TerminalCommandPanel.vue";
import TerminalSpecialKeyBar from "../terminal/TerminalSpecialKeyBar.vue";
import TerminalPluginEntry from "../terminal/TerminalPluginEntry.vue";
import TerminalRendererBadge from "../terminal/TerminalRendererBadge.vue";
import { attachTerminalClipboard } from "../terminal/terminalClipboard";
import { describeTerminalClipboardError, useTerminalClipboardPermission } from "../terminal/useTerminalClipboardPermission";
import { useMobileVisualViewport } from "../terminal/useMobileVisualViewport";
import {
  clampTerminalDecimal as clampDecimal,
  clampTerminalInteger as clampNumber,
  loadTerminalPreferences,
  normalizeTerminalPreferences,
  saveTerminalPreferences,
  type TerminalPreferences,
} from "../terminal/terminalPreferences";
import type {
  TerminalRenderer,
  WebTerminalHandle,
  WebTerminalReadyEvent,
  WebTerminalResizeEvent,
  WebTerminalSearchResult,
} from "../terminal/WebTerminal.types";

type ConnectionStatus = "connecting" | "authenticating" | "connected" | "closed" | "error";
type TerminalModifier = "ctrl" | "alt";
type TerminalSpecialKey =
  | "escape"
  | "tab"
  | "arrowLeft"
  | "arrowDown"
  | "arrowUp"
  | "arrowRight"
  | "home"
  | "end"
  | "pageUp"
  | "pageDown"
  | "pipe"
  | "slash"
  | "dash"
  | "tilde";

interface TerminalTab {
  id: string;
  host: SshHost;
  status: ConnectionStatus;
  message: string;
  socket: WebSocket;
  terminal?: Terminal;
  terminalView?: WebTerminalHandle;
  connectPayload: Record<string, unknown>;
  pendingCredential?: CredentialData;
  rememberCredential: boolean;
  persistCredential: boolean;
  usedStoredCredential: boolean;
  restoreBuffer?: string;
  recording: boolean;
  recordingStartedAt?: string;
  recordingContent: string;
  recordingEntries: Array<{ at: Date; data: string }>;
  recordingDecoder?: TextDecoder;
  recordingStripAnsi: boolean;
  recordingTimestamps: boolean;
  recordingSizeBytes: number;
  recordingLimitReached: boolean;
  renderer: "webgl" | "canvas";
  searchResultIndex: number;
  searchResultCount: number;
  searchResultLimited: boolean;
  clipboardCleanup?: () => void;
  reconnectHintShown: boolean;
  socketReady: boolean;
  automaticRetryCount: number;
  commandDraft: string;
  commandHistory: string[];
  ctrlModifier: boolean;
  altModifier: boolean;
}

type TerminalSettings = TerminalPreferences;

interface CredentialData {
  method: string;
  password: string;
  privateKey: string;
  passphrase: string;
}

interface FingerprintRequest {
  tabId: string;
  host: string;
  port: number;
  fingerprint: string;
}

const message = useMessage();
const { mobileViewportStyle } = useMobileVisualViewport();
const {
  clipboardPermissionState,
  clipboardPermissionLabel,
  refreshClipboardPermission,
  requestClipboardPermission,
  canReadClipboardAutomatically,
  markClipboardReadFailed,
  disposeClipboardPermission,
} = useTerminalClipboardPermission();
const hosts = ref<SshHost[]>([]);
const tabs = ref<TerminalTab[]>([]);
const activeTabId = ref("");
const draggedTabId = ref("");
const activePane = ref<"terminal" | "sftp">("terminal");
const openedSftpTabIds = ref<string[]>([]);
const openedSftpTabs = computed(() => tabs.value.filter((tab) => openedSftpTabIds.value.includes(tab.id)));
const showSearch = ref(false);
const searchQuery = ref("");
const searchCaseSensitive = ref(false);
const searchWholeWord = ref(false);
const searchRegex = ref(false);
const searchTargetIndex = ref<number | null>(null);
const searchIndexEditing = ref(false);
const searchInput = ref<{ focus: () => void } | null>(null);
const showSnippets = ref(false);
const snippets = useCommandSnippets();
const snippetDraft = reactive<{ name: string; command: string; pinned: boolean; action: SnippetAction }>({
  name: "",
  command: "",
  pinned: true,
  action: "insert",
});
const editingSnippetId = ref("");
const {
  draggedSnippetId,
  snippetDropTargetId,
  snippetDropPosition,
  startSnippetPointerDrag,
  updateSnippetPointerDrag,
  dropSnippetPointerDrag,
  cancelSnippetPointerDrag,
} = useCommandSnippetReorder(snippets, ".ssh-snippet-row");
const snippetLibraryTab = ref<"local" | "shared">("local");
const sharedActionId = ref("");
const {
  librarySnippets,
  libraryLoading,
  refreshSharedSnippets,
  storeSharedSnippet,
  removeSharedSnippet,
} = useSharedCommandSnippetLibrary();
const showQuickSnippets = ref(localStorage.getItem("ssh-show-quick-snippets") !== "false");
const configurationInput = ref<HTMLInputElement | null>(null);
const showPortForwards = ref(false);
const portForwards = ref<SshPortForward[]>([]);
const forwardDraft = reactive({ hostId: "", localPort: 8080, remoteHost: "127.0.0.1", remotePort: 80 });
const showRecordingOptions = ref(false);
const recordingTargetId = ref("");
const recordingDraft = reactive({ stripAnsi: true, timestamps: false });
const terminalSettings = reactive<TerminalSettings>(loadTerminalPreferences());
const terminalSettingsDraft = reactive<TerminalSettings>({ ...terminalSettings });
const showTerminalSettings = ref(false);
const credentialCache = new Map<string, CredentialData>();
let clipboardWarningShown = false;
let clipboardFallbackHintShown = false;
let originalViewportContent: string | null = null;
let originalThemeColor: string | null = null;
let searchInputTimer: number | undefined;
const keyword = ref("");
const loading = ref(true);
const saving = ref(false);
const showHostEditor = ref(false);
const showCredentials = ref(false);
const showFingerprint = ref(false);
const editingId = ref("");
const selectedHost = ref<SshHost | null>(null);
const fingerprintRequest = ref<FingerprintRequest | null>(null);
const hostDraft = reactive<SshHost>(emptyHost());
const credentialDraft = reactive({
  method: "password",
  password: "",
  privateKey: "",
  passphrase: "",
  remember: true,
  persist: false,
});
const dialogStyle = { width: "var(--ssh-dialog-width)" };
const hostScrollbarTheme = {
  width: "7px",
  borderRadius: "999px",
  color: "#527c7a",
  colorHover: "#83b3af",
  railColor: "#151b20",
};
const searchHighlightLimit = 1000;
const snippetActionOptions = [
  { label: "插入输入框，可编辑后发送", value: "insert" },
  { label: "点击后立即执行", value: "run" },
];
const collapsedHostSectionsKey = "space-station:ssh-collapsed-host-sections";
const collapsedHostSections = ref<Set<string>>(loadCollapsedHostSections());
let hostSaveQueue: Promise<void> = Promise.resolve();

const filteredHosts = computed(() => {
  const query = keyword.value.trim().toLowerCase();
  if (!query) return hosts.value;
  return hosts.value.filter((host) => [host.name, host.host, host.username, host.group || ""].some((part) => part.toLowerCase().includes(query)));
});
const hostSections = computed(() => {
  const query = keyword.value.trim();
  if (query) return filteredHosts.value.length
    ? [{ key: "search", label: "搜索结果", hosts: filteredHosts.value }]
    : [];

  const sections: Array<{ key: string; label: string; hosts: SshHost[] }> = [];
  const favorites = hosts.value.filter((host) => host.favorite);
  if (favorites.length) sections.push({ key: "favorites", label: "收藏", hosts: favorites });

  const recent = hosts.value
    .filter((host) => host.lastUsedAt)
    .slice()
    .sort((left, right) => Date.parse(right.lastUsedAt || "") - Date.parse(left.lastUsedAt || ""))
    .slice(0, 5);
  if (recent.length) sections.push({ key: "recent", label: "最近使用", hosts: recent });

  const groups = new Map<string, SshHost[]>();
  for (const host of hosts.value) {
    const group = host.group?.trim() || "未分组";
    const items = groups.get(group) || [];
    items.push(host);
    groups.set(group, items);
  }
  Array.from(groups.entries())
    .sort(([left], [right]) => left === "未分组" ? 1 : right === "未分组" ? -1 : left.localeCompare(right, "zh-CN"))
    .forEach(([label, items]) => sections.push({ key: `group:${label}`, label, hosts: items }));
  return sections;
});
const activeTab = computed(() => tabs.value.find((tab) => tab.id === activeTabId.value));
const jumpHostOptions = computed(() => hosts.value
  .filter((host) => host.id !== editingId.value && !host.jumpHostId)
  .map((host) => ({ label: `${host.name} (${host.username}@${host.host})`, value: host.id })));
const hostOptions = computed(() => hosts.value.map((host) => ({ label: host.name, value: host.id })));
const pinnedSnippets = computed(() => snippets.value.filter((snippet) => snippet.pinned !== false));
const mobileActionOptions = computed(() => {
  const tab = activeTab.value;
  if (!tab) return [];
  const options: Array<{ label: string; key: string; disabled?: boolean }> = [];
  if (tab.status === "closed" || tab.status === "error") options.push({ label: "重新连接", key: "reconnect" });
  if (activePane.value === "terminal") {
    options.push(
      { label: "搜索终端", key: "search" },
      { label: "命令片段", key: "snippets" },
      { label: tab.recording ? "停止录制" : "开始录制", key: "recording" },
      { label: `渲染：${tab.renderer === "webgl" ? "GPU" : "Canvas"}`, key: "renderer", disabled: true },
    );
  }
  return options;
});
watch([searchQuery, searchCaseSensitive, searchWholeWord, searchRegex], ([value]) => {
  if (!showSearch.value) return;
  if (searchInputTimer) window.clearTimeout(searchInputTimer);
  if (!value) {
    activeTab.value?.terminalView?.clearSearch();
    resetSearchResults(activeTab.value);
    return;
  }
  searchInputTimer = window.setTimeout(() => searchTerminal(true, true), 200);
});

watch(showSnippets, (visible) => {
  if (visible) void loadSharedSnippetLibrary(true);
});

onMounted(() => {
  const viewport = document.querySelector<HTMLMetaElement>('meta[name="viewport"]');
  if (viewport) {
    originalViewportContent = viewport.content;
    if (!viewport.content.includes("viewport-fit=cover")) viewport.content += ", viewport-fit=cover";
  }
  const themeColor = document.querySelector<HTMLMetaElement>('meta[name="theme-color"]');
  if (themeColor) {
    originalThemeColor = themeColor.content;
    themeColor.content = "#101418";
  }
  document.documentElement.classList.add("ssh-page-lock");
  document.body.classList.add("ssh-page-lock");
  void loadHosts();
  void refreshClipboardPermission();
  window.addEventListener("keydown", handleGlobalShortcut, true);
});
onBeforeUnmount(() => {
  if (searchInputTimer) window.clearTimeout(searchInputTimer);
  const viewport = document.querySelector<HTMLMetaElement>('meta[name="viewport"]');
  if (viewport && originalViewportContent !== null) viewport.content = originalViewportContent;
  const themeColor = document.querySelector<HTMLMetaElement>('meta[name="theme-color"]');
  if (themeColor && originalThemeColor !== null) themeColor.content = originalThemeColor;
  document.documentElement.classList.remove("ssh-page-lock");
  document.body.classList.remove("ssh-page-lock");
  disposeClipboardPermission();
  window.removeEventListener("keydown", handleGlobalShortcut, true);
  tabs.value.forEach(disposeTab);
});

function emptyHost(): SshHost {
  return { id: "", name: "", host: "", port: 22, username: "root", group: "", hostKeySha256: "", useAgent: false, jumpHostId: "", favorite: false, lastUsedAt: "" };
}

function loadCollapsedHostSections() {
  try {
    const value = JSON.parse(localStorage.getItem(collapsedHostSectionsKey) || "[]");
    return new Set<string>(Array.isArray(value) ? value.filter((item): item is string => typeof item === "string") : []);
  } catch {
    return new Set<string>();
  }
}

function isHostSectionCollapsed(key: string) {
  return collapsedHostSections.value.has(key);
}

function toggleHostSection(key: string) {
  const next = new Set(collapsedHostSections.value);
  if (next.has(key)) next.delete(key);
  else next.add(key);
  collapsedHostSections.value = next;
  localStorage.setItem(collapsedHostSectionsKey, JSON.stringify(Array.from(next)));
}

function createId() {
  return window.crypto.randomUUID?.() || `${Date.now()}-${Math.random().toString(16).slice(2)}`;
}

function enqueueHostSave(snapshot = hosts.value.map((host) => ({ ...host }))) {
  const operation = hostSaveQueue.then(() => saveSshHosts(snapshot));
  hostSaveQueue = operation.catch(() => undefined);
  return operation;
}

async function toggleHostFavorite(host: SshHost) {
  const previous = Boolean(host.favorite);
  host.favorite = !previous;
  try {
    await enqueueHostSave();
  } catch (error) {
    host.favorite = previous;
    message.error(error instanceof Error ? error.message : "收藏状态保存失败");
  }
}

function markHostUsed(host: SshHost) {
  host.lastUsedAt = new Date().toISOString();
  void enqueueHostSave().catch((error) => {
    console.warn("SSH recent host save failed", error);
  });
}

function jumpHostName(host: SshHost) {
  if (!host.jumpHostId) return "";
  return hosts.value.find((candidate) => candidate.id === host.jumpHostId)?.name || "未知跳板机";
}

async function loadHosts() {
  try {
    hosts.value = (await fetchSshHosts()).hosts;
  } catch (error) {
    message.error(error instanceof Error ? error.message : "SSH 主机加载失败");
  } finally {
    loading.value = false;
  }
}

function openHostEditor(host?: SshHost) {
  editingId.value = host?.id ?? "";
  Object.assign(hostDraft, host ? { ...host } : emptyHost());
  showHostEditor.value = true;
}

async function saveHost() {
  if (!hostDraft.name.trim() || !hostDraft.host.trim() || !hostDraft.username.trim()) {
    message.warning("名称、主机和用户名不能为空");
    return;
  }
  saving.value = true;
  try {
    const value: SshHost = {
      ...hostDraft,
      id: editingId.value || createId(),
      name: hostDraft.name.trim(),
      host: hostDraft.host.trim(),
      username: hostDraft.username.trim(),
      group: hostDraft.group.trim(),
      port: Math.max(1, Math.min(65535, Number(hostDraft.port) || 22)),
      useAgent: Boolean(hostDraft.useAgent),
      jumpHostId: hostDraft.jumpHostId || "",
    };
    const next = editingId.value ? hosts.value.map((host) => (host.id === editingId.value ? value : host)) : [...hosts.value, value];
    if (value.jumpHostId === value.id) {
      message.warning("不能将当前主机设置为自己的跳板机");
      return;
    }
    const jumpHost = next.find((host) => host.id === value.jumpHostId);
    if (jumpHost?.jumpHostId) {
      message.warning("当前只支持单层跳板，所选跳板机自身不能再配置跳板机");
      return;
    }
    await enqueueHostSave(next);
    hosts.value = next;
    showHostEditor.value = false;
  } catch (error) {
    message.error(error instanceof Error ? error.message : "SSH 主机保存失败");
  } finally {
    saving.value = false;
  }
}

async function deleteHost() {
  if (!window.confirm(`确定删除 SSH 主机“${hostDraft.name}”吗？保存的凭据也会一并删除。`)) return;
  const next = hosts.value.filter((host) => host.id !== editingId.value);
  await enqueueHostSave(next);
  hosts.value = next;
  showHostEditor.value = false;
}

async function forgetCredential() {
  if (!editingId.value) return;
  try {
    await deleteSshCredential(editingId.value);
    hostDraft.hasCredential = false;
    const host = hosts.value.find((item) => item.id === editingId.value);
    if (host) host.hasCredential = false;
    credentialCache.delete(editingId.value);
    message.success("已删除保存的凭据");
  } catch (error) {
    message.error(error instanceof Error ? error.message : "凭据删除失败");
  }
}

function openCredentials(host: SshHost) {
  markHostUsed(host);
  if (host.useAgent) {
    openTerminal(host, { method: "agent", password: "", privateKey: "", passphrase: "" }, false, false);
    return;
  }
  const cached = credentialCache.get(host.id);
  if (cached) {
    openTerminal(host, { ...cached }, true, false);
    return;
  }
  if (host.hasCredential) {
    openTerminal(host, { method: "stored", password: "", privateKey: "", passphrase: "" }, false, false);
    return;
  }
  selectedHost.value = host;
  Object.assign(credentialDraft, {
    method: "password",
    password: "",
    privateKey: "",
    passphrase: "",
    remember: true,
    persist: false,
  });
  showCredentials.value = true;
}

function resumeMobileSession(tab: TerminalTab) {
  activeTabId.value = tab.id;
  activePane.value = "terminal";
  void nextTick(() => {
    tab.terminalView?.fit();
    sendResize(tab);
  });
}

function handleMobileAction(key: string) {
  const tab = activeTab.value;
  if (!tab) return;
  if (key === "reconnect") reconnectTab(tab);
  else if (key === "search") openSearch();
  else if (key === "snippets") openSnippets();
  else if (key === "recording") toggleRecording(tab);
}

function requestPersistentCredentials(host: SshHost) {
  selectedHost.value = host;
  Object.assign(credentialDraft, {
    method: "password",
    password: "",
    privateKey: "",
    passphrase: "",
    remember: true,
    persist: true,
  });
  showCredentials.value = true;
}

async function loadPrivateKey(options: { file: UploadFileInfo }) {
  const file = options.file.file;
  if (!file) return;
  if (file.size > 1024 * 1024) {
    message.error("私钥文件不能超过 1 MiB");
    return;
  }
  const content = (await file.text()).trim();
  if (!/^-----BEGIN (?:OPENSSH |RSA |EC |DSA |ENCRYPTED )?PRIVATE KEY-----/.test(content)) {
    message.error("所选文件不是支持的 SSH 私钥");
    return;
  }
  credentialDraft.privateKey = `${content}\n`;
  message.success(`已读取 ${file.name}`);
}

async function connect() {
  const host = selectedHost.value;
  const credential = credentialDraft.method === "privateKey" ? credentialDraft.privateKey : credentialDraft.password;
  if (!host || (credentialDraft.method !== "agent" && !credential)) {
    message.warning("请输入认证凭据");
    return;
  }
  showCredentials.value = false;
  openTerminal(host, {
    method: credentialDraft.method,
    password: credentialDraft.password,
    privateKey: credentialDraft.privateKey,
    passphrase: credentialDraft.passphrase,
  }, credentialDraft.method === "agent" ? false : credentialDraft.remember || credentialDraft.persist,
  credentialDraft.method === "agent" ? false : credentialDraft.persist);
  Object.assign(credentialDraft, { password: "", privateKey: "", passphrase: "" });
}

async function openTerminal(
  host: SshHost,
  credential: CredentialData,
  rememberCredential: boolean,
  persistCredential: boolean,
) {
  const id = createId();
  const socket = createTerminalSocket();
  const tab: TerminalTab = {
    id,
    host,
    status: "connecting",
    message: "正在打开连接…",
    socket,
    connectPayload: buildConnectPayload(host, credential, persistCredential),
    pendingCredential: credential,
    rememberCredential,
    persistCredential,
    usedStoredCredential: credential.method === "stored",
    recording: false,
    recordingContent: "",
    recordingEntries: [],
    recordingStripAnsi: true,
    recordingTimestamps: false,
    recordingSizeBytes: 0,
    recordingLimitReached: false,
    renderer: "canvas",
    searchResultIndex: -1,
    searchResultCount: 0,
    searchResultLimited: false,
    reconnectHintShown: false,
    socketReady: false,
    automaticRetryCount: 0,
    commandDraft: "",
    commandHistory: [],
    ctrlModifier: false,
    altModifier: false,
  };
  // The backend sends `ready` immediately after the WebSocket handshake. Bind
  // handlers before rendering the terminal so a fast first connection cannot
  // lose that event while `nextTick` is pending.
  bindTerminalSocket(tab, socket);
  tabs.value.push(tab);
  activeTabId.value = id;
  activePane.value = "terminal";
  await nextTick();
}

function createTerminalSocket() {
  const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
  const socket = new WebSocket(`${protocol}//${window.location.host}/api/tools/ssh/terminal`);
  socket.binaryType = "arraybuffer";
  return socket;
}

function buildConnectPayload(host: SshHost, credential: CredentialData, persistCredential: boolean) {
  return {
    type: "connect",
    hostId: host.id,
    password: credential.method === "password" ? credential.password : "",
    privateKey: credential.method === "privateKey" ? credential.privateKey : "",
    passphrase: credential.passphrase,
    useAgent: credential.method === "agent",
    saveCredential: persistCredential,
  };
}

function bindTerminalSocket(tab: TerminalTab, socket: WebSocket) {
  socket.onmessage = (event) => {
    if (tab.socket === socket) handleSocketMessage(tab, event);
  };
  socket.onerror = () => {
    if (tab.socket !== socket) return;
    if (!tab.socketReady && scheduleAutomaticReconnect(tab)) return;
    updateTab(tab, "error", "WebSocket 通道建立失败");
    tab.terminal?.writeln("\r\n\x1b[31mWebSocket 通道建立失败，请检查访问地址、客户端证书或后端状态。\x1b[0m");
    showReconnectHint(tab);
  };
  socket.onclose = (event) => {
    if (tab.socket !== socket) return;
    if (!tab.socketReady && scheduleAutomaticReconnect(tab)) return;
    if (tab.status !== "closed" && tab.status !== "error") {
      const reason = event.reason ? `：${event.reason}` : "";
      updateTab(tab, "closed", `连接已关闭（${event.code}）${reason}`);
    }
    showReconnectHint(tab);
  };
}

function handleTerminalReady(tab: TerminalTab, event: WebTerminalReadyEvent) {
  const { terminal, element } = event;
  tab.terminal = terminal;
  tab.terminalView = event.handle;
  terminal.attachCustomKeyEventHandler((event) => {
    // F12 belongs to the browser (Chrome DevTools). Returning false prevents
    // xterm from translating it to ESC[24~ without canceling Chrome's default.
    if (event.key === "F12") return false;
    if (event.type !== "keydown" || (!event.ctrlKey && !event.metaKey)) return true;
    const key = event.key.toLowerCase();
    if (key === "f") {
      if (!event.defaultPrevented) toggleSearch();
      event.preventDefault();
      return false;
    }
    if (key === "c" && terminal.hasSelection()) {
      event.preventDefault();
      const selection = terminal.getSelection();
      if (selection) {
        void writeClipboard(selection).then((success) => {
          if (!success) warnClipboardAccess("浏览器不允许复制终端选区，请检查站点剪贴板权限");
        });
      }
      return false;
    }
    return true;
  });
  setupTerminalClipboard(tab, terminal, element);
  sendResize(tab);
}

function handleTerminalData(tab: TerminalTab, data: string) {
  if (tab.status === "closed" || tab.status === "error") {
    reconnectTab(tab);
    return;
  }
  if (tab.socket.readyState === WebSocket.OPEN) {
    tab.socket.send(new TextEncoder().encode(applyTerminalModifiers(tab, data)));
  }
}

function applyTerminalModifiers(tab: TerminalTab, data: string) {
  const useCtrl = tab.ctrlModifier;
  const useAlt = tab.altModifier;
  tab.ctrlModifier = false;
  tab.altModifier = false;
  const result = useCtrl ? controlCharacter(data) : data;
  return useAlt ? `\x1b${result}` : result;
}

function controlCharacter(data: string) {
  if (data.length !== 1) return data;
  if (data === "/") return "\x1f";
  if (data === "?") return "\x7f";
  const code = data.toUpperCase().charCodeAt(0);
  return code >= 64 && code <= 95 ? String.fromCharCode(code - 64) : data;
}

function toggleTerminalModifier(tab: TerminalTab, modifier: TerminalModifier) {
  if (modifier === "ctrl") tab.ctrlModifier = !tab.ctrlModifier;
  else tab.altModifier = !tab.altModifier;
  tab.terminal?.focus();
}

function sendSpecialKey(tab: TerminalTab, key: TerminalSpecialKey) {
  if (tab.socket.readyState !== WebSocket.OPEN || tab.status !== "connected") return;
  const useCtrl = tab.ctrlModifier;
  const useAlt = tab.altModifier;
  tab.ctrlModifier = false;
  tab.altModifier = false;

  const arrowFinal = ({
    arrowLeft: "D",
    arrowDown: "B",
    arrowUp: "A",
    arrowRight: "C",
  } as Partial<Record<TerminalSpecialKey, string>>)[key];
  let data = "";
  if (arrowFinal) {
    if (useCtrl || useAlt) {
      const modifier = useCtrl && useAlt ? 7 : useCtrl ? 5 : 3;
      data = `\x1b[1;${modifier}${arrowFinal}`;
    } else {
      data = tab.terminal?.modes.applicationCursorKeysMode ? `\x1bO${arrowFinal}` : `\x1b[${arrowFinal}`;
    }
  } else {
    data = ({
      escape: "\x1b",
      tab: "\t",
      home: "\x1b[H",
      end: "\x1b[F",
      pageUp: "\x1b[5~",
      pageDown: "\x1b[6~",
      pipe: "|",
      slash: "/",
      dash: "-",
      tilde: "~",
    } as Partial<Record<TerminalSpecialKey, string>>)[key] || "";
    if (useCtrl) data = controlCharacter(data);
    if (useAlt) data = `\x1b${data}`;
  }
  if (data) tab.socket.send(new TextEncoder().encode(data));
  tab.terminal?.focus();
}

function handleTerminalResize(tab: TerminalTab, event: WebTerminalResizeEvent) {
  if (tab.socket.readyState !== WebSocket.OPEN) return;
  tab.socket.send(JSON.stringify({ type: "resize", columns: event.columns, rows: event.rows }));
}

function updateSearchResults(tab: TerminalTab, event: WebTerminalSearchResult) {
  const reactiveTab = tabs.value.find((candidate) => candidate.id === tab.id);
  if (!reactiveTab) return;
  reactiveTab.searchResultIndex = event.resultIndex;
  reactiveTab.searchResultCount = event.resultCount;
  reactiveTab.searchResultLimited = event.limited;
  if (reactiveTab.id === activeTabId.value && !searchIndexEditing.value) {
    searchTargetIndex.value = event.resultIndex >= 0 ? event.resultIndex + 1 : null;
  }
}

function setupTerminalClipboard(tab: TerminalTab, terminal: Terminal, element: HTMLElement) {
  tab.clipboardCleanup = attachTerminalClipboard(terminal, element, {
    copyOnSelect: () => terminalSettings.copyOnSelect,
    pasteOnRightClick: () => terminalSettings.pasteOnRightClick,
    canPaste: () => tab.socket.readyState === WebSocket.OPEN && tab.status === "connected",
    canReadClipboard: canReadClipboardAutomatically,
    onCopyError: () => warnClipboardAccess("浏览器不允许自动写入剪贴板，请检查站点权限"),
    onPasteUnavailable: () => {
      terminal.focus();
      if (!clipboardFallbackHintShown) {
        clipboardFallbackHintShown = true;
        message.info("剪贴板读取尚未授权或终端未连接，已保留原生右键菜单");
      }
    },
    onPasteError: (error) => {
      markClipboardReadFailed();
      message.error(`${describeTerminalClipboardError(error)}；下次右键将显示原生菜单`);
    },
  });
}

function warnClipboardAccess(content: string) {
  if (clipboardWarningShown) return;
  clipboardWarningShown = true;
  message.warning(content);
}

async function requestClipboardAccess() {
  try {
    const state = await requestClipboardPermission();
    clipboardFallbackHintShown = false;
    message.success(state === "granted"
      ? "剪贴板读取权限已允许，右键可以自动粘贴"
      : "剪贴板读取成功；此浏览器仍会使用原生右键菜单按次粘贴");
  } catch (error) {
    await refreshClipboardPermission();
    message.error(describeTerminalClipboardError(error));
  }
}

function handleSocketMessage(tab: TerminalTab, event: MessageEvent) {
  if (event.data instanceof ArrayBuffer) {
    const bytes = new Uint8Array(event.data);
    tab.terminal?.write(bytes);
    const recordingLimit = terminalSettings.recordingMaxMiB * 1024 * 1024;
    if (tab.recording && tab.recordingSizeBytes < recordingLimit) {
      if (tab.recordingSizeBytes + bytes.byteLength <= recordingLimit) {
        const decoded = tab.recordingDecoder?.decode(bytes, { stream: true }) ?? new TextDecoder().decode(bytes);
        tab.recordingContent += decoded;
        tab.recordingEntries.push({ at: new Date(), data: decoded });
        tab.recordingSizeBytes += bytes.byteLength;
      } else if (!tab.recordingLimitReached) {
        tab.recordingLimitReached = true;
        tab.recordingSizeBytes = recordingLimit;
        message.warning(`录制缓冲区已达到 ${terminalSettings.recordingMaxMiB} MiB 上限，已停止追加`);
      }
    }
    return;
  }
  const payload = JSON.parse(String(event.data)) as Record<string, unknown>;
  if (payload.type === "ready") {
    tab.socketReady = true;
    tab.connectPayload.columns = tab.terminal?.cols ?? 100;
    tab.connectPayload.rows = tab.terminal?.rows ?? 30;
    tab.socket.send(JSON.stringify(tab.connectPayload));
    tab.connectPayload = {};
  } else if (payload.type === "status") {
    updateTab(tab, String(payload.status) as ConnectionStatus, String(payload.message ?? ""));
    if (payload.status === "connected") {
      if (tab.rememberCredential && tab.pendingCredential) credentialCache.set(tab.host.id, { ...tab.pendingCredential });
      if (tab.persistCredential) {
        const host = hosts.value.find((item) => item.id === tab.host.id);
        if (host) host.hasCredential = true;
      }
      tab.terminal?.focus();
    }
  } else if (payload.type === "host-key") {
    fingerprintRequest.value = {
      tabId: tab.id,
      host: String(payload.host),
      port: Number(payload.port),
      fingerprint: String(payload.fingerprint),
    };
    showFingerprint.value = true;
  } else if (payload.type === "warning") {
    message.warning(String(payload.message ?? "SSH 操作未完全成功"));
  } else if (payload.type === "host-key-mismatch" || payload.type === "error") {
    const errorText = String(payload.message ?? "SSH 连接失败");
    if (payload.type === "error" && isTransientSshError(errorText) && scheduleAutomaticReconnect(tab)) return;
    credentialCache.delete(tab.host.id);
    if (tab.usedStoredCredential && /authentication|认证/i.test(errorText)) {
      void deleteSshCredential(tab.host.id).then(() => {
        const host = hosts.value.find((item) => item.id === tab.host.id);
        if (host) host.hasCredential = false;
      });
    }
    updateTab(tab, "error", errorText);
    tab.terminal?.writeln(`\r\n\x1b[31m${errorText}\x1b[0m`);
    showReconnectHint(tab);
  }
}

function isTransientSshError(content: string) {
  return /无法连接 SSH 主机|DNS 解析超时|TCP 连接超时|SSH 握手(?:失败|超时)|socket|Unable to exchange encryption keys|Failure establishing SSH session/i.test(content);
}

function scheduleAutomaticReconnect(tab: TerminalTab) {
  if (tab.automaticRetryCount >= 1 || !tabs.value.some((item) => item.id === tab.id)) return false;
  tab.automaticRetryCount += 1;
  const previousSocket = tab.socket;
  previousSocket.onopen = null;
  previousSocket.onmessage = null;
  previousSocket.onerror = null;
  previousSocket.onclose = null;
  if (previousSocket.readyState === WebSocket.OPEN || previousSocket.readyState === WebSocket.CONNECTING) previousSocket.close();
  tab.socketReady = false;
  tab.connectPayload = buildConnectPayload(
    tab.host,
    tab.pendingCredential ? { ...tab.pendingCredential } : { method: "stored", password: "", privateKey: "", passphrase: "" },
    tab.persistCredential,
  );
  updateTab(tab, "connecting", "正在建立连接…");
  window.setTimeout(() => {
    if (!tabs.value.some((item) => item.id === tab.id) || tab.socket !== previousSocket) return;
    const socket = createTerminalSocket();
    tab.socket = socket;
    bindTerminalSocket(tab, socket);
  }, 250);
  return true;
}

function showReconnectHint(tab: TerminalTab) {
  if (tab.reconnectHintShown) return;
  tab.reconnectHintShown = true;
  tab.terminal?.writeln("\r\n\x1b[33m连接已中断，按任意键重连…\x1b[0m");
}

function answerFingerprint(trusted: boolean) {
  const request = fingerprintRequest.value;
  const tab = tabs.value.find((item) => item.id === request?.tabId);
  if (tab?.socket.readyState === WebSocket.OPEN) tab.socket.send(JSON.stringify({ type: "trust-host", trusted }));
  if (trusted && tab) {
    const host = hosts.value.find((item) => item.id === tab.host.id);
    if (host && request) host.hostKeySha256 = request.fingerprint;
  }
  showFingerprint.value = false;
  fingerprintRequest.value = null;
}

function sendResize(tab: TerminalTab) {
  if (tab.socket.readyState === WebSocket.OPEN && tab.terminal) {
    tab.socket.send(JSON.stringify({ type: "resize", columns: tab.terminal.cols, rows: tab.terminal.rows }));
  }
}

function updateTab(tab: TerminalTab, status: ConnectionStatus, statusMessage: string) {
  const reactiveTab = tabs.value.find((item) => item.id === tab.id);
  if (!reactiveTab) return;
  reactiveTab.status = status;
  reactiveTab.message = statusMessage;
  if (status !== "connected") {
    reactiveTab.ctrlModifier = false;
    reactiveTab.altModifier = false;
  }
}

function activateTab(id: string) {
  activeTabId.value = id;
  if (activePane.value === "sftp" && !openedSftpTabIds.value.includes(id)) openedSftpTabIds.value.push(id);
  nextTick(() => {
    const tab = tabs.value.find((item) => item.id === id);
    if (activePane.value === "terminal") {
      tab?.terminalView?.fit();
      tab?.terminal?.focus();
      if (tab) sendResize(tab);
      if (tab && showSearch.value && searchQuery.value) searchTerminal(true, true);
    }
  });
}

function handleTabDragStart(event: DragEvent, id: string) {
  draggedTabId.value = id;
  if (event.dataTransfer) {
    event.dataTransfer.effectAllowed = "move";
    event.dataTransfer.setData("text/plain", id);
  }
}

function handleTabListWheel(event: WheelEvent) {
  const element = event.currentTarget as HTMLElement;
  if (element.scrollWidth <= element.clientWidth || Math.abs(event.deltaX) >= Math.abs(event.deltaY)) return;
  event.preventDefault();
  element.scrollLeft += event.deltaY;
}

function handleTabListDragOver(event: DragEvent) {
  const element = event.currentTarget as HTMLElement;
  const bounds = element.getBoundingClientRect();
  const edgeSize = Math.min(48, bounds.width / 4);
  if (event.clientX < bounds.left + edgeSize) element.scrollLeft -= 18;
  else if (event.clientX > bounds.right - edgeSize) element.scrollLeft += 18;
}

function handleTabDrop(event: DragEvent, targetId: string) {
  const sourceId = draggedTabId.value || event.dataTransfer?.getData("text/plain") || "";
  if (!sourceId || sourceId === targetId) return;
  const sourceIndex = tabs.value.findIndex((tab) => tab.id === sourceId);
  if (sourceIndex < 0) return;
  const targetElement = event.currentTarget as HTMLElement;
  const targetBounds = targetElement.getBoundingClientRect();
  const insertAfter = event.clientX > targetBounds.left + targetBounds.width / 2;
  const [sourceTab] = tabs.value.splice(sourceIndex, 1);
  let targetIndex = tabs.value.findIndex((tab) => tab.id === targetId);
  if (insertAfter) targetIndex += 1;
  tabs.value.splice(Math.max(0, targetIndex), 0, sourceTab);
  draggedTabId.value = "";
}

function showTerminalPane() {
  activePane.value = "terminal";
  nextTick(() => {
    const tab = activeTab.value;
    tab?.terminalView?.fit();
    tab?.terminal?.focus();
    if (tab) sendResize(tab);
  });
}

function showSftpPane() {
  const tab = activeTab.value;
  if (!tab) return;
  if (!openedSftpTabIds.value.includes(tab.id)) openedSftpTabIds.value.push(tab.id);
  activePane.value = "sftp";
}

function reconnectTab(tab: TerminalTab) {
  const credential = tab.pendingCredential ? { ...tab.pendingCredential } : { method: "stored", password: "", privateKey: "", passphrase: "" };
  const previousSocket = tab.socket;
  previousSocket.onopen = null;
  previousSocket.onmessage = null;
  previousSocket.onerror = null;
  previousSocket.onclose = null;
  if (previousSocket.readyState === WebSocket.OPEN || previousSocket.readyState === WebSocket.CONNECTING) previousSocket.close();
  tab.connectPayload = buildConnectPayload(tab.host, credential, tab.persistCredential);
  tab.usedStoredCredential = credential.method === "stored";
  tab.reconnectHintShown = false;
  tab.socketReady = false;
  tab.automaticRetryCount = 0;
  updateTab(tab, "connecting", "正在重新连接…");
  tab.terminal?.writeln("\r\n\x1b[36m正在重新连接，文件传输状态将继续保留…\x1b[0m");
  const socket = createTerminalSocket();
  tab.socket = socket;
  bindTerminalSocket(tab, socket);
}

function searchTerminal(previous: boolean, incremental = false) {
  if (searchInputTimer) {
    window.clearTimeout(searchInputTimer);
    searchInputTimer = undefined;
  }
  const tab = activeTab.value;
  if (!tab || !searchQuery.value) return;
  tab.terminalView?.search(
    searchQuery.value,
    previous,
    incremental,
    searchCaseSensitive.value,
    searchWholeWord.value,
    searchRegex.value,
  );
}

function openSearch() {
  showSearch.value = true;
  searchTargetIndex.value = activeTab.value?.searchResultIndex !== undefined && activeTab.value.searchResultIndex >= 0
    ? activeTab.value.searchResultIndex + 1
    : null;
  nextTick(() => {
    searchInput.value?.focus();
    if (searchQuery.value) searchTerminal(true, true);
  });
}

function closeSearch() {
  if (searchInputTimer) {
    window.clearTimeout(searchInputTimer);
    searchInputTimer = undefined;
  }
  showSearch.value = false;
  tabs.value.forEach((tab) => {
    tab.terminalView?.clearSearch();
    resetSearchResults(tab);
  });
  activeTab.value?.terminal?.focus();
}

function toggleSearch() {
  if (showSearch.value) closeSearch();
  else openSearch();
}

function resetSearchResults(tab?: TerminalTab) {
  if (!tab) return;
  tab.searchResultIndex = -1;
  tab.searchResultCount = 0;
  tab.searchResultLimited = false;
  if (tab.id === activeTabId.value) searchTargetIndex.value = null;
}

function jumpToSearchIndex() {
  const tab = activeTab.value;
  const target = Math.trunc(Number(searchTargetIndex.value));
  if (!tab || !Number.isFinite(target) || target < 1 || target > tab.searchResultCount) {
    message.warning(tab?.searchResultCount ? `请输入 1 到 ${tab.searchResultCount} 之间的序号` : "当前没有搜索结果");
    return;
  }
  if (!tab.terminalView?.jumpToSearchIndex(target - 1)) {
    message.warning("搜索索引尚未准备完成，请稍后重试");
  }
}

function searchCountText(tab: TerminalTab) {
  const current = tab.searchResultIndex >= 0 ? String(tab.searchResultIndex + 1) : tab.searchResultCount ? "?" : "0";
  const total = tab.searchResultLimited ? `${searchHighlightLimit}+` : String(tab.searchResultCount);
  return `${current}/${total}`;
}

function handleGlobalShortcut(event: KeyboardEvent) {
  if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "f" && activeTab.value && activePane.value === "terminal") {
    event.preventDefault();
    toggleSearch();
  }
}

function updateRenderer(tab: TerminalTab, renderer: TerminalRenderer) {
  const reactiveTab = tabs.value.find((item) => item.id === tab.id);
  if (reactiveTab) reactiveTab.renderer = renderer;
}

function toggleRecording(tab: TerminalTab) {
  if (!tab.recording) {
    recordingTargetId.value = tab.id;
    Object.assign(recordingDraft, { stripAnsi: true, timestamps: false });
    showRecordingOptions.value = true;
    return;
  }
  tab.recording = false;
  const decoderTail = tab.recordingDecoder?.decode() ?? "";
  if (decoderTail) {
    tab.recordingContent += decoderTail;
    tab.recordingEntries.push({ at: new Date(), data: decoderTail });
  }
  const body = tab.recordingTimestamps
    ? formatTimestampedRecording(tab.recordingEntries, tab.recordingStripAnsi)
    : normalizeRecordingText(tab.recordingStripAnsi ? stripAnsi(tab.recordingContent) : tab.recordingContent);
  const startedAt = tab.recordingStartedAt ? formatRecordingTime(new Date(tab.recordingStartedAt)) : "";
  const content = `# ${tab.host.name} ${startedAt}\n${body}`;
  const filenameTime = formatFilenameTimestamp(tab.recordingStartedAt ? new Date(tab.recordingStartedAt) : new Date());
  downloadText(`${safeFilename(tab.host.name)}-${filenameTime}.log`, content, "text/plain");
  tab.recordingContent = "";
  tab.recordingEntries = [];
  tab.recordingDecoder = undefined;
  tab.recordingSizeBytes = 0;
  tab.recordingLimitReached = false;
  message.success("录制已停止并下载");
}

function startRecording() {
  const tab = tabs.value.find((item) => item.id === recordingTargetId.value);
  if (!tab) return;
  tab.recording = true;
  tab.recordingStartedAt = new Date().toISOString();
  tab.recordingContent = "";
  tab.recordingEntries = [];
  tab.recordingDecoder = new TextDecoder();
  tab.recordingStripAnsi = recordingDraft.stripAnsi;
  tab.recordingTimestamps = recordingDraft.timestamps;
  tab.recordingSizeBytes = 0;
  tab.recordingLimitReached = false;
  showRecordingOptions.value = false;
  message.success("会话录制已开始");
}

function stripAnsi(value: string) {
  return value
    .replace(/\x1B(?:\[[0-?]*[ -/]*[@-~]|\][^\x07]*(?:\x07|\x1B\\)|[@-_])/g, "")
    .replace(/\x9B[0-?]*[ -/]*[@-~]/g, "")
    .replace(/[\x00-\x08\x0B\x0C\x0E-\x1A\x1C-\x1F\x7F]/g, "");
}

function formatTimestampedRecording(entries: Array<{ at: Date; data: string }>, filterAnsi: boolean) {
  let output = "";
  let pending = "";
  let pendingTime = "";
  for (const entry of entries) {
    const value = normalizeRecordingText(filterAnsi ? stripAnsi(entry.data) : entry.data);
    const parts = value.split("\n");
    for (let index = 0; index < parts.length; ++index) {
      if (!pendingTime && parts[index]) pendingTime = formatRecordingTime(entry.at);
      pending += parts[index];
      if (index < parts.length - 1) {
        output += `${pendingTime ? `[${pendingTime}] ` : ""}${pending}\n`;
        pending = "";
        pendingTime = "";
      }
    }
  }
  if (pending) output += `${pendingTime ? `[${pendingTime}] ` : ""}${pending}`;
  return output;
}

function formatRecordingTime(value: Date) {
  const pad = (part: number, length = 2) => String(part).padStart(length, "0");
  return `${value.getFullYear()}-${pad(value.getMonth() + 1)}-${pad(value.getDate())} ${pad(value.getHours())}:${pad(value.getMinutes())}:${pad(value.getSeconds())}.${pad(value.getMilliseconds(), 3)}`;
}

function formatFilenameTimestamp(value: Date) {
  const pad = (part: number, length = 2) => String(part).padStart(length, "0");
  const offsetMinutes = -value.getTimezoneOffset();
  const offsetSign = offsetMinutes >= 0 ? "+" : "-";
  const offsetHours = Math.floor(Math.abs(offsetMinutes) / 60);
  const offsetRemainder = Math.abs(offsetMinutes) % 60;
  return `${value.getFullYear()}-${pad(value.getMonth() + 1)}-${pad(value.getDate())}T${pad(value.getHours())}-${pad(value.getMinutes())}-${pad(value.getSeconds())}.${pad(value.getMilliseconds(), 3)}${offsetSign}${pad(offsetHours)}-${pad(offsetRemainder)}`;
}

function normalizeRecordingText(value: string) {
  return value.replace(/\r\n/g, "\n").replace(/\r/g, "");
}

function openTerminalSettings() {
  Object.assign(terminalSettingsDraft, terminalSettings);
  showTerminalSettings.value = true;
  void refreshClipboardPermission();
}

function saveTerminalSettings() {
  terminalSettings.scrollbackLines = clampNumber(terminalSettingsDraft.scrollbackLines, 1000, 500000, 50000);
  terminalSettings.recordingMaxMiB = clampNumber(terminalSettingsDraft.recordingMaxMiB, 1, 500, 50);
  terminalSettings.fontSize = clampNumber(terminalSettingsDraft.fontSize, 10, 28, 14);
  terminalSettings.lineHeight = clampDecimal(terminalSettingsDraft.lineHeight, 1, 2, 1.2);
  terminalSettings.letterSpacing = clampDecimal(terminalSettingsDraft.letterSpacing, 0, 4, 0);
  terminalSettings.showCommandComposer = terminalSettingsDraft.showCommandComposer !== false;
  terminalSettings.copyOnSelect = terminalSettingsDraft.copyOnSelect === true;
  terminalSettings.pasteOnRightClick = terminalSettingsDraft.pasteOnRightClick === true;
  Object.assign(terminalSettingsDraft, terminalSettings);
  Object.assign(terminalSettings, saveTerminalPreferences(terminalSettings));
  tabs.value.forEach((tab) => tab.terminalView?.setAppearance({
    fontSize: terminalSettings.fontSize,
    lineHeight: terminalSettings.lineHeight,
    letterSpacing: terminalSettings.letterSpacing,
  }));
  showTerminalSettings.value = false;
  message.success("终端显示设置已应用；回滚行数将在新终端中生效");
}

function persistSnippets() {
  persistCommandSnippets(snippets.value);
}

function snippetContentsMatch(left: Pick<CommandSnippet, "name" | "command" | "action">, right: Pick<CommandSnippet, "name" | "command" | "action">) {
  return left.name === right.name && left.command === right.command && left.action === right.action;
}

function localShareStatus(snippet: CommandSnippet) {
  const shared = librarySnippets.value.find((item) => item.id === snippet.id);
  if (!shared) return "存入共享";
  return snippetContentsMatch(snippet, shared) ? "已共享" : "更新共享";
}

function sharedImportStatus(snippet: SharedCommandSnippet) {
  const local = snippets.value.find((item) => item.id === snippet.id)
    ?? snippets.value.find((item) => snippetContentsMatch(item, snippet));
  if (!local) return "添加到本地";
  return snippetContentsMatch(local, snippet) ? "已在本地" : "更新本地";
}

async function loadSharedSnippetLibrary(force = false) {
  try {
    await refreshSharedSnippets(force);
  } catch (error) {
    message.error(error instanceof Error ? error.message : "共享片段读取失败");
  }
}

async function storeSnippetInLibrary(snippet: CommandSnippet) {
  const duplicate = librarySnippets.value.find((item) => item.id !== snippet.id && snippetContentsMatch(item, snippet));
  if (duplicate) return message.info(`共享库已有相同片段“${duplicate.name}”`);
  const updating = librarySnippets.value.some((item) => item.id === snippet.id);
  sharedActionId.value = snippet.id;
  try {
    await storeSharedSnippet(snippet);
    message.success(updating ? "共享片段已更新" : "片段已存入共享库");
  } catch (error) {
    message.error(error instanceof Error ? error.message : "共享片段保存失败");
  } finally {
    sharedActionId.value = "";
  }
}

function addSharedSnippetToLocal(snippet: SharedCommandSnippet) {
  const localIndex = snippets.value.findIndex((item) => item.id === snippet.id);
  if (localIndex >= 0) {
    const local = snippets.value[localIndex];
    snippets.value[localIndex] = { ...snippet, pinned: local.pinned !== false };
    message.success("本地片段已更新");
  } else {
    const duplicate = snippets.value.find((item) => snippetContentsMatch(item, snippet));
    if (duplicate) return message.info(`本地已有相同片段“${duplicate.name}”`);
    snippets.value.push({ ...snippet, pinned: true });
    message.success("已添加到本地片段");
  }
  persistSnippets();
}

async function deleteSnippetFromLibrary(id: string) {
  sharedActionId.value = id;
  try {
    await removeSharedSnippet(id);
    message.success("已从共享库删除；本地片段不受影响");
  } catch (error) {
    message.error(error instanceof Error ? error.message : "共享片段删除失败");
  } finally {
    sharedActionId.value = "";
  }
}

function saveSnippet() {
  if (!snippetDraft.name.trim() || !snippetDraft.command.trim()) return message.warning("名称和命令不能为空");
  const value: CommandSnippet = {
    id: editingSnippetId.value || createId(),
    name: snippetDraft.name.trim(),
    command: snippetDraft.command.trim(),
    pinned: snippetDraft.pinned,
    action: snippetDraft.action,
  };
  if (editingSnippetId.value) snippets.value = snippets.value.map((snippet) => snippet.id === editingSnippetId.value ? value : snippet);
  else snippets.value.push(value);
  cancelSnippetEdit();
  persistSnippets();
}

function openSnippets() {
  showSnippets.value = true;
}

function editSnippet(snippet: CommandSnippet) {
  editingSnippetId.value = snippet.id;
  Object.assign(snippetDraft, {
    name: snippet.name,
    command: snippet.command,
    pinned: snippet.pinned !== false,
    action: snippet.action,
  });
}

function cancelSnippetEdit() {
  editingSnippetId.value = "";
  Object.assign(snippetDraft, { name: "", command: "", pinned: true, action: "insert" });
}

function deleteSnippet(id: string) {
  snippets.value = snippets.value.filter((item) => item.id !== id);
  if (editingSnippetId.value === id) cancelSnippetEdit();
  persistSnippets();
}

function useSnippet(tab: TerminalTab, snippet: CommandSnippet, closeModal = true) {
  if (snippet.action === "insert") {
    tab.commandDraft = snippet.command;
    if (!terminalSettings.showCommandComposer) {
      terminalSettings.showCommandComposer = true;
      terminalSettingsDraft.showCommandComposer = true;
      saveTerminalPreferences(terminalSettings);
    }
    if (closeModal) showSnippets.value = false;
    nextTick(() => tab.terminalView?.fit());
    return;
  }
  if (tab.socket.readyState !== WebSocket.OPEN || tab.status !== "connected") return message.warning("SSH 尚未连接");
  tab.socket.send(new TextEncoder().encode(`${snippet.command}\n`));
  rememberCommand(tab, snippet.command);
  if (closeModal) showSnippets.value = false;
  tab.terminal?.focus();
}

function toggleQuickSnippets() {
  showQuickSnippets.value = !showQuickSnippets.value;
  localStorage.setItem("ssh-show-quick-snippets", String(showQuickSnippets.value));
  nextTick(() => {
    activeTab.value?.terminalView?.fit();
  });
}

function toggleCommandComposer() {
  terminalSettings.showCommandComposer = !terminalSettings.showCommandComposer;
  terminalSettingsDraft.showCommandComposer = terminalSettings.showCommandComposer;
  saveTerminalPreferences(terminalSettings);
  nextTick(() => activeTab.value?.terminalView?.fit());
}

function sendCommand(tab: TerminalTab) {
  const command = tab.commandDraft.trimEnd();
  if (!command) return;
  if (tab.socket.readyState !== WebSocket.OPEN || tab.status !== "connected") {
    message.warning("SSH 尚未连接");
    return;
  }
  tab.socket.send(new TextEncoder().encode(`${command}\n`));
  rememberCommand(tab, command);
  tab.commandDraft = "";
  tab.terminal?.focus();
}

function rememberCommand(tab: TerminalTab, command: string) {
  tab.commandHistory = [command, ...tab.commandHistory.filter((item) => item !== command)].slice(0, 30);
}

function commandHistoryOptions(tab: TerminalTab) {
  return tab.commandHistory.map((command, index) => ({
    key: String(index),
    label: command.replace(/\s+/g, " ").slice(0, 64),
  }));
}

function selectCommandHistory(tab: TerminalTab, key: string | number) {
  const command = tab.commandHistory[Number(key)];
  if (command !== undefined) tab.commandDraft = command;
}

function handleCommandKeydown(tab: TerminalTab, event: KeyboardEvent) {
  if ((event.ctrlKey || event.metaKey) && event.key === "Enter") {
    event.preventDefault();
    sendCommand(tab);
  }
}

function exportConfiguration() {
  downloadText("space-station-ssh.json", JSON.stringify({ version: 1, hosts: hosts.value, snippets: snippets.value, terminalSettings }, null, 2), "application/json");
}

async function importConfiguration(event: Event) {
  const input = event.target as HTMLInputElement;
  const file = input.files?.[0];
  if (!file) return;
  try {
    const data = JSON.parse(await file.text()) as { hosts?: SshHost[]; snippets?: CommandSnippet[]; terminalSettings?: Partial<TerminalSettings> };
    if (!Array.isArray(data.hosts)) throw new Error("配置中缺少主机列表");
    const importedHosts = data.hosts.map((host) => ({ ...host, hasCredential: false }));
    await enqueueHostSave(importedHosts);
    hosts.value = importedHosts;
    if (Array.isArray(data.snippets)) {
      snippets.value = data.snippets
        .filter((item) => typeof item.id === "string" && typeof item.name === "string" && typeof item.command === "string")
        .map((item) => ({ ...item, action: item.action === "insert" ? "insert" : "run" }));
    }
    if (data.terminalSettings) {
      terminalSettings.scrollbackLines = clampNumber(data.terminalSettings.scrollbackLines, 1000, 500000, terminalSettings.scrollbackLines);
      terminalSettings.recordingMaxMiB = clampNumber(data.terminalSettings.recordingMaxMiB, 1, 500, terminalSettings.recordingMaxMiB);
      terminalSettings.fontSize = clampNumber(data.terminalSettings.fontSize, 10, 28, terminalSettings.fontSize);
      terminalSettings.lineHeight = clampDecimal(data.terminalSettings.lineHeight, 1, 2, terminalSettings.lineHeight);
      terminalSettings.letterSpacing = clampDecimal(data.terminalSettings.letterSpacing, 0, 4, terminalSettings.letterSpacing);
      if (typeof data.terminalSettings.showCommandComposer === "boolean") terminalSettings.showCommandComposer = data.terminalSettings.showCommandComposer;
      if (typeof data.terminalSettings.copyOnSelect === "boolean") terminalSettings.copyOnSelect = data.terminalSettings.copyOnSelect;
      if (typeof data.terminalSettings.pasteOnRightClick === "boolean") terminalSettings.pasteOnRightClick = data.terminalSettings.pasteOnRightClick;
      Object.assign(terminalSettings, normalizeTerminalPreferences(terminalSettings));
      saveTerminalPreferences(terminalSettings);
    }
    persistSnippets();
    message.success("SSH 配置已导入（凭据不会导入）");
  } catch (error) {
    message.error(error instanceof Error ? error.message : "配置导入失败");
  } finally {
    input.value = "";
  }
}

function safeFilename(value: string) {
  return value.replace(/[\\/:*?"<>|]/g, "_") || "ssh-session";
}

function downloadText(filename: string, content: string, type: string) {
  const url = URL.createObjectURL(new Blob([content], { type }));
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = filename;
  anchor.click();
  URL.revokeObjectURL(url);
}

async function openPortForwards() {
  showPortForwards.value = true;
  if (!forwardDraft.hostId && hosts.value[0]) forwardDraft.hostId = hosts.value[0].id;
  await refreshForwards();
}

async function refreshForwards() {
  try { portForwards.value = (await fetchSshPortForwards()).forwards; }
  catch (error) { message.error(error instanceof Error ? error.message : "端口转发状态读取失败"); }
}

async function startForward() {
  if (!forwardDraft.hostId || !forwardDraft.remoteHost || !forwardDraft.localPort || !forwardDraft.remotePort) {
    message.warning("请填写完整的转发参数");
    return;
  }
  try {
    await startSshPortForward({ ...forwardDraft });
    await new Promise((resolve) => window.setTimeout(resolve, 150));
    await refreshForwards();
  } catch (error) {
    message.error(error instanceof Error ? error.message : "端口转发启动失败");
  }
}

async function stopForward(id: string) {
  await stopSshPortForward(id);
  await refreshForwards();
}

function closeTab(id: string) {
  const index = tabs.value.findIndex((tab) => tab.id === id);
  if (index < 0) return;
  const [tab] = tabs.value.splice(index, 1);
  openedSftpTabIds.value = openedSftpTabIds.value.filter((tabId) => tabId !== id);
  disposeTab(tab);
  if (activeTabId.value === id) {
    activeTabId.value = tabs.value[Math.min(index, tabs.value.length - 1)]?.id ?? "";
    if (activePane.value === "sftp" && activeTabId.value && !openedSftpTabIds.value.includes(activeTabId.value)) {
      openedSftpTabIds.value.push(activeTabId.value);
    }
  }
}

function disposeTab(tab: TerminalTab) {
  tab.clipboardCleanup?.();
  if (tab.socket.readyState === WebSocket.OPEN || tab.socket.readyState === WebSocket.CONNECTING) tab.socket.close();
  if (tab.pendingCredential) {
    tab.pendingCredential.password = "";
    tab.pendingCredential.privateKey = "";
    tab.pendingCredential.passphrase = "";
  }
}
</script>

<style scoped>
:global(html.ssh-page-lock),
:global(body.ssh-page-lock),
:global(body.ssh-page-lock #app),
:global(body.ssh-page-lock .app-shell) { margin: 0; overflow: hidden; background: #101418; }
.ssh-app { height: 100dvh; min-height: 0; display: grid; grid-template-columns: 300px minmax(0, 1fr); overflow: hidden; background: #101418; color: #d8dee9; }
.ssh-sidebar { box-sizing: border-box; min-width: 0; min-height: 0; height: 100%; padding: 14px; display: flex; flex-direction: column; gap: 14px; overflow: hidden; border-right: 1px solid #27313a; background: #171d22; }
.ssh-brand-row { display: grid; grid-template-columns: 38px minmax(0, 1fr) auto; align-items: center; gap: 10px; }
.ssh-brand-row strong, .ssh-brand-row small { display: block; }
.ssh-brand-row small { margin-top: 2px; color: #7f8d99; font-size: 11px; }
.ssh-home { width: 38px; height: 38px; display: grid; place-items: center; border-radius: 8px; background: #79a8a5; color: #101418; font-weight: 900; text-decoration: none; }
.ssh-host-scroll { min-height: 0; flex: 1 1 0; }
.ssh-host-list { min-height: 100%; padding-right: 9px; display: flex; flex-direction: column; gap: 12px; }
.ssh-host-section { display: grid; gap: 5px; }
.ssh-host-section-title { width: 100%; min-height: 24px; padding: 0 5px; display: flex; align-items: center; justify-content: space-between; border: 0; border-radius: 5px; background: transparent; color: #a7b5be; font-size: 11px; font-weight: 800; letter-spacing: .04em; cursor: pointer; }
.ssh-host-section-title:hover { background: #202930; color: #d4dee4; }
.ssh-host-section-title small { min-width: 22px; padding: 1px 6px; border-radius: 999px; background: #26323a; color: #8797a2; font-size: 10px; text-align: center; }
.ssh-host-section-meta { display: flex; align-items: center; gap: 4px; }
.ssh-host-section-chevron { transition: transform .16s ease; }
.ssh-host-section-chevron.collapsed { transform: rotate(-90deg); }
.ssh-host-section-items { display: grid; gap: 5px; }
.ssh-host { width: 100%; flex: 0 0 auto; display: grid; grid-template-columns: minmax(0, 1fr) 28px 30px; align-items: center; gap: 4px; border-radius: 7px; background: transparent; color: inherit; }
.ssh-host:hover { background: #222b32; }
.ssh-host-main { min-width: 0; padding: 9px; display: grid; grid-template-columns: 34px minmax(0, 1fr); align-items: center; gap: 9px; border: 0; background: transparent; color: inherit; text-align: left; cursor: pointer; }
.ssh-host-icon { width: 34px; height: 34px; display: grid; place-items: center; border-radius: 7px; background: #293740; color: #9fc4c2; font-weight: 800; }
.ssh-host-copy { min-width: 0; }
.ssh-host-copy strong, .ssh-host-copy small { display: block; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.ssh-host-copy small { margin-top: 3px; color: #83919c; font-size: 11px; }
.ssh-edit-button { color: #c8d4dc; background: #2b3740; }
.ssh-edit-button:hover { color: #101418; background: #9bc7c4; }
.ssh-favorite-button { width: 28px; height: 28px; padding: 0; display: grid; place-items: center; border: 0; border-radius: 999px; background: transparent; color: #697984; cursor: pointer; }
.ssh-favorite-button:hover { background: #303c45; color: #d6b35e; }
.ssh-favorite-button.active { color: #e0b84e; }
.ssh-empty { padding: 28px 8px; display: grid; justify-items: center; gap: 8px; color: #7f8d99; font-size: 13px; }
.ssh-workspace { position: relative; min-width: 0; min-height: 0; display: grid; grid-template-rows: 42px minmax(0, 1fr); }
.ssh-tabs { min-width: 0; display: grid; grid-template-columns: minmax(0, 1fr) auto; align-items: stretch; border-bottom: 1px solid #27313a; background: #151a1f; overflow: hidden; }
.ssh-tabs :deep(.n-button--secondary) { color: #e4edf2; background: #34434e; border-color: #536570; }
.ssh-tabs :deep(.n-button--secondary:hover) { color: #101418; background: #9bc7c4; }
.ssh-tab-list { min-width: 0; display: flex; overflow-x: auto; overscroll-behavior-x: contain; scrollbar-width: none; }
.ssh-tab-list::-webkit-scrollbar, .ssh-tab-actions::-webkit-scrollbar { display: none; }
.ssh-tab-actions { max-width: 70vw; padding: 0 4px; display: flex; align-items: center; gap: 4px; overflow-x: auto; scrollbar-width: none; background: #151a1f; box-shadow: -8px 0 12px #101418aa; }
.ssh-mobile-hosts { display: none; }
.ssh-mobile-more { display: none; }
.ssh-tab { min-width: 130px; max-width: 220px; padding: 0 12px; display: flex; align-items: center; gap: 8px; border: 0; border-right: 1px solid #27313a; border-bottom: 2px solid transparent; background: transparent; color: #8997a2; cursor: pointer; }
.ssh-tab.active { border-bottom-color: #79a8a5; background: #101418; color: #e5e9ef; }
.ssh-tab.dragging { opacity: .45; }
.ssh-tab span:nth-child(2) { min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.ssh-tab-close { margin-left: auto; flex: 0 0 auto; }
.ssh-status-dot { width: 7px; height: 7px; flex: 0 0 auto; border-radius: 50%; background: #87909a; }
.ssh-status-dot.connecting, .ssh-status-dot.authenticating { background: #e4b860; }
.ssh-status-dot.connected { background: #66bd83; }
.ssh-status-dot.error { background: #e06c75; }
.ssh-pane-switch { align-self: center; margin: 0 4px; padding: 2px; display: flex; border: 1px solid #2b3740; border-radius: 6px; background: #101418; }
.ssh-pane-switch button { padding: 3px 9px; border: 0; border-radius: 4px; background: transparent; color: #7f8d99; font-size: 12px; cursor: pointer; }
.ssh-pane-switch button.active { background: #2b3a42; color: #dce4e9; }
.ssh-search-bar { top: 47px; }
.ssh-config-actions { display: flex; flex-wrap: wrap; gap: 6px; margin-top: -7px; }
.ssh-config-actions :deep(.n-button) { color: #d5dfe5; background: #26323a; border-color: #41515d; }
.ssh-config-actions :deep(.n-button:hover) { color: #101418; background: #9bc7c4; }
.ssh-config-actions input { display: none; }
.ssh-mobile-sessions { display: none; }
.ssh-renderer, .ssh-status-text { align-self: center; padding: 2px 6px; border: 1px solid #3c4851; border-radius: 4px; color: #8c9aa4; font: 10px/1.4 monospace; white-space: nowrap; }
.ssh-renderer.webgl { border-color: #3b7256; color: #76cf96; background: #193124; }
.ssh-status-text { max-width: 220px; overflow: hidden; text-overflow: ellipsis; }
.ssh-status-text.connected { border-color: #3b7256; color: #76cf96; background: #193124; }
.ssh-status-text.connecting, .ssh-status-text.authenticating { border-color: #816b35; color: #e4c36e; background: #382e18; }
.ssh-status-text.error { border-color: #814751; color: #f08a95; background: #381d22; }
.ssh-status-text.closed { border-color: #56616a; color: #a8b2ba; background: #252c31; }
.ssh-terminal-pane { min-width: 0; min-height: 0; display: grid; grid-template-rows: minmax(0, 1fr) auto auto; overflow: hidden; }
.ssh-terminal { box-sizing: border-box; min-width: 0; min-height: 0; overflow: hidden; background: #101418; }
.ssh-terminal :deep(.xterm) { touch-action: pan-y; }
.ssh-terminal :deep(.xterm-viewport) { overflow-y: auto !important; overscroll-behavior-y: contain; touch-action: pan-y; -webkit-overflow-scrolling: touch; }
.ssh-welcome { display: grid; place-content: center; justify-items: center; color: #788690; text-align: center; }
.ssh-welcome-mark { color: #79a8a5; font: 700 52px/1 monospace; }
.ssh-welcome h1 { margin: 18px 0 7px; color: #b9c3ca; font-size: 20px; }
.ssh-welcome p { margin: 0; font-size: 13px; }
.ssh-welcome-action { margin-top: 20px; }
.ssh-form-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
.ssh-form-grid--connection { grid-template-columns: 140px 1fr; }
.ssh-dialog-actions { display: flex; align-items: center; gap: 10px; }
.ssh-dialog-spacer { flex: 1; }
.ssh-dialog-actions--end { justify-content: flex-end; }
.ssh-reset-key { margin-left: 8px; }
.ssh-forget-credential { margin-left: 8px; }
.ssh-connect-target { margin: 0 0 16px; color: #637382; font-family: monospace; }
.ssh-field-hint { margin: 7px 0 0; color: #7a8792; font-size: 12px; }
.ssh-persist-credential { margin-left: 18px; }
.ssh-private-key { margin: 12px 0; font-family: monospace; }
.ssh-command-panel { padding: 7px 9px 9px; display: grid; gap: 7px; border-top: 1px solid #34414b; background: #171e23; }
.ssh-command-toolbar { min-width: 0; display: flex; align-items: center; gap: 7px; }
.ssh-command-toolbar :deep(.n-button) { color: #dce6eb; border-color: #50616c; background: #2c3941; }
.ssh-quick-snippets { min-width: 0; flex: 1; display: flex; gap: 6px; overflow-x: auto; scrollbar-width: none; }
.ssh-quick-snippets::-webkit-scrollbar { display: none; }
.ssh-quick-snippets > span { align-self: center; color: #71808b; font-size: 12px; }
.ssh-command-editor { display: grid; grid-template-columns: minmax(0, 1fr) auto auto; align-items: stretch; gap: 8px; }
.ssh-command-editor :deep(textarea) { font-family: "SFMono-Regular", Consolas, monospace; }
.ssh-command-editor :deep(.ssh-history-button) { min-width: 62px; color: #e1e9ed; background: #2c3941; border-color: #596b76; }
.ssh-command-editor :deep(.ssh-history-button:hover) { color: #fff; background: #3a4b55; border-color: #78909d; }
.ssh-command-editor :deep(.ssh-history-button.n-button--disabled) { color: #84939c; background: #242f36; border-color: #3d4c55; opacity: 1; }
.ssh-snippet-editor { display: grid; grid-template-columns: 1fr auto; align-items: center; gap: 10px; }
.ssh-snippet-editor > :nth-child(2) { grid-column: 1 / -1; }
.ssh-snippet-editor-actions { grid-column: 1 / -1; display: flex; justify-content: flex-end; gap: 8px; }
.ssh-snippet-list { margin-top: 16px; display: grid; gap: 7px; max-height: 280px; overflow: auto; scrollbar-width: none; }
.ssh-snippet-list::-webkit-scrollbar { display: none; }
.ssh-snippet-order-hint { margin: 0 0 2px; color: #71808b; font-size: 12px; }
.ssh-shared-snippet-heading { margin-bottom: 10px; display: flex; align-items: center; justify-content: space-between; gap: 12px; }
.ssh-shared-snippet-heading p { margin: 0; color: #687783; font-size: 12px; line-height: 1.5; }
.ssh-shared-snippet-list { max-height: 390px; }
.ssh-snippet-row { position: relative; padding: 8px 10px; display: flex; align-items: center; gap: 8px; border: 1px solid #e3e7ea; border-radius: 6px; }
.ssh-snippet-row--dragging { opacity: .45; }
.ssh-snippet-row--drop-before::before, .ssh-snippet-row--drop-after::after { position: absolute; right: 4px; left: 4px; height: 2px; border-radius: 2px; background: #18a058; content: ""; }
.ssh-snippet-row--drop-before::before { top: -5px; }
.ssh-snippet-row--drop-after::after { bottom: -5px; }
.ssh-snippet-drag-handle { flex: 0 0 auto; width: 34px; height: 34px; padding: 0; border: 0; border-radius: 6px; background: transparent; color: #82909a; font-size: 22px; line-height: 1; cursor: grab; touch-action: none; user-select: none; }
.ssh-snippet-drag-handle:hover { background: #f0f3f5; color: #53636e; }
.ssh-snippet-drag-handle:active { cursor: grabbing; }
.ssh-snippet-content { min-width: 0; flex: 1; display: grid; gap: 4px; border: 0; background: transparent; text-align: left; cursor: pointer; }
.ssh-snippet-title { display: flex; align-items: center; gap: 7px; }
.ssh-snippet-title small { padding: 1px 6px; border-radius: 999px; background: #e8eef1; color: #64737d; font-size: 10px; }
.ssh-snippet-row code { overflow: hidden; color: #687783; text-overflow: ellipsis; white-space: nowrap; }
.ssh-forward-form { margin-top: 14px; display: grid; grid-template-columns: 1fr 130px; gap: 10px; }
.ssh-forward-form > :last-child { grid-column: 2; }
.ssh-forward-list { margin-top: 16px; display: grid; gap: 7px; }
.ssh-forward-row { padding: 9px 10px; display: flex; align-items: center; gap: 10px; border: 1px solid #e3e7ea; border-radius: 6px; }
.ssh-forward-row > div { min-width: 0; flex: 1; }
.ssh-forward-row strong, .ssh-forward-row small { display: block; }
.ssh-forward-row small { margin-top: 3px; color: #687783; }
.ssh-recording-options { display: grid; gap: 12px; }
.ssh-recording-options p { margin: 4px 0 0; color: #687783; font-size: 12px; }
.ssh-settings-recording { margin-top: 18px; }
.ssh-settings-switches { display: grid; gap: 10px; }
.ssh-clipboard-permission { padding-top: 2px; display: flex; align-items: center; justify-content: space-between; gap: 12px; color: #687783; font-size: 12px; }
.ssh-fingerprint { display: grid; grid-template-columns: 70px minmax(0, 1fr); gap: 10px; margin: 18px 0 0; }
.ssh-fingerprint dt { color: #637382; }
.ssh-fingerprint dd { margin: 0; overflow-wrap: anywhere; font-family: monospace; }
:global(.ssh-dialog) { --ssh-dialog-width: min(560px, calc(100vw - 32px)); }
@media (max-width: 760px) {
  .ssh-app {
    position: fixed;
    top: var(--terminal-visual-top, 0px);
    left: var(--terminal-visual-left, 0px);
    width: var(--terminal-visual-width, 100%);
    max-width: var(--terminal-visual-width, 100%);
    height: var(--terminal-visual-height, 100dvh);
    max-height: var(--terminal-visual-height, 100dvh);
    grid-template-columns: minmax(0, 1fr);
    overflow: hidden;
  }
  .ssh-sidebar {
    width: 100%;
    padding:
      calc(10px + env(safe-area-inset-top, 0px))
      calc(10px + env(safe-area-inset-right, 0px))
      calc(10px + env(safe-area-inset-bottom, 0px))
      calc(10px + env(safe-area-inset-left, 0px));
    border-right: 0;
  }
  .ssh-app:not(.ssh-app--terminal-open) .ssh-workspace { display: none; }
  .ssh-app--terminal-open .ssh-sidebar { display: none; }
  .ssh-workspace { box-sizing: border-box; width: 100%; height: 100%; padding-top: env(safe-area-inset-top, 0px); padding-bottom: env(safe-area-inset-bottom, 0px); }
  .ssh-tabs { grid-template-columns: minmax(0, 1fr) auto; }
  .ssh-tab-list .ssh-tab:not(.active) { display: none; }
  .ssh-tab-list .ssh-tab.active { min-width: 0; max-width: none; flex: 1; }
  .ssh-tab-actions { max-width: none; padding-right: max(4px, env(safe-area-inset-right, 0px)); box-shadow: none; }
  .ssh-mobile-hosts {
    display: inline-flex;
    flex: 0 0 auto;
    min-height: 34px;
    margin: 4px;
    border: 1px solid #647985 !important;
    background: #30434e !important;
    color: #f4f9fb !important;
    font-weight: 700;
  }
  .ssh-mobile-hosts:hover, .ssh-mobile-hosts:active {
    border-color: #9bc7c4 !important;
    background: #9bc7c4 !important;
    color: #102027 !important;
  }
  .ssh-mobile-hosts:focus-visible { outline: 2px solid #b9d9d7; outline-offset: 1px; }
  .ssh-mobile-more { display: inline-flex; }
  .ssh-desktop-action { display: none !important; }
  .ssh-status-text { display: none; }
  .ssh-mobile-sessions { padding: 9px; display: grid; gap: 8px; border: 1px solid #34434d; border-radius: 8px; background: #1d262c; }
  .ssh-mobile-sessions-title { display: flex; align-items: center; justify-content: space-between; color: #b9c7cf; font-size: 12px; }
  .ssh-mobile-sessions-title span { min-width: 24px; padding: 1px 7px; border-radius: 999px; background: #30404a; color: #dce6eb; text-align: center; }
  .ssh-mobile-session-list { display: flex; gap: 7px; overflow-x: auto; scrollbar-width: none; }
  .ssh-mobile-session-list::-webkit-scrollbar { display: none; }
  .ssh-mobile-session { flex: 0 0 auto; display: flex; align-items: stretch; overflow: hidden; border: 1px solid #42535e; border-radius: 7px; background: #27343c; }
  .ssh-mobile-session > button { min-height: 34px; padding: 0 9px; display: flex; align-items: center; gap: 7px; border: 0; background: transparent; color: #e0e8ed; }
  .ssh-mobile-session > button:first-child { max-width: 150px; }
  .ssh-mobile-session > button:first-child span:last-child { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .ssh-mobile-session > button:last-child { padding: 0 10px; border-left: 1px solid #42535e; color: #aebbc4; font-size: 18px; }
  .ssh-terminal :deep(.xterm), .ssh-terminal :deep(.xterm-viewport), .ssh-terminal :deep(.xterm-screen) { touch-action: none; }
  .ssh-command-panel { display: none; }
  .ssh-form-grid, .ssh-form-grid--connection { grid-template-columns: 1fr; gap: 0; }
  .ssh-persist-credential { display: flex; margin: 10px 0 0; }
  .ssh-forward-form { grid-template-columns: 1fr; }
  .ssh-forward-form > :last-child { grid-column: 1; }
  .ssh-command-toolbar { flex-wrap: wrap; }
  .ssh-quick-snippets { order: 3; flex-basis: 100%; }
  .ssh-command-editor { grid-template-columns: minmax(0, 1fr) auto 64px; gap: 6px; }
  .ssh-snippet-row { align-items: flex-start; flex-wrap: wrap; }
  .ssh-snippet-row > .ssh-snippet-content { flex-basis: calc(100% - 50px); }
  .ssh-shared-snippet-list .ssh-snippet-content { flex-basis: 100%; }
  :global(.ssh-dialog) {
    --ssh-dialog-width: 100vw;
    max-width: 100vw;
    max-height: 100dvh;
    margin: 0;
    border-radius: 0;
  }
}
</style>
