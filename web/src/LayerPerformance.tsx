import { useEffect, useRef, useState } from 'react';
import type { CompositionState } from './types';
import { layerRates } from './layer-performance';

export function LayerPerformance({ composition }: { composition: CompositionState | undefined }) {
  const previous = useRef<CompositionState | null>(null);
  const [rates, setRates] = useState<ReturnType<typeof layerRates>>(new Map());
  useEffect(() => {
    if (!composition) { previous.current = null; setRates(new Map()); return; }
    setRates(layerRates(previous.current, composition)); previous.current = composition;
  }, [composition]);
  if (!composition?.layers?.length) return null;
  const hz = (value: number | null | undefined) => value == null ? '—' : value.toFixed(1) + '/s';
  return <details><summary>Layer performance</summary><p className="helper">Updates completed and distinct contributions included in completed output frames. Another layer may cover them. A valid cached contribution needs no new update, but may still be waiting for output.</p>
    <table><thead><tr><th>Layer</th><th>Updates</th><th>Displayed</th><th>Contribution</th><th>Input delay</th></tr></thead><tbody>{composition.layers.map(layer => <tr key={layer.id}>
      <th>{layer.id}</th><td>{hz(rates.get(layer.id)?.updates)}</td><td>{hz(rates.get(layer.id)?.displayed)}</td>
      <td>{layer.dependency_current === true ? 'Cached · valid' : layer.dependency_current === false ? 'Updating' : layer.displayed_sample_age_us == null ? 'Not displayed' : Math.round(layer.displayed_sample_age_us / 1000) + ' ms old'}</td>
      <td>{layer.dirty_age_us != null ? Math.round(layer.dirty_age_us / 1000) + ' ms pending' : layer.dirty_to_display_us == null ? '—' : Math.round(layer.dirty_to_display_us / 1000) + ' ms displayed'}</td>
    </tr>)}</tbody></table>
  </details>;
}
