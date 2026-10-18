import { DEFAULT_EXAMPLE } from './examples';
export type Path = 'usb' | 'http';
export interface Description {
  controller_id: string; boot_id: string; board: string;
  version: string; animation_reset?: boolean; activation_settings?: boolean;
  module_limit: number; config_limit: number;
  pixel_capacity: number; pins: string[]; slots: string[];
  storage_available: boolean; wifi_supported: boolean; wifi_ap_supported?: boolean; http_supported: boolean; websocket_supported?: boolean; mqtt_supported?: boolean;
  active_revision: number; saved_revision: number; catalog_revision: number;
  network_revision: number; active_program: string; boot_error?: string;
  animation_error?: string;
  ab_transitions?: boolean; transition_sources?: boolean; transition_effects?: string[]; transition_programs?: boolean;
  compositions?: boolean; composition_layer_limit?: number; composition_blend_modes?: string[];
  presets?: boolean; preset_transitions?: boolean; power_transitions?: boolean; transient_settings?: boolean; typed_storage?: boolean;
  storage_backend?: string; storage_writable?: boolean; storage_bytes?: number; free_bytes?: number;
  largest_free_bytes?: number; storage_allocation_unit?: number;
  boot_sequence_supported?: boolean;
  recovery_supported?: boolean;
  boot_sequence?: { count: number; triggered: boolean; error: string };
}
export interface TransitionSpec {
  program: string; crc32: number; duration_ms: number;
  settings?: Record<string, number | boolean>; seed?: number;
}
export interface HardwareConfig {
  schema: 'lightvm.config';
  outputs: { id: string; type: 'ws2812b'; on_transition?: TransitionSpec; off_transition?: TransitionSpec; home_assistant?: { default_preset: string; presets: string[]; name?: string }; startup?: { program: string; settings?: Record<string, number | boolean> }; config: { pin: string; count: number; order?: string; brightness_limit?: number } }[];
}
export interface NetworkConfig {
  schema: 'lightvm.network';
  wifi: { enabled: boolean; country: string; mode?: 'station' | 'ap'; ap?: { ssid?: string; password?: string }; networks: { ssid: string; hidden?: boolean; password?: string }[] };
  http: { enabled: boolean; port: number };
  mqtt?: { enabled: boolean; host: string; port: number; username: string; password?: string };
}
export interface NetworkStatus { mqtt_connected?: boolean; mqtt_error?: string; state: string; mode?: 'station' | 'ap'; initial_setup?: boolean; recovery?: boolean; ssid?: string; ip?: string; error?: string; retry_ms?: number; revision: number; http_port: number }
export interface Program { name: string; size: number; crc32: number; type?: 'animation' | 'transition'; profile?: 'animation' | 'transition' }
export interface Setting { name: string; type: 'int' | 'float' | 'bool' | 'rgb8'; minimum?: number; maximum?: number }
export interface Manifest { compiler_version: string; requires: string; schema: string; profile: string; pixels: { format: string }; settings: Setting[] }
export interface ProgramState {
  output?: string; layer?: string;
  manifest: Manifest;
  generation: number; slot: string; paused: boolean; faulted: boolean;
  render_count?: number; settings_values?: Record<string, number | boolean>;
}
export interface NearbyController { controller_id: string; board: string; hostname: string; ip: string; port: number; url: string }
export interface Diagnostic { message: string; line: number; column: number; end_line?: number; end_column?: number }
export interface Compilation { id: number; source: string; bytecode: Uint8Array; manifest: Manifest; diagnostics: Diagnostic[] }
export const EMPTY_CONFIG: HardwareConfig = { schema: 'lightvm.config', outputs: [] };
export const DEFAULT_SOURCE = DEFAULT_EXAMPLE.source;

export interface CompositionState {
  active: boolean; preset?: string | null; revision?: number; playing?: boolean;
  layers?: { id: string; program: string; generation?: number }[];
}

export interface TransitionState {
  revision: number; active: boolean; progress: number; effect: string; seed: number;
  program?: string; program_crc32?: number;
  recall?: { power: boolean; preset: string | null } | null;
  driver: { kind: 'manual' | 'timed' | 'source'; assignment: number; source?: string; target?: number; duration_ms?: number; interval_ms?: number };
  A: CompositionState;
  B: CompositionState;
  error: string;
}
