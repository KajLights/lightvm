import { releaseError } from './release';
import type { AppStore } from './store';
import { activityActions, editorActions } from './store';
import type { Language } from './languages/source';
import type { Compilation, Description, Manifest } from './types';

export function programCompatibility(target: Description, manifest: Manifest): string | null {
  if (manifest.schema !== 'lightvm.program' || !['animation', 'transition'].includes(manifest.profile) || manifest.pixels?.format !== 'rgba8-premul')
    return 'Recompile this program with the release-based compiler.';
  if (manifest.profile === 'transition' && !target.transition_programs) return 'Install firmware with transition program support.';
  if (manifest.inputs?.length && !target.live_inputs) return 'Install firmware with live input support.';
  if (manifest.inputs?.some(input => input.control) && !target.light_controls) return 'Install firmware with light control support.';
  return releaseError(target.version, manifest.requires);
}
export function supportsProgram(target: Description, manifest: Manifest) {
  return programCompatibility(target, manifest) === null;
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
    this.store.dispatch(editorActions.compiler({ status: 'loading', message: 'Loading the LightVM compiler…' }));
    try {
      this.worker = new Worker(new URL('./compiler-worker.ts', import.meta.url), { type: 'module' });
      this.initializationDeadline = setTimeout(() => this.fail('Compiler download timed out. Check your internet connection and retry.'), 60000);
      this.worker.postMessage({ type: 'initialize', baseUrl: assetUrl('./') });
      this.worker.onerror = event => this.fail(event.message || 'The compiler could not load. Check your internet connection and retry.');
      this.worker.onmessage = ({ data }) => {
        if (data.type === 'status') this.store.dispatch(editorActions.compiler({ status: 'loading', message: data.message }));
        else if (data.type === 'fatal') this.fail(data.message);
        else if (data.type === 'ready') {
          clearTimeout(this.initializationDeadline);
          this.store.dispatch(editorActions.compiler({ status: 'ready', message: 'Compiler ready' }));
          this.store.dispatch(activityActions.log({ text: 'LightVM compiler ready. Compilation stays in this browser.', kind: 'success' }));
          this.check();
        } else if (data.id === this.store.getState().editor.revision) {
          clearTimeout(this.compilationDeadline);
          if (data.type === 'failed') { this.fail(data.message); return; }
          const editor = this.store.getState().editor;
          this.artifact = data.diagnostics.length ? null : { ...data, source: editor.source };
          this.store.dispatch(editorActions.compiled({ revision: data.id, bytes: data.bytecode.length, manifest: data.manifest, diagnostics: data.diagnostics, advisories: data.advisories }));
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
    const { editor } = this.store.getState();
    if (editor.compiler !== 'ready' || !this.worker) return;
    clearTimeout(this.compilationDeadline);
    this.compilationDeadline = setTimeout(() => this.fail('Compilation timed out. Retry the compiler or simplify the source.'), 10000);
    this.worker.postMessage({ id: editor.revision, source: editor.source, language: editor.language, filename: editor.filename, target: null });
  }
  edit(source: string, filename?: string) {
    this.store.dispatch(editorActions.edited({ source, filename }));
  }
  language(language: Language) { this.store.dispatch(editorActions.languageChanged(language)); }
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
