import axios from "axios";

export interface HealthResponse {
  ok: boolean;
  service: string;
  version: string;
}

export interface SharedCommandSnippet {
  id: string;
  name: string;
  command: string;
  action: "insert" | "run";
}

export interface AppConfig {
  dataPath: string;
  logLevel: "trace" | "debug" | "info" | "warn" | "error";
  port: number;
  httpEnabled: boolean;
  httpPort: number;
  certificatePath: string;
  privateKeyPath: string;
  trustedRootCertificatePath: string;
  configPath: string;
  logPath: string;
}

export interface CertificateSubject {
  commonName?: string;
  organization?: string;
  organizationalUnit?: string;
  country?: string;
  state?: string;
  locality?: string;
  email?: string;
}

export interface CertificateSan {
  dns?: string[];
  ips?: string[];
  emails?: string[];
  uris?: string[];
}

export interface GenerateCertificateRequest {
  subject: CertificateSubject;
  san: CertificateSan;
  isCa: boolean;
  validDays: number;
  keyAlgorithm: "rsa" | "ec";
  keyBits?: number;
  ecCurve?: string;
  serialNumber?: string;
  keyUsage?: string[];
  extendedKeyUsage?: string[];
}

export interface GenerateCertificateResponse {
  ok: boolean;
  error?: string;
  privateKeyPem: string;
  publicKeyPem: string;
  csrPem: string;
  certificatePem: string;
}

export interface SignCertificateRequest {
  caCertificatePem: string;
  caPrivateKeyPem: string;
  csrPem: string;
  san: CertificateSan;
  validDays: number;
  serialNumber?: string;
  keyUsage?: string[];
  extendedKeyUsage?: string[];
}

export interface SignCertificateResponse {
  ok: boolean;
  error?: string;
  certificatePem: string;
}

export interface ParseCsrRequest {
  csrPem: string;
}

export interface ParsedCsrResponse {
  ok: boolean;
  error?: string;
  subject: CertificateSubject & { raw: string };
  publicKeyAlgorithm: string;
  publicKeyBits: number;
  signatureAlgorithm: string;
  signatureValid: boolean;
  san: Required<CertificateSan>;
  keyUsage: string[];
  extendedKeyUsage: string[];
  keyUsageText: string;
  extendedKeyUsageText: string;
}

export interface CreateP12Request {
  certificatePem: string;
  privateKeyPem: string;
  caCertificatePem?: string;
  password?: string;
  friendlyName?: string;
}

export interface CreateP12Response {
  ok: boolean;
  error?: string;
  filename: string;
  p12Base64: string;
}

export interface ParseP12Request {
  p12Base64: string;
  password?: string;
}

export interface ParseCertificateRequest {
  certificatePem: string;
}

export interface ParsedCertificateInfo {
  version: number;
  serialNumber: string;
  serialNumberHex: string;
  subject: CertificateSubject & { raw: string };
  issuer: CertificateSubject & { raw: string };
  validFrom: string;
  validTo: string;
  signatureAlgorithm: string;
  publicKeyAlgorithm: string;
  publicKeyBits: number;
  isCa: boolean;
  selfSigned: boolean;
  san: Required<CertificateSan>;
  basicConstraints: string;
  keyUsage: string;
  extendedKeyUsage: string;
}

export interface ParsedCertificateResponse extends ParsedCertificateInfo {
  ok: boolean;
  error?: string;
}

export interface ParsedP12Certificate extends ParsedCertificateInfo {
  role: "certificate" | "ca";
}

export interface ParsedP12Response {
  ok: boolean;
  error?: string;
  friendlyName: string;
  hasPrivateKey: boolean;
  privateKeyAlgorithm: string;
  privateKeyBits: number;
  certificateCount: number;
  caCertificateCount: number;
  certificates: ParsedP12Certificate[];
}

export interface TimeManagerTask {
  id: string;
  title: string;
  icon: string;
  parentId: string | null;
  tagIds: string[];
  scheduledAt: string;
  completed: boolean;
  notes: string;
  sortOrder: number;
  createdAt: string;
  updatedAt: string;
}

export interface TimeManagerTag {
  id: string;
  name: string;
  color: string;
}

export type TimeManagerFilterField = "tag" | "completed" | "scheduledAt" | "keyword";
export type TimeManagerFilterOperator = "contains" | "equals" | "before" | "after" | "empty" | "notEmpty";

export interface TimeManagerFilterCondition {
  id: string;
  field: TimeManagerFilterField;
  operator: TimeManagerFilterOperator;
  value: string | boolean;
}

export interface TimeManagerSavedFilter {
  id: string;
  name: string;
  logic: "all" | "any";
  nodeId: string | null;
  conditions: TimeManagerFilterCondition[];
  createdAt: string;
  updatedAt: string;
}

export interface TimeManagerSettings {
  calendarStartHour: number;
  calendarEndHour: number;
  openDetailsOnNodeClick: boolean;
}

export interface TimeManagerState {
  tasks: TimeManagerTask[];
  tags: TimeManagerTag[];
  filters: TimeManagerSavedFilter[];
  settings: TimeManagerSettings;
}

export type HabitKind = "build" | "reduce";
export type HabitScheduleMode = "weekdays" | "weeklyTarget";

export interface Habit {
  id: string;
  title: string;
  kind: HabitKind;
  color: string;
  schedule: {
    mode: HabitScheduleMode;
    weekdays: number[];
    targetPerWeek: number;
  };
  alternative: string;
  archived: boolean;
  archivedAt: string;
  createdAt: string;
  updatedAt: string;
}

export interface HabitLog {
  habitId: string;
  date: string;
  completed: boolean;
  skipped: boolean;
  occurrences: number;
  replacements: number;
  confirmed: boolean;
  note: string;
  updatedAt: string;
}

export interface HabitState {
  version: 2;
  habits: Habit[];
  logs: HabitLog[];
  settings: {
    weekStartsOn: number;
  };
}

export interface FileShare {
  id: string;
  name: string;
  path: string;
  exists?: boolean;
}

export interface FileShareItem {
  name: string;
  path: string;
  type: "directory" | "file" | "symlink";
  size: number;
  modifiedAt: number;
}

export interface SshHost {
  id: string;
  name: string;
  host: string;
  port: number;
  username: string;
  group: string;
  hostKeySha256: string;
  hasCredential?: boolean;
  useAgent?: boolean;
  jumpHostId?: string;
  favorite?: boolean;
  lastUsedAt?: string;
}

export interface SftpItem {
  name: string;
  path: string;
  type: "directory" | "file" | "symlink";
  size: number;
  modifiedAt: number;
}

export interface SftpListResponse {
  path: string;
  parentPath: string;
  items: SftpItem[];
}

export interface SshPortForward {
  id: string;
  hostId: string;
  localPort: number;
  remoteHost: string;
  remotePort: number;
  running: boolean;
  message: string;
}

export interface FileShareListResponse {
  share: FileShare;
  path: string;
  parentPath: string;
  items: FileShareItem[];
}

export interface TransferServerConfig {
  configVersion: number;
  listenAddress: string;
  tlsEnabled: boolean;
  tlsPort: number;
  plainEnabled: boolean;
  plainPort: number;
  certificatePath: string;
  privateKeyPath: string;
  clientCaPath: string;
  destinationRoot: string;
  basisRoots: string[];
  sameNameMatchThreshold: number;
  overwrite: boolean;
}

export interface TransferFileProgress {
  fileId: string;
  path: string;
  stage: "hashing" | "scanning" | "uploading" | "verifying" | "completed" | "failed";
  fileSize: number;
  matchedBytes: number;
  uploadedBytes: number;
  wireBytes: number;
  compressedChunks: number;
  compressionMode: "none" | "chunk" | "stream";
  error: string;
}

export interface TransferJob {
  id: string;
  status: "queued" | "running" | "completed" | "failed";
  error: string;
  files: TransferFileProgress[];
}

export interface TransferState {
  server: TransferServerConfig;
  serverRunning: boolean;
  jobs: TransferJob[];
}

export interface TransferClientRequest {
  host: string;
  port: number;
  tlsEnabled: boolean;
  certificatePath: string;
  privateKeyPath: string;
  serverCaPath: string;
  serverName: string;
  chunkSize: number;
  compressionMode: "chunk" | "stream";
  files: string[];
}

export interface BackendSerialPort {
  id: string;
  name: string;
  path: string;
}

export interface BrowserSerialShare {
  id: string;
  name: string;
  portLabel: string;
  pluginTarget?: string;
  writeEnabled: boolean;
  viewers: number;
}

export interface TerminalPluginManifest {
  id: string;
  name: string;
  enabled: boolean;
  entry: string;
  transports: Array<"serial" | "ssh" | "browser-serial">;
  targets: string[];
  allowedHosts: string[];
  memoryLimitMiB: number;
  executionTimeoutMs: number;
  maxConcurrentRequests: number;
  maxResponseBytes: number;
}

export interface TerminalPluginStatus {
  directory: string;
  id?: string;
  name?: string;
  enabled?: boolean;
  loaded: boolean;
  transports?: string[];
  targets?: string[];
  allowedHosts?: string[];
  error?: string;
}

export interface TerminalPluginListResponse {
  directory: string;
  plugins: TerminalPluginStatus[];
}

export interface EffectiveTerminalPluginResponse {
  transport: string;
  target: string;
  plugins: TerminalPluginStatus[];
}

export interface TerminalPluginDetail {
  manifest: TerminalPluginManifest;
  source: string;
}

export interface AuthenticatorEntry {
  id: string;
  name: string;
  issuer: string;
  account: string;
  algorithm: "SHA1" | "SHA256" | "SHA512";
  digits: 6 | 8;
  period: number;
  code: string;
  remaining: number;
}

export interface AuthenticatorEntryInput {
  id: string;
  name: string;
  issuer: string;
  account: string;
  secret: string;
  algorithm: "SHA1" | "SHA256" | "SHA512";
  digits: 6 | 8;
  period: number;
}

export interface AuthenticatorEntriesResponse {
  entries: AuthenticatorEntry[];
  serverTime: number;
}

const api = axios.create({
  baseURL: "/api",
  timeout: 10000,
});

async function terminalPluginRequest<T>(request: () => Promise<T>): Promise<T> {
  try {
    return await request();
  } catch (error) {
    if (!axios.isAxiosError(error) || error.response) throw error;
    await new Promise((resolve) => window.setTimeout(resolve, 250));
    return request();
  }
}

export async function fetchHealth(): Promise<HealthResponse> {
  const { data } = await api.get<HealthResponse>("/health");
  return data;
}

export async function fetchBackendSerialPorts(): Promise<{ ports: BackendSerialPort[] }> {
  const { data } = await api.get<{ ports: BackendSerialPort[] }>("/tools/serial/ports");
  return data;
}

export async function fetchBrowserSerialShares(): Promise<{ shares: BrowserSerialShare[] }> {
  const { data } = await api.get<{ shares: BrowserSerialShare[] }>("/tools/serial/browser-shares", {
    params: { _: Date.now() },
    headers: { "Cache-Control": "no-cache" },
  });
  return data;
}

export async function fetchTerminalPlugins(): Promise<TerminalPluginListResponse> {
  const { data } = await terminalPluginRequest(() => api.get<TerminalPluginListResponse>("/tools/terminal/plugins", {
    params: { _: Date.now() },
  }));
  return data;
}

export async function fetchEffectiveTerminalPlugins(
  transport: string,
  target: string,
): Promise<EffectiveTerminalPluginResponse> {
  const { data } = await terminalPluginRequest(() => api.get<EffectiveTerminalPluginResponse>(
    "/tools/terminal/plugins/effective",
    { params: { transport, target, _: Date.now() } },
  ));
  return data;
}

export async function fetchTerminalPlugin(pluginId: string): Promise<TerminalPluginDetail> {
  const { data } = await terminalPluginRequest(() => api.get<TerminalPluginDetail>(`/tools/terminal/plugins/${encodeURIComponent(pluginId)}`));
  return data;
}

export async function saveTerminalPlugin(pluginId: string, detail: TerminalPluginDetail): Promise<void> {
  await terminalPluginRequest(() => api.put(`/tools/terminal/plugins/${encodeURIComponent(pluginId)}`, detail));
}

export async function deleteTerminalPlugin(pluginId: string): Promise<void> {
  await terminalPluginRequest(() => api.delete(`/tools/terminal/plugins/${encodeURIComponent(pluginId)}`));
}

export async function reloadTerminalPlugins(): Promise<void> {
  await terminalPluginRequest(() => api.post("/tools/terminal/plugins/reload"));
}

export async function fetchAuthenticatorEntries(): Promise<AuthenticatorEntriesResponse> {
  const { data } = await api.get<AuthenticatorEntriesResponse>("/tools/authenticator/entries");
  return data;
}

export async function saveAuthenticatorEntry(payload: AuthenticatorEntryInput): Promise<AuthenticatorEntry> {
  const { data } = await api.post<{ ok: boolean; entry: AuthenticatorEntry }>("/tools/authenticator/entries", payload);
  return data.entry;
}

export async function deleteAuthenticatorEntry(id: string): Promise<void> {
  await api.delete("/tools/authenticator/entries", { params: { id } });
}

export async function fetchConfig(): Promise<AppConfig> {
  const { data } = await api.get<AppConfig>("/config");
  return data;
}

export async function updateConfig(
  payload: Partial<
    Pick<
      AppConfig,
      | "dataPath"
      | "logLevel"
      | "port"
      | "httpEnabled"
      | "httpPort"
      | "certificatePath"
      | "privateKeyPath"
      | "trustedRootCertificatePath"
    >
  >,
): Promise<AppConfig> {
  const { data } = await api.put<AppConfig>("/config", payload);
  return data;
}

export async function generateCertificateBundle(payload: GenerateCertificateRequest): Promise<GenerateCertificateResponse> {
  const { data } = await api.post<GenerateCertificateResponse>("/certificates/generate", payload);
  return data;
}

export async function signCertificateRequest(payload: SignCertificateRequest): Promise<SignCertificateResponse> {
  const { data } = await api.post<SignCertificateResponse>("/certificates/sign", payload);
  return data;
}

export async function parseCsr(payload: ParseCsrRequest): Promise<ParsedCsrResponse> {
  const { data } = await api.post<ParsedCsrResponse>("/certificates/parse-csr", payload);
  return data;
}

export async function createP12(payload: CreateP12Request): Promise<CreateP12Response> {
  const { data } = await api.post<CreateP12Response>("/certificates/p12", payload);
  return data;
}

export async function parseP12(payload: ParseP12Request): Promise<ParsedP12Response> {
  const { data } = await api.post<ParsedP12Response>("/certificates/parse-p12", payload);
  return data;
}

export async function parseCertificate(payload: ParseCertificateRequest): Promise<ParsedCertificateResponse> {
  const { data } = await api.post<ParsedCertificateResponse>("/certificates/parse", payload);
  return data;
}

export async function fetchTimeManagerState(): Promise<TimeManagerState> {
  const { data } = await api.get<TimeManagerState>("/tools/time-manager/state");
  return data;
}

export async function saveTimeManagerState(payload: TimeManagerState): Promise<void> {
  await api.put("/tools/time-manager/state", payload);
}

export async function fetchHabitState(): Promise<HabitState> {
  const { data } = await api.get<HabitState>("/tools/habits/state");
  return data;
}

export async function saveHabitState(payload: HabitState): Promise<void> {
  await api.put("/tools/habits/state", payload);
}

export async function fetchSharedCommandSnippets(): Promise<SharedCommandSnippet[]> {
  const { data } = await api.get<{ snippets: SharedCommandSnippet[] }>("/tools/terminal/snippets");
  return data.snippets;
}

export async function saveSharedCommandSnippet(snippet: SharedCommandSnippet): Promise<SharedCommandSnippet> {
  const { data } = await api.put<{ snippet: SharedCommandSnippet }>("/tools/terminal/snippets", snippet);
  return data.snippet;
}

export async function deleteSharedCommandSnippet(id: string): Promise<void> {
  await api.delete("/tools/terminal/snippets", { params: { id } });
}

export async function fetchFileShareState(): Promise<{ shares: FileShare[] }> {
  const { data } = await api.get<{ shares: FileShare[] }>("/tools/file-share/state");
  return data;
}

export async function fetchSshHosts(): Promise<{ hosts: SshHost[] }> {
  const { data } = await api.get<{ hosts: SshHost[] }>("/tools/ssh/hosts");
  return data;
}

export async function saveSshHosts(hosts: SshHost[]): Promise<void> {
  await api.put("/tools/ssh/hosts", { hosts });
}

export async function deleteSshCredential(hostId: string): Promise<void> {
  await api.delete("/tools/ssh/credential", { params: { hostId } });
}

export async function listSftp(hostId: string, path = "/"): Promise<SftpListResponse> {
  const { data } = await api.get<SftpListResponse>("/tools/ssh/sftp/list", {
    params: { hostId, path },
    timeout: 30000,
  });
  return data;
}

export async function createSftpFolder(hostId: string, path: string): Promise<void> {
  await api.post("/tools/ssh/sftp/folder", { hostId, path }, { timeout: 30000 });
}

export async function deleteSftpItem(hostId: string, path: string, directory: boolean): Promise<void> {
  await api.delete("/tools/ssh/sftp/item", { data: { hostId, path, directory }, timeout: 30000 });
}

export async function renameSftpItem(hostId: string, from: string, to: string): Promise<void> {
  await api.put("/tools/ssh/sftp/rename", { hostId, from, to }, { timeout: 30000 });
}

export async function uploadSftpFile(
  hostId: string,
  path: string,
  file: File,
  onProgress?: (loaded: number) => void,
): Promise<void> {
  const maximumFileSize = 2 * 1024 * 1024 * 1024;
  const chunkSize = 8 * 1024 * 1024;
  if (file.size > maximumFileSize) throw new Error(`“${file.name}”超过 2 GiB 上传限制`);
  const uploadId = crypto.randomUUID();
  let offset = 0;
  do {
    const end = Math.min(file.size, offset + chunkSize);
    const chunk = file.slice(offset, end);
    const formData = new FormData();
    formData.append("uploadId", uploadId);
    formData.append("hostId", hostId);
    formData.append("path", path);
    formData.append("offset", String(offset));
    formData.append("totalSize", String(file.size));
    formData.append("files", chunk, file.name);
    const chunkStart = offset;
    await api.post("/tools/ssh/sftp/upload", formData, {
      timeout: 300000,
      onUploadProgress: (event) => onProgress?.(Math.min(file.size, chunkStart + Math.min(chunk.size, event.loaded))),
    });
    offset = end;
    onProgress?.(offset);
  } while (offset < file.size);
}

export interface SftpDownloadStatus {
  found: boolean;
  id?: string;
  status?: "queued" | "downloading" | "success" | "error";
  downloadedBytes?: number;
  totalBytes?: number;
  message?: string;
}

export function sftpDownloadUrl(hostId: string, path: string, downloadId: string): string {
  const query = new URLSearchParams({ hostId, path, downloadId });
  return `/api/tools/ssh/sftp/download?${query.toString()}`;
}

export async function streamSftpDownload(
  hostId: string,
  path: string,
  downloadId: string,
  filename: string,
  onProgress?: (loaded: number) => void,
): Promise<void> {
  const response = await fetch(sftpDownloadUrl(hostId, path, downloadId), {
    cache: "no-store",
    credentials: "same-origin",
  });
  if (!response.ok) {
    let detail = `下载请求失败（HTTP ${response.status}）`;
    try {
      const body = await response.json() as { message?: string };
      if (body.message) detail = body.message;
    } catch {
      // Keep the HTTP error when the server did not return JSON.
    }
    throw new Error(detail);
  }
  if (!response.body) throw new Error("浏览器不支持流式下载响应");

  const temporaryName = `space-station-download-${downloadId}`;
  const storage = navigator.storage as StorageManager & {
    getDirectory?: () => Promise<FileSystemDirectoryHandle>;
  };
  let directory: FileSystemDirectoryHandle | undefined;
  let temporaryFile: FileSystemFileHandle | undefined;
  let writer: FileSystemWritableFileStream | undefined;
  try {
    directory = await storage.getDirectory?.();
    if (directory) {
      temporaryFile = await directory.getFileHandle(temporaryName, { create: true });
      writer = await temporaryFile.createWritable();
    }
  } catch {
    directory = undefined;
    temporaryFile = undefined;
    writer = undefined;
  }

  const chunks: BlobPart[] = [];
  const reader = response.body.getReader();
  let loaded = 0;
  try {
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      if (writer) await writer.write(new Blob([value]));
      else chunks.push(value.slice().buffer);
      loaded += value.byteLength;
      onProgress?.(loaded);
    }
    if (writer) await writer.close();
  } catch (error) {
    await writer?.abort().catch(() => undefined);
    if (directory) await directory.removeEntry(temporaryName).catch(() => undefined);
    throw error;
  }

  const blob = temporaryFile ? await temporaryFile.getFile() : new Blob(chunks, { type: "application/octet-stream" });
  const objectUrl = URL.createObjectURL(blob);
  const anchor = document.createElement("a");
  anchor.href = objectUrl;
  anchor.download = filename;
  document.body.appendChild(anchor);
  anchor.click();
  anchor.remove();

  window.setTimeout(() => {
    URL.revokeObjectURL(objectUrl);
    if (directory) void directory.removeEntry(temporaryName).catch(() => undefined);
  }, 60_000);
}

export async function fetchSftpDownloadStatus(downloadId: string): Promise<SftpDownloadStatus> {
  const { data } = await api.get<SftpDownloadStatus>("/tools/ssh/sftp/download/status", {
    params: { downloadId },
    timeout: 30000,
  });
  return data;
}

export async function dismissSftpDownload(downloadId: string): Promise<void> {
  await api.delete("/tools/ssh/sftp/download/status", { params: { downloadId }, timeout: 30000 });
}

export async function fetchSshPortForwards(): Promise<{ forwards: SshPortForward[] }> {
  const { data } = await api.get<{ forwards: SshPortForward[] }>("/tools/ssh/forwards");
  return data;
}

export async function startSshPortForward(payload: { hostId: string; localPort: number; remoteHost: string; remotePort: number }): Promise<void> {
  await api.post("/tools/ssh/forwards", payload);
}

export async function stopSshPortForward(id: string): Promise<void> {
  await api.delete(`/tools/ssh/forwards/${encodeURIComponent(id)}`);
}

export async function saveFileShares(shares: FileShare[]): Promise<void> {
  await api.put("/tools/file-share/shares", { shares });
}

export async function listFileShare(shareId: string, path = ""): Promise<FileShareListResponse> {
  const { data } = await api.get<FileShareListResponse>("/tools/file-share/list", {
    params: { shareId, path },
  });
  return data;
}

export async function uploadFileShareFiles(shareId: string, path: string, files: FileList | File[]): Promise<void> {
  const formData = new FormData();
  formData.append("shareId", shareId);
  formData.append("path", path);
  Array.from(files).forEach((file) => {
    formData.append("files", file);
  });
  await api.post("/tools/file-share/upload", formData, {
    headers: { "Content-Type": "multipart/form-data" },
    timeout: 120000,
  });
}

export async function createFileShareFolder(payload: { shareId: string; path: string; name: string }): Promise<void> {
  await api.post("/tools/file-share/folder", payload);
}

export async function deleteFileShareItem(payload: { shareId: string; path: string }): Promise<void> {
  await api.delete("/tools/file-share/item", { data: payload });
}

export function fileShareDownloadUrl(shareId: string, path: string) {
  const params = new URLSearchParams({ shareId, path });
  return `/api/tools/file-share/download?${params.toString()}`;
}

export async function fetchTransferState(): Promise<TransferState> {
  const { data } = await api.get<TransferState>("/tools/transfer/state");
  return data;
}

export async function saveTransferServerConfig(config: TransferServerConfig): Promise<TransferServerConfig> {
  const { data } = await api.put<TransferServerConfig>("/tools/transfer/server/config", config);
  return data;
}

export async function startTransferServer(): Promise<void> {
  await api.post("/tools/transfer/server/start");
}

export async function stopTransferServer(): Promise<void> {
  await api.post("/tools/transfer/server/stop");
}

export async function startTransferClientJob(config: TransferClientRequest): Promise<{ jobId: string }> {
  const { data } = await api.post<{ jobId: string }>("/tools/transfer/client/jobs", config);
  return data;
}
