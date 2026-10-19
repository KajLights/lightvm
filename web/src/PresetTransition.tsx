import { useEffect, useState } from 'react';
import { RecallTransition } from './RecallTransition';
import { validTransitionSpec, type CompositionLayer } from './library';
import { useRuntime } from './runtime';
import type { TransitionSpec } from './types';

// Controller recipes can be edited even when their original browser library is absent.
export function PresetTransition({ name, output, busy }: { name: string; output: string; busy: boolean }) {
  const { controllers } = useRuntime(), client = controllers.client;
  const [recipe, setRecipe] = useState<{ layers: CompositionLayer[]; revision: number }>();
  const [transition, setTransition] = useState<TransitionSpec>();
  const [error, setError] = useState(''), [reload, setReload] = useState(0);
  useEffect(() => {
    let active = true; setRecipe(undefined); setError('');
    if (client) void (async () => {
      try {
        const state = await client.describe();
        const document = await client.json('preset_get', { name });
        if (active) { setRecipe({ layers: document.layers, revision: state.catalog_revision }); setTransition(document.entry_transition); }
      } catch (error) { if (active) setError((error as Error).message); }
    })();
    return () => { active = false; };
  }, [client, name, reload]);
  async function save() {
    if (!recipe) return;
    await controllers.run('Saving preset transition', async current => {
      const connection = controllers.require(current);
      const result = await connection.savePreset(name, recipe.layers, output, transition, recipe.revision);
      setRecipe({ ...recipe, revision: result.value >>> 0 });
      await controllers.readPresets(connection);
      controllers.log('Saved transition for ' + name + '. Playback unchanged.', 'success');
    });
  }
  return <div>
    <RecallTransition id="controller-preset-entry" label="Controller preset transition" value={transition} onChange={setTransition} disabled={busy || !recipe} />
    <p className="helper">This preset’s transition is used when selected, turned on or off, and at startup.</p>
    {error && <p role="alert">{error}</p>}
    <div className="button-pair">
      <button className="secondary" disabled={busy || !recipe || (transition !== undefined && !validTransitionSpec(transition))} onClick={() => void save()}>Save preset transition</button>
      <button className="quiet" disabled={busy} onClick={() => setReload(value => value + 1)}>Reload preset</button>
    </div>
  </div>;
}
