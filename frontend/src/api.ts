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
  configPath: string;
  logPath: string;
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

export async function updateConfig(payload: Partial<Pick<AppConfig, "dataPath" | "logLevel" | "port">>): Promise<AppConfig> {
  const { data } = await api.put<AppConfig>("/config", payload);
  return data;
}
