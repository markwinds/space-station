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

export interface Habit {
  id: string;
  title: string;
  kind: HabitKind;
  color: string;
  schedule: "daily" | "weekly";
  targetCount: number;
  weekdays: number[];
  reminderTime: string;
  replacementAction: string;
  archived: boolean;
  createdAt: string;
  updatedAt: string;
}

export type HabitRecordStatus = "completed" | "partial" | "skipped" | "occurred" | "replaced";

export interface HabitRecord {
  id: string;
  habitId: string;
  date: string;
  status: HabitRecordStatus;
  count: number;
  note: string;
  createdAt: string;
  updatedAt: string;
}

export interface HabitState {
  habits: Habit[];
  records: HabitRecord[];
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
