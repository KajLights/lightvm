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
  presets?: boolean; preset_transitions?: boolean; transient_settings?: boolean; typed_storage?: boolean;
  storage_backend?: string; storage_writable?: boolean; storage_bytes?: number; free_bytes?: number;
  largest_free_bytes?: number; storage_allocation_unit?: number;
  boot_sequence_supported?: boolean;
  recovery_supported?: boolean;
  live_inputs?: boolean; input_limit?: number; input_source_limit?: number;
  boot_sequence?: { count: number; triggered: boolean; error: string };
}
export interface TransitionSpec {
  program: string; crc32: number; duration_ms: number;
  settings?: Record<string, number | boolean>; inputs?: InputBindings; seed?: number;
}
export interface HardwareConfig {
  schema: 'lightvm.config';
  inputs?: { id: string; type: 'tca9555'; config: { sda: string; scl: string; address?: number; active_low?: boolean; debounce_ms?: number; long_press_ms?: number; multi_press_ms?: number }; home_assistant?: { indicators?: string; names?: string[] } }[];
  outputs: { id: string; type: 'ws2812b' | 'apa102'; mode?: 'animation' | 'indicators'; home_assistant?: { default_preset: string; presets: string[]; name?: string }; startup?: { mode: 'off' | 'restore_last' } | { mode: 'preset'; preset: string }; config: { pin?: string; data_pin?: string; clock_pin?: string; enable_pin?: string; frequency?: number; count: number; order?: string; brightness_limit?: number } }[];
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
export interface RampDriver { kind: 'ramp'; duration: number; mode: 'once' | 'repeat' | 'pingpong'; easing?: 'linear' | 'smoothstep'; from?: number; to?: number; restart?: boolean }
export type InputBinding = { value: number | boolean } | { driver: RampDriver } | { source: string; timeout_ms?: number; fallback?: 'default' | 'hold'; map?: { input_min: number; input_max: number; output_min: number; output_max: number } } | { default: true };
export type InputBindings = Record<string, InputBinding>;
export interface InputDeclaration extends Setting { default: number | boolean; unit?: string; driver?: RampDriver }
export interface InputState { value: number | boolean; fallback: boolean; binding: InputBinding }
export interface InputSource { name: string; type: Setting['type']; valid: boolean; value: number | boolean | null; age_ms: number | null }
export interface Manifest { compiler_version: string; requires: string; schema: string; profile: string; pixels: { format: string }; settings: Setting[]; inputs?: InputDeclaration[] }
export interface ProgramState {
  output?: string; layer?: string;
  target?: 'transition';
  manifest: Manifest;
  generation: number; slot: string; paused: boolean; faulted: boolean;
  render_count?: number; settings_values?: Record<string, number | boolean>;
  inputs_revision?: number; inputs?: Record<string, InputState>;
  program_crc32?: number;
}
export interface NearbyController { controller_id: string; board: string; hostname: string; ip: string; port: number; url: string }
export interface Diagnostic { blockId?: string; message: string; line: number; column: number; end_line?: number; end_column?: number }
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
  driver: { kind: 'manual' | 'timed' | 'source' | 'input'; assignment: number; source?: string; target?: number; duration_ms?: number; interval_ms?: number; binding?: InputBinding };
  A: CompositionState;
  B: CompositionState;
  error: string;
}
