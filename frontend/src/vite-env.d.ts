/// <reference types="vite/client" />

interface SerialPortInfo {
  usbVendorId?: number;
  usbProductId?: number;
  bluetoothServiceClassId?: string;
}

interface SerialPort extends EventTarget {
  readonly readable: ReadableStream<Uint8Array> | null;
  readonly writable: WritableStream<Uint8Array> | null;
  getInfo(): SerialPortInfo;
  open(options: {
    baudRate: number;
    dataBits?: 7 | 8;
    stopBits?: 1 | 2;
    parity?: "none" | "even" | "odd";
    flowControl?: "none" | "hardware";
    bufferSize?: number;
  }): Promise<void>;
  close(): Promise<void>;
}

interface Serial extends EventTarget {
  getPorts(): Promise<SerialPort[]>;
  requestPort(options?: { filters?: Array<Record<string, number | string>> }): Promise<SerialPort>;
}

interface Navigator {
  readonly serial?: Serial;
}
