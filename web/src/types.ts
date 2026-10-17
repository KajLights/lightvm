import { DEFAULT_EXAMPLE } from './examples';
export type Path = 'usb' | 'http';
export interface Description {
  device_id: string; boot_id: string; board: string;
  version: string; animation_reset?: boolean; activation_settings?: boolean;
  module_limit: number; config_limit: number;
  pixel_capacity: number; pins: string[]; slots: string[];
  storage_available: boolean; wifi_supported: boolean; wifi_ap_supported?: boolean; http_supported: boolean;
  active_revision: number; saved_revision: number; catalog_revision: number;
  network_revision: number; active_procedure: string; boot_error?: string;
  animation_error?: string;
  scenes?: boolean; scene_layer_limit?: number; scene_blend_modes?: string[];
  presets?: boolean; preset_limit?: number; procedure_limit?: number; transient_settings?: boolean;
  boot_sequence_supported?: boolean;
  recovery_supported?: boolean;
  boot_sequence?: { count: number; triggered: boolean; error: string };
}
export interface HardwareConfig {
  schema: 'lightvm.config';
  devices: { id: string; type: 'ws2812b'; startup?: { procedure: string; args?: Record<string, number | boolean> }; config: { pin: string; count: number; order?: string; brightness_limit?: number } }[];
}
export interface NetworkConfig {
  schema: 'lightvm.network';
  wifi: { enabled: boolean; country: string; mode?: 'station' | 'ap'; ap?: { ssid?: string; password?: string }; networks: { ssid: string; hidden?: boolean; password?: string }[] };
  http: { enabled: boolean; port: number };
}
export interface NetworkStatus { state: string; mode?: 'station' | 'ap'; initial_setup?: boolean; recovery?: boolean; ssid?: string; ip?: string; error?: string; retry_ms?: number; revision: number; http_port: number }
export interface Procedure { name: string; size: number; crc32: number }
export interface Setting { name: string; type: 'int' | 'float' | 'bool' | 'rgb8'; minimum?: number; maximum?: number }
export interface Manifest { compiler_version: string; requires: string; schema: string; profile: string; pixels: { format: string }; settings: Setting[] }
export interface ProgramState {
  device?: string; layer?: string;
  manifest: Manifest;
  generation: number; slot: string; paused: boolean; faulted: boolean;
  render_count?: number; settings_values?: Record<string, number | boolean>;
}
export interface NearbyDevice { device_id: string; board: string; hostname: string; ip: string; port: number; url: string }
export interface Diagnostic { message: string; line: number; column: number; end_line?: number; end_column?: number }
export interface Compilation { id: number; source: string; bytecode: Uint8Array; manifest: Manifest; diagnostics: Diagnostic[] }
export const EMPTY_CONFIG: HardwareConfig = { schema: 'lightvm.config', devices: [] };
export const DEFAULT_SOURCE = DEFAULT_EXAMPLE.source;
