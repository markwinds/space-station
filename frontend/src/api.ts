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
