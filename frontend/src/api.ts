import axios from "axios";

export interface HealthResponse {
  ok: boolean;
  service: string;
  version: string;
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
  reminderTime: string;
  alternative: string;
  archived: boolean;
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

const api = axios.create({
  baseURL: "/api",
  timeout: 10000,
});

export async function fetchHealth(): Promise<HealthResponse> {
  const { data } = await api.get<HealthResponse>("/health");
  return data;
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

export async function downloadSftpFile(
  hostId: string,
  path: string,
  onProgress?: (loaded: number) => void,
): Promise<Blob> {
  const { data } = await api.get<Blob>("/tools/ssh/sftp/download", {
    params: { hostId, path },
    responseType: "blob",
    timeout: 0,
    onDownloadProgress: (event) => onProgress?.(event.loaded),
  });
  return data;
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
