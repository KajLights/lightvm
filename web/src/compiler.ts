import type { AppStore } from './store';
import { activityActions, DRAFT_KEY, editorActions } from './store';
import type { Compilation, Description, Manifest } from './types';

export function supportsProgram(target: Description, manifest: Manifest) {
  const abis = target.program_abis ?? [target.control_abi];
  const formats = target.bytecode_formats ?? [target.bytecode_format];
  return abis.includes(manifest.abi) && formats.includes(`LBC1/${manifest.abi}`) &&
    target.program_schemas.includes(manifest.schema ?? `lightvm.program/${manifest.abi + 1}`) &&
    (manifest.abi !== 3 || Boolean(target.program_profiles?.includes('animation/1')));
}

export function assetUrl(name: string) { return new URL(name, new URL(import.meta.env.BASE_URL, document.baseURI)).href; }

// Worker handles and binary artifacts have lifetimes, rather than Redux state.
export class Compiler {
  worker: Worker | null = null;
  artifact: Compilation | null = null;
  private timer?: ReturnType<typeof setTimeout>;
  private initializationDeadline?: ReturnType<typeof setTimeout>;
  private compilationDeadline?: ReturnType<typeof setTimeout>;
  private unsubscribe?: () => void;
  constructor(private store: AppStore) {}
  start() {
    if (this.worker) return;
    this.store.dispatch(editorActions.compiler({ status: 'loading', message: 'Loading Python…' }));
    try {
      this.worker = new Worker(assetUrl('compiler-worker.mjs'), { type: 'module' });
      this.initializationDeadline = setTimeout(() => this.fail('Compiler download timed out. Check your internet connection and retry.'), 60000);
      this.worker.onerror = event => this.fail(event.message || 'The compiler could not load. Check your internet connection and retry.');
      this.worker.onmessage = ({ data }) => {
        if (data.type === 'status') this.store.dispatch(editorActions.compiler({ status: 'loading', message: data.message }));
        else if (data.type === 'fatal') this.fail(data.message);
        else if (data.type === 'ready') {
          clearTimeout(this.initializationDeadline);
          this.store.dispatch(editorActions.compiler({ status: 'ready', message: 'Compiler ready' }));
          this.store.dispatch(activityActions.log({ text: 'Python compiler ready. Compilation stays in this browser.', kind: 'success' }));
          this.check();
        } else if (data.id === this.store.getState().editor.revision) {
          clearTimeout(this.compilationDeadline);
          if (data.type === 'failed') { this.fail(data.message); return; }
          const editor = this.store.getState().editor;
          this.artifact = data.diagnostics.length ? null : { ...data, source: editor.source };
          this.store.dispatch(editorActions.compiled({ revision: data.id, bytes: data.bytecode.length, manifest: data.manifest, diagnostics: data.diagnostics }));
        }
      };
    } catch (error) { this.fail((error as Error).message); return; }
    let revision = this.store.getState().editor.revision;
    this.unsubscribe = this.store.subscribe(() => {
      const editor = this.store.getState().editor;
      if (editor.revision === revision) return;
      revision = editor.revision; this.artifact = null;
      clearTimeout(this.timer); clearTimeout(this.compilationDeadline);
      this.timer = setTimeout(() => this.check(), 220);
    });
  }
  check() {
    const { editor, device } = this.store.getState();
    if (editor.compiler !== 'ready' || !this.worker) return;
    clearTimeout(this.compilationDeadline);
    this.compilationDeadline = setTimeout(() => this.fail('Compilation timed out. Retry the compiler or simplify the source.'), 10000);
    this.worker.postMessage({ id: editor.revision, source: editor.source, target: device.selectedPath ? device.description : null });
  }
  edit(source: string, filename?: string) {
    this.store.dispatch(editorActions.edited({ source, filename }));
    try {
      const editor = this.store.getState().editor;
      localStorage.setItem(DRAFT_KEY, JSON.stringify({ source, filename: editor.filename }));
      this.store.dispatch(editorActions.draftSaved(true));
    } catch { this.store.dispatch(editorActions.draftSaved(false)); }
  }
  fail(message: string) {
    this.stop();
    this.store.dispatch(editorActions.compiler({ status: 'error', message }));
    this.store.dispatch(activityActions.log({ text: message, kind: 'error' }));
  }
  retry() { this.stop(); this.start(); }
  stop() {
    this.worker?.terminate(); this.worker = null; this.artifact = null;
    this.unsubscribe?.(); this.unsubscribe = undefined;
    clearTimeout(this.timer); clearTimeout(this.initializationDeadline); clearTimeout(this.compilationDeadline);
  }
}
