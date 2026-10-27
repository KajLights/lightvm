import { useEffect, useRef, useState } from 'react';
import * as Blockly from 'blockly/core';
import * as En from 'blockly/msg/en';
import { BLOCK_DEFINITIONS, lowerBlocks, parseBlocks } from './languages/blocks';
import type { Diagnostic } from './types';
Blockly.setLocale(Object.fromEntries(Object.entries(En).filter((entry): entry is [string, string] => typeof entry[1] === 'string')));
Blockly.defineBlocksWithJsonArray(BLOCK_DEFINITIONS);
class NamePicker extends Blockly.FieldDropdown {
  pendingName?: string;
  override setValue(value: string | null, fireChangeEvent?: boolean) {
    if (typeof value === 'string') this.pendingName = value;
    super.setValue(value, fireChangeEvent);
  }
}
// Reading a value uses a picker populated from the names the learner has made.
// Declarations keep editable text fields, so creating a name remains one step.
for (const type of ['lm_get', 'lm_call', 'lm_call_procedure', 'lm_array_get', 'lm_array_set', 'lm_array_length']) {
  const original = Blockly.Blocks[type].init;
  Blockly.Blocks[type].init = function(this: Blockly.Block) {
    original.call(this);
    const input = this.inputList.find(input => input.fieldRow.some(field => field.name === 'NAME'))!;
    const index = input.fieldRow.findIndex(field => field.name === 'NAME'), initial = this.getFieldValue('NAME');
    input.removeField('NAME');
    const owner = this;
    const picker = new NamePicker(function(this: NamePicker) {
      const block = owner;
      const names = new Set<string>([this.pendingName || block.getFieldValue('NAME') || initial]);
      for (const block of owner.workspace.getAllBlocks(false)) {
        if (type === 'lm_call' || type === 'lm_call_procedure') {
          if (block.type === (type === 'lm_call' ? 'lm_function' : 'lm_procedure')) names.add(block.getFieldValue('NAME'));
        } else if (type !== 'lm_get') {
          if (block.type === 'lm_array') names.add(block.getFieldValue('NAME'));
        } else if (owner.getFieldValue('SCOPE') === 'state') {
          if (block.type === 'lm_setting' || block.type === 'lm_set' && block.getFieldValue('SCOPE') === 'state') names.add(block.getFieldValue('NAME'));
        } else {
          if (['lm_each', 'lm_repeat', 'lm_for_range', 'lm_for_array'].includes(block.type) || block.type === 'lm_set' && block.getFieldValue('SCOPE') === 'local') names.add(block.getFieldValue('NAME'));
          if (['lm_function', 'lm_procedure', 'lm_sprite'].includes(block.type)) for (const name of String(block.getFieldValue('PARAM')).split(',')) if (name.trim()) names.add(name.trim());
        }
      }
      return [...names].sort().map(name => [name, name] as [string, string]);
    });
    // Saved names remain selectable before their declaration has loaded.
    input.insertFieldAt(index, picker, 'NAME'); picker.setValue(initial);
  };
}
const theme = Blockly.Theme.defineTheme('lightvm', {
  name: 'lightvm', base: Blockly.Themes.Classic,
  componentStyles: {
    workspaceBackgroundColour: '#14212b', toolboxBackgroundColour: '#22323e', toolboxForegroundColour: '#e5eef5',
    flyoutBackgroundColour: '#293e4b', flyoutForegroundColour: '#e5eef5', flyoutOpacity: 1,
    scrollbarColour: '#8194a0', scrollbarOpacity: 0.55, insertionMarkerColour: '#c4f46a', insertionMarkerOpacity: 0.4,
  },
});
const category = (name: string, colour: string, types: string[]) => ({ kind: 'category', name, colour, contents: types.map(type => ({ kind: 'block', type })) });
const toolbox = { kind: 'categoryToolbox', contents: [
  category('Program', '#9b59b6', ['lm_program', 'lm_runtime']),
  category('Light & colour', '#bd7730', ['lm_fill', 'lm_pixel', 'lm_range', 'lm_gradient', 'lm_clear', 'lm_rgb', 'lm_hsv', 'lm_colorwheel']),
  category('Motion', '#536fc0', ['lm_motion', 'lm_lerp', 'lm_wrap', 'lm_native_math', 'lm_power', 'lm_noise2', 'lm_noise3', 'lm_random', 'lm_random_int', 'lm_random_seed']),
  category('Numbers & logic', '#417bb0', ['lm_number', 'lm_integer', 'lm_fixed', 'lm_convert', 'lm_math', 'lm_negate', 'lm_bitwise', 'lm_bit_not', 'lm_compare', 'lm_bool', 'lm_logic', 'lm_not']),
  category('Loops & choices', '#558b3c', ['lm_each', 'lm_repeat', 'lm_for_range', 'lm_for_array', 'lm_while', 'lm_if', 'lm_flow']),
  category('Values & inputs', '#a84978', ['lm_set', 'lm_get', 'lm_setting', 'lm_input', 'lm_array', 'lm_array_get', 'lm_array_set', 'lm_array_length', 'lm_fixed_fill', 'lm_fixed_copy']),
  category('Images & trails', '#bd7730', ['lm_image', 'lm_image_point', 'lm_image_fade', 'lm_image_draw']),
  category('Sprites', '#9b59b6', ['lm_sprite', 'lm_batch', 'lm_spawn', 'lm_batch_render', 'lm_sprite_transform', 'lm_sprite_color', 'lm_sprite_remove', 'lm_artwork', 'lm_artwork_draw']),
  category('Functions', '#9b59b6', ['lm_function', 'lm_call', 'lm_procedure', 'lm_call_procedure']),
  category('Transitions', '#9b59b6', ['lm_mix', 'lm_wipe', 'lm_copy']),
] };
const beginnerToolbox = { kind: 'categoryToolbox', contents: [
  category('Start here', '#9b59b6', ['lm_program', 'lm_runtime']),
  category('Lights', '#bd7730', ['lm_fill', 'lm_pixel', 'lm_rgb']),
  category('Numbers & names', '#a84978', ['lm_number', 'lm_math', 'lm_set', 'lm_get']),
  category('Repeat & choose', '#558b3c', ['lm_each', 'lm_repeat', 'lm_compare', 'lm_if']),
] };

export function BlocksEditor({ source, onChange, diagnostics, active = true }: { source: string; onChange: (source: string) => void; diagnostics: Diagnostic[]; active?: boolean }) {
  const host = useRef<HTMLDivElement>(null), workspace = useRef<Blockly.WorkspaceSvg | null>(null);
  const current = useRef(source), change = useRef(onChange), loading = useRef(false);
  const [error, setError] = useState(''), [toolboxOpen, setToolboxOpen] = useState(true), [advanced, setAdvanced] = useState(false); change.current = onChange;
  useEffect(() => {
    if (!host.current) return;
    const ws = Blockly.inject(host.current, { toolbox: beginnerToolbox, renderer: 'zelos', trashcan: true, sounds: false,
      grid: { spacing: 24, length: 2, colour: '#344955', snap: true },
      zoom: { controls: true, wheel: true, startScale: 0.85 }, move: { scrollbars: true, drag: true, wheel: true },
      theme,
    });
    workspace.current = ws;
    try { Blockly.serialization.workspaces.load(parseBlocks(current.current), ws); ws.zoomToFit(); } catch (e) { setError((e as Error).message); }
    ws.addChangeListener(event => {
      if (loading.current || event.isUiEvent || event.type === Blockly.Events.FINISHED_LOADING) return;
      const next = JSON.stringify(Blockly.serialization.workspaces.save(ws)); current.current = next; change.current(next);
    });
    let compact: boolean | undefined;
    const resize = new ResizeObserver(() => {
      Blockly.svgResize(ws);
      const next = (host.current?.clientWidth ?? 1000) < 600;
      if (compact !== next) { compact = next; setToolboxOpen(!next); }
    }); resize.observe(host.current);
    return () => { resize.disconnect(); ws.dispose(); workspace.current = null; };
  }, []);
  useEffect(() => { workspace.current?.updateToolbox(advanced ? toolbox : beginnerToolbox); }, [advanced]);
  useEffect(() => {
    const ws = workspace.current; if (!ws) return;
    ws.getToolbox()?.setVisible(toolboxOpen); Blockly.svgResize(ws); ws.zoomToFit();
  }, [toolboxOpen]);
  useEffect(() => {
    if (!workspace.current || source === current.current) return;
    loading.current = true;
    Blockly.Events.disable();
    try { Blockly.serialization.workspaces.load(parseBlocks(source), workspace.current); workspace.current.zoomToFit(); current.current = source; setError(''); }
    catch (e) { setError((e as Error).message); }
    finally { Blockly.Events.enable(); loading.current = false; }
  }, [source]);
  useEffect(() => { if (workspace.current && active) Blockly.svgResize(workspace.current); }, [active]);
  useEffect(() => {
    const ws = workspace.current; if (!ws) return;
    ws.getAllBlocks(false).forEach(block => block.setWarningText(null));
    for (const d of diagnostics) if (d.blockId) ws.getBlockById(d.blockId)?.setWarningText(d.message);
  }, [diagnostics]);
  useEffect(() => {
    const focus = (event: Event) => {
      const id = (event as CustomEvent<string>).detail, ws = workspace.current;
      if (ws?.getBlockById(id)) { ws.centerOnBlock(id); ws.highlightBlock(id); }
    };
    window.addEventListener('lightvm-focus-block', focus); return () => window.removeEventListener('lightvm-focus-block', focus);
  }, []);
  let javascript = '';
  try { javascript = lowerBlocks(source).javascript; } catch { /* Code checks describe incomplete workspaces. */ }
  return <div className="blocks-editor">
    <p className="helper blocks-hint">Open a category, then drag blocks into your program. Try changing a number or colour. Scroll to move; use + / − to zoom.</p>
    {error && <p role="alert">{error}</p>}
    <div className="blocks-tools"><button className="quiet" aria-pressed={toolboxOpen} onClick={() => setToolboxOpen(value => !value)}>{toolboxOpen ? 'Hide toolbox' : 'Show toolbox'}</button><button className="quiet" aria-pressed={advanced} onClick={() => setAdvanced(value => !value)}>{advanced ? 'Show starting blocks' : 'Show all blocks'}</button><button className="quiet" onClick={() => workspace.current?.zoomToFit()}>Fit blocks</button></div>
    <div ref={host} className="block-workspace" aria-label="Blocks workspace" />
    <details className="blocks-code"><summary>Explore the JavaScript</summary><p className="helper">A reading guide to your blocks. Blocks compile independently to LightVM. Edit JavaScript in its own draft using the language selector.</p><pre aria-label="Generated JavaScript">{javascript || 'Connect the blocks to see their code.'}</pre></details>
  </div>;
}
