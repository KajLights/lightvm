export type Path = 'usb' | 'http';
export interface Description {
  device_id: string; boot_id: string; board: string; control_abi: number;
  program_schemas: string[]; module_limit: number; config_limit: number;
  pixel_capacity: number; pins: string[]; slots: string[];
  storage_available: boolean; wifi_supported: boolean; wifi_ap_supported?: boolean; http_supported: boolean;
  active_revision: number; saved_revision: number; catalog_revision: number;
  network_revision: number; active_procedure: string; boot_error?: string;
  animation_error?: string;
  boot_sequence_supported?: boolean;
  recovery_supported?: boolean;
  boot_sequence?: { count: number; triggered: boolean; error: string };
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
export interface NetworkStatus { state: string; mode?: 'station' | 'ap'; initial_setup?: boolean; recovery?: boolean; ssid?: string; ip?: string; error?: string; retry_ms?: number; revision: number; http_port: number }
export interface Procedure { name: string; size: number; crc32: number }
export interface Parameter { name: string; type: 'int' | 'float' | 'bool' | 'rgb8'; minimum?: number; maximum?: number }
export interface Manifest { abi: number; parameters: Parameter[] }
export interface ParameterState {
  manifest: Manifest; values: Record<string, string | number | boolean>;
  generation: number; parameter_revision: number; slot: string; paused: boolean; faulted: boolean;
}
export interface NearbyDevice { device_id: string; board: string; hostname: string; ip: string; port: number; url: string }
export interface Diagnostic { message: string; line: number; column: number; end_line?: number; end_column?: number }
export interface Compilation { id: number; source: string; bytecode: Uint8Array; manifest: Manifest; diagnostics: Diagnostic[] }
export const EMPTY_CONFIG: HardwareConfig = { schema: 'lightvm.config/1', devices: [], targets: {}, startup: null };
export const DEFAULT_SOURCE = "\"\"\"The original moving hue gradient; simulation and rendering are separate.\"\"\"\nfrom lightvm import parameter, hsv, step, prepare, shade\nspeed = parameter(0.1, minimum=-10.0, maximum=10.0)\nscale = parameter(1.0, minimum=0.0, maximum=16.0)\nphase = 0.0\nstride = 0.0\n@step\ndef update():\n    global phase\n    phase = (phase + delta * speed) % 1.0\n@prepare\ndef frame():\n    global stride\n    stride = scale / count\n@shade\ndef pixel():\n    return hsv(phase + index * stride, 1.0, 1.0)\n";
