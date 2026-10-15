import type { Target } from './target.js';
export class Picoboot {
  static requestDevice(targets: Target[]): Promise<Picoboot>;
  connect(): Promise<{
    resetInterface(): Promise<void>;
    setExclusiveAccess(mode: number): Promise<void>;
    exitXip(): Promise<void>;
    flashErase(address: number, size: number): Promise<void>;
    flashWrite(address: number, data: Uint8Array): Promise<void>;
    flashRead(address: number, size: number): Promise<Uint8Array>;
    reboot(delayMs: number): Promise<void>;
  }>;
  disconnect(): Promise<void>;
}
