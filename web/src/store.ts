import { configureStore, createSlice, type PayloadAction } from '@reduxjs/toolkit';
import { useDispatch, useSelector } from 'react-redux';
import type { Description, Diagnostic, HardwareConfig, Manifest, NearbyController, NetworkConfig, NetworkStatus, ProgramState, Path, Program } from './types';
import { DEFAULT_SOURCE } from './types';
import { DEFAULT_EXAMPLE } from './examples';

interface ControllerState {
  description: Description | null;
  paths: { key: Path; label: string; protocol?: string }[]; selectedPath: Path | null;
  config: HardwareConfig | null; configRevision: number; savedRevision: number;
  networkConfig: NetworkConfig | null; networkRevision: number; networkStatus: NetworkStatus | null;
  programs: Program[]; presets: Program[]; programState: ProgramState | null;
  selectedOutput: string | null;
  nearby: NearbyController[]; discovering: boolean; discoveryMessage: string;
}
const initialController: ControllerState = {
  description: null, paths: [], selectedPath: null, config: null, configRevision: 0, savedRevision: 0,
  networkConfig: null, networkRevision: 0, networkStatus: null, programs: [], presets: [], programState: null, selectedOutput: null,
  nearby: [], discovering: false, discoveryMessage: 'Finding controllers on your local network.',
};
const controllerSlice = createSlice({
  name: 'controller', initialState: initialController,
  reducers: {
    connections(state, action: PayloadAction<{ paths: ControllerState['paths']; selectedPath: Path | null; description?: Description }>) {
      const { description, ...connections } = action.payload;
      if (description && state.description?.controller_id !== description.controller_id) Object.assign(state, {
        config: null, configRevision: 0, savedRevision: 0, networkConfig: null, networkRevision: 0,
        networkStatus: null, programState: null, programs: [], presets: [], selectedOutput: null,
      });
      Object.assign(state, connections);
      if (description) state.description = description;
    },
    description(state, action: PayloadAction<Description>) { state.description = action.payload; },
    configuration(state, action: PayloadAction<{ config: HardwareConfig; revision: number; savedRevision: number }>) {
      state.config = action.payload.config; state.configRevision = action.payload.revision; state.savedRevision = action.payload.savedRevision;
      if (!state.config.outputs.some(output => output.id === state.selectedOutput && output.mode !== 'indicators')) {
        state.selectedOutput = state.config.outputs.find(output => output.mode !== 'indicators')?.id ?? null; state.programState = null;
      }
    },
    network(state, action: PayloadAction<{ config: NetworkConfig; revision: number }>) {
      state.networkConfig = action.payload.config; state.networkRevision = action.payload.revision;
    },
    networkStatus(state, action: PayloadAction<NetworkStatus>) { state.networkStatus = action.payload; },
    programs(state, action: PayloadAction<Program[]>) { state.programs = action.payload; },
    presets(state, action: PayloadAction<Program[]>) { state.presets = action.payload; },
    programState(state, action: PayloadAction<ProgramState | null>) { state.programState = action.payload; },
    outputSelected(state, action: PayloadAction<string>) {
      if (state.config?.outputs.some(output => output.id === action.payload && output.mode !== 'indicators')) { state.selectedOutput = action.payload; state.programState = null; }
    },
    discoveryStarted(state) { state.discovering = true; },
    discoveryFinished(state, action: PayloadAction<{ controllers: NearbyController[]; message: string }>) {
      state.nearby = action.payload.controllers; state.discoveryMessage = action.payload.message; state.discovering = false;
    },
  },
});
interface EditorState {
  source: string; filename: string; revision: number;
  compiler: 'idle' | 'loading' | 'ready' | 'error'; message: string; checking: boolean;
  diagnostics: Diagnostic[]; artifact: { revision: number; bytes: number; manifest: Manifest } | null;
}
const initialEditor: EditorState = {
  source: DEFAULT_SOURCE, filename: DEFAULT_EXAMPLE.filename, revision: 0, compiler: 'idle', message: 'Open Editor to load the compiler.',
  checking: false, diagnostics: [], artifact: null,
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
export const controllerActions = controllerSlice.actions;
export const editorActions = editorSlice.actions;
export const activityActions = activitySlice.actions;
export function createAppStore() {
  return configureStore({ reducer: { controller: controllerSlice.reducer, editor: editorSlice.reducer, activity: activitySlice.reducer } });
}
export const store = createAppStore();
export type AppStore = ReturnType<typeof createAppStore>;
export type RootState = ReturnType<AppStore['getState']>;
export type AppDispatch = AppStore['dispatch'];
export const useAppSelector = useSelector.withTypes<RootState>();
export const useAppDispatch = useDispatch.withTypes<AppDispatch>();
export function outputFor(config: HardwareConfig | null) { return config?.outputs.find(output => output.mode !== 'indicators'); }
