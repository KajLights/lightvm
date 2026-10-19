import type { Manifest } from './types';
import type { SettingsValues } from './program';

export function SettingFields({ manifest, values, onChange, prefix }: { manifest: Manifest; values: SettingsValues; onChange: (values: SettingsValues) => void; prefix: string }) {
  return <div className="setting-list">{(manifest.settings ?? []).map(setting => {
    const enabled = Object.hasOwn(values, setting.name), value = values[setting.name];
    const id = prefix + '-' + setting.name;
    return <div className="setting-row" key={setting.name}>
      <label className="setting-toggle"><input type="checkbox" checked={enabled} aria-label={'Override ' + setting.name} onChange={event => {
        const next = { ...values };
        if (event.target.checked) next[setting.name] = setting.type === 'bool' ? false : setting.minimum ?? 0;
        else delete next[setting.name];
        onChange(next);
      }} />Override {setting.name}</label>
      <label className="sr-only" htmlFor={id}>{setting.name}</label>
      {setting.type === 'bool' ? <select id={id} disabled={!enabled} value={enabled ? String(value) : 'default'} onChange={event => onChange({ ...values, [setting.name]: event.target.value === 'true' })}>
        {!enabled && <option value="default">Program default</option>}<option value="true">True</option><option value="false">False</option>
      </select> : <input id={id} disabled={!enabled} type={setting.type === 'rgb8' && enabled ? 'color' : 'number'}
        value={!enabled ? '' : setting.type === 'rgb8' ? '#' + Number(value).toString(16).padStart(6, '0') : Number.isNaN(value) ? '' : String(value)}
        placeholder="Program default" min={setting.minimum} max={setting.maximum} step={setting.type === 'float' ? 'any' : 1}
        onChange={event => onChange({ ...values, [setting.name]: setting.type === 'rgb8' ? parseInt(event.target.value.slice(1), 16) : event.target.value === '' ? NaN : Number(event.target.value) })} />}
    </div>;
  })}</div>;
}

