import { configureStore, createSlice, type PayloadAction } from '@reduxjs/toolkit';
import { useDispatch, useSelector } from 'react-redux';
import type { Description, Diagnostic, HardwareConfig, Manifest, NearbyDevice, NetworkConfig, NetworkStatus, ParameterState, Path, Procedure } from './types';
import { DEFAULT_SOURCE } from './types';
import { DEFAULT_EXAMPLE, exampleForSource } from './examples';

export const DRAFT_KEY = 'lightvm.editor.draft.v1';
function readDraft() {
  try {
    const saved = JSON.parse(localStorage.getItem(DRAFT_KEY) || 'null');
    if (typeof saved?.source === 'string') {
      const filename = typeof saved.filename === 'string' ? saved.filename : 'animation.py';
      return { source: saved.source, filename: filename === 'animation.py' ? exampleForSource(saved.source)?.filename ?? filename : filename };
    }
  } catch { /* Browser storage is optional. */ }
  return { source: DEFAULT_SOURCE, filename: DEFAULT_EXAMPLE.filename };
}
interface DeviceState {
  description: Description | null;
  paths: { key: Path; label: string }[]; selectedPath: Path | null;
  config: HardwareConfig | null; configRevision: number; savedRevision: number;
  networkConfig: NetworkConfig | null; networkRevision: number; networkStatus: NetworkStatus | null;
  procedures: Procedure[]; presets: Procedure[]; parameters: ParameterState | null;
  selectedOutput: string | null;
  nearby: NearbyDevice[]; discovering: boolean; discoveryMessage: string;
}
const initialDevice: DeviceState = {
  description: null, paths: [], selectedPath: null, config: null, configRevision: 0, savedRevision: 0,
  networkConfig: null, networkRevision: 0, networkStatus: null, procedures: [], presets: [], parameters: null, selectedOutput: null,
  nearby: [], discovering: false, discoveryMessage: 'Finding devices on your local network.',
};
const deviceSlice = createSlice({
  name: 'device', initialState: initialDevice,
  reducers: {
    connections(state, action: PayloadAction<{ paths: DeviceState['paths']; selectedPath: Path | null; description?: Description }>) {
      const { description, ...connections } = action.payload;
      if (description && state.description?.device_id !== description.device_id) Object.assign(state, {
        config: null, configRevision: 0, savedRevision: 0, networkConfig: null, networkRevision: 0,
        networkStatus: null, parameters: null, procedures: [], presets: [], selectedOutput: null,
      });
      Object.assign(state, connections);
      if (description) state.description = description;
    },
    description(state, action: PayloadAction<Description>) { state.description = action.payload; },
    configuration(state, action: PayloadAction<{ config: HardwareConfig; revision: number; savedRevision: number }>) {
      state.config = action.payload.config; state.configRevision = action.payload.revision; state.savedRevision = action.payload.savedRevision;
      if (!state.config.devices.some(output => output.id === state.selectedOutput)) {
        state.selectedOutput = state.config.devices[0]?.id ?? null; state.parameters = null;
      }
    },
    network(state, action: PayloadAction<{ config: NetworkConfig; revision: number }>) {
      state.networkConfig = action.payload.config; state.networkRevision = action.payload.revision;
    },
    networkStatus(state, action: PayloadAction<NetworkStatus>) { state.networkStatus = action.payload; },
    procedures(state, action: PayloadAction<Procedure[]>) { state.procedures = action.payload; },
    presets(state, action: PayloadAction<Procedure[]>) { state.presets = action.payload; },
    parameters(state, action: PayloadAction<ParameterState | null>) { state.parameters = action.payload; },
    outputSelected(state, action: PayloadAction<string>) {
      if (state.config?.devices.some(output => output.id === action.payload)) { state.selectedOutput = action.payload; state.parameters = null; }
    },
    discoveryStarted(state) { state.discovering = true; },
    discoveryFinished(state, action: PayloadAction<{ devices: NearbyDevice[]; message: string }>) {
      state.nearby = action.payload.devices; state.discoveryMessage = action.payload.message; state.discovering = false;
    },
  },
});
interface EditorState {
  source: string; filename: string; revision: number;
  compiler: 'idle' | 'loading' | 'ready' | 'error'; message: string; checking: boolean;
  diagnostics: Diagnostic[]; artifact: { revision: number; bytes: number; manifest: Manifest } | null;
  draftSaved: boolean;
}
const initialEditor: EditorState = {
  ...readDraft(), revision: 0, compiler: 'idle', message: 'Open Develop to load the compiler.',
  checking: false, diagnostics: [], artifact: null, draftSaved: true,
};
const editorSlice = createSlice({
  name: 'editor', initialState: initialEditor,
  reducers: {
    edited(state, action: PayloadAction<{ source: string; filename?: string }>) {
      state.source = action.payload.source;
      if (action.payload.filename) state.filename = action.payload.filename;
      state.revision++; state.artifact = null; state.diagnostics = []; state.checking = true;
    },
    targetChanged(state) { state.revision++; state.artifact = null; state.diagnostics = []; state.checking = true; },
    compiler(state, action: PayloadAction<{ status: EditorState['compiler']; message: string }>) {
      state.compiler = action.payload.status; state.message = action.payload.message;
      if (state.compiler !== 'ready') state.artifact = null;
      if (state.compiler === 'error') state.checking = false;
    },
    compiled(state, action: PayloadAction<{ revision: number; bytes: number; manifest: Manifest; diagnostics: Diagnostic[] }>) {
      if (action.payload.revision !== state.revision) return;
      state.checking = false; state.diagnostics = action.payload.diagnostics;
      state.artifact = action.payload.diagnostics.length ? null : { revision: action.payload.revision, bytes: action.payload.bytes, manifest: action.payload.manifest };
    },
    draftSaved(state, action: PayloadAction<boolean>) { state.draftSaved = action.payload; },
  },
});
const activitySlice = createSlice({
  name: 'activity',
  initialState: { operation: null as string | null, notice: '', progress: null as number | null, entries: [{ id: 0, text: 'Workspace opened.', kind: '' }], sequence: 0 },
  reducers: {
    operation(state, action: PayloadAction<string | null>) { state.operation = action.payload; },
    notice(state, action: PayloadAction<string>) { state.notice = action.payload; },
    progress(state, action: PayloadAction<number | null>) { state.progress = action.payload; },
    log(state, action: PayloadAction<{ text: string; kind?: string }>) {
      state.entries.unshift({ id: ++state.sequence, text: action.payload.text, kind: action.payload.kind || '' });
      state.entries = state.entries.slice(0, 40);
    },
    clear(state) { state.entries = []; },
  },
});
export const deviceActions = deviceSlice.actions;
export const editorActions = editorSlice.actions;
export const activityActions = activitySlice.actions;
export function createAppStore() {
  return configureStore({ reducer: { device: deviceSlice.reducer, editor: editorSlice.reducer, activity: activitySlice.reducer } });
}
export const store = createAppStore();
export type AppStore = ReturnType<typeof createAppStore>;
export type RootState = ReturnType<AppStore['getState']>;
export type AppDispatch = AppStore['dispatch'];
export const useAppSelector = useSelector.withTypes<RootState>();
export const useAppDispatch = useDispatch.withTypes<AppDispatch>();
export function outputFor(config: HardwareConfig | null) { return config?.devices[0]; }
