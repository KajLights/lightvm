export type Path = 'usb' | 'http';
export interface Description {
  device_id: string; boot_id: string; board: string; control_abi: number;
  program_schemas: string[]; module_limit: number; config_limit: number;
  pixel_capacity: number; pins: string[]; slots: string[];
  storage_available: boolean; wifi_supported: boolean; wifi_ap_supported?: boolean; http_supported: boolean;
  active_revision: number; saved_revision: number; catalog_revision: number;
  network_revision: number; active_procedure: string; boot_error?: string;
  animation_error?: string;
}
export interface HardwareConfig {
  schema: 'lightvm.config/1';
  devices: { id: string; type: 'ws2812b'; config: { pin: string; count: number; order?: string; brightness_limit?: number } }[];
  targets?: { A?: { device: string } };
  startup?: { procedure: string; target: 'A' } | null;
}
export interface NetworkConfig {
  schema: 'lightvm.network/1';
  wifi: { enabled: boolean; country: string; mode?: 'station' | 'ap'; ap?: { ssid?: string; password?: string }; networks: { ssid: string; hidden?: boolean; password?: string }[] };
  http: { enabled: boolean; port: number };
}
export interface NetworkStatus { state: string; mode?: 'station' | 'ap'; initial_setup?: boolean; ssid?: string; ip?: string; error?: string; retry_ms?: number; revision: number; http_port: number }
export interface Procedure { name: string; size: number; crc32: number }
export interface Parameter { name: string; type: 'int' | 'float' | 'bool' | 'rgb8'; minimum?: number; maximum?: number }
export interface Manifest { abi: number; parameters: Parameter[] }
export interface ParameterState {
  manifest: Manifest; values: Record<string, string | number | boolean>;
  generation: number; parameter_revision: number; slot: string; paused: boolean; faulted: boolean;
}
export interface NearbyDevice { device_id: string; board: string; hostname: string; ip: string; port: number; url: string }
export interface Diagnostic { message: string; line: number; column: number; end_line?: number; end_column?: number }
export interface Compilation { id: number; source: string; wasm: Uint8Array; manifest: Manifest; diagnostics: Diagnostic[] }
export const EMPTY_CONFIG: HardwareConfig = { schema: 'lightvm.config/1', devices: [], targets: {}, startup: null };
export const DEFAULT_SOURCE = 'from lightvm import parameter, hsv, on_frame\n\nspeed = parameter("speed", 0.1, minimum=-10.0, maximum=10.0)\nscale = parameter("scale", 1.0, minimum=0.0, maximum=16.0)\nphase = 0.0\n\n@on_frame\ndef draw(ctx):\n    global phase\n    strip = ctx.pixels\n    phase = (phase + ctx.delta * speed.value) % 1.0\n    for i in range(strip.count):\n        strip[i] = hsv(phase + i * scale.value / strip.count, 1.0, 1.0)\n';
