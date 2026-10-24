import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import { InputBindingControl } from './InputBindingControl';
afterEach(cleanup);
it('offers full RGB light colour and explicit numeric music palettes for the same input', () => {
  const publish = vi.fn();
  render(<InputBindingControl declaration={{ name: 'eye', type: 'rgb8', default: 0xffffff, control: 'color' }} binding={{ control: 'color' }} value={0xffffff} publish={publish} />);
  expect(screen.getByLabelText('eye driver')).toHaveValue('control:color');
  fireEvent.change(screen.getByLabelText('eye driver'), { target: { value: 'music.bass' } });
  expect(publish).toHaveBeenLastCalledWith({ source: 'music.bass', palette: { input_min: 0, input_max: 1, from: 0x0044ff, to: 0xff4400 } });
  fireEvent.change(screen.getByLabelText('eye driver'), { target: { value: 'control:color' } });
  expect(publish).toHaveBeenLastCalledWith({ control: 'color' });
});
it('maps normalized music to declared local behavior bounds and preserves default restoration', () => {
  const publish = vi.fn();
  render(<InputBindingControl declaration={{ name: 'width', type: 'float', default: 0.1, minimum: 0.02, maximum: 0.4 }} binding={{ value: 0.1 }} value={0.1} publish={publish} />);
  expect(screen.queryByRole('option', { name: 'Light brightness (behaviour)' })).toBeNull();
  fireEvent.change(screen.getByLabelText('width driver'), { target: { value: 'music.bass' } });
  expect(publish).toHaveBeenLastCalledWith({ source: 'music.bass', map: { input_min: 0, input_max: 1, output_min: 0.02, output_max: 0.4 } });
  fireEvent.click(screen.getByText('Restore default'));
  expect(publish).toHaveBeenLastCalledWith({ default: true });
});
it('offers board sensors with units and maps their physical range into the input range', () => {
  const publish = vi.fn();
  render(<InputBindingControl declaration={{ name: 'heat', type: 'float', default: 0, minimum: 0, maximum: 1 }} binding={{ value: 0 }} value={0} publish={publish}
    sources={[{ name: 'board.temperature_c', origin: 'board', label: 'Chip temperature', unit: '°C', type: 'float', valid: false, value: null, age_ms: null, suggested_min: 20, suggested_max: 80 }]} />);
  expect(screen.getByRole('option', { name: 'Board → Chip temperature (°C)' })).toBeInTheDocument();
  fireEvent.change(screen.getByLabelText('heat driver'), { target: { value: 'board.temperature_c' } });
  expect(publish).toHaveBeenLastCalledWith({ source: 'board.temperature_c', map: { input_min: 20, input_max: 80, output_min: 0, output_max: 1 } });
});
it('changes physical source ranges while retaining output mapping and fallback', () => {
  const publish = vi.fn();
  const temperature = { name: 'board.temperature_c', origin: 'board' as const, type: 'float' as const, valid: true, value: 50, age_ms: 0, suggested_min: 20, suggested_max: 80 };
  const voltage = { ...temperature, name: 'board.supply_voltage_v', suggested_min: 0, suggested_max: 6 };
  const declaration = { name: 'heat', type: 'float' as const, default: 0, minimum: 0, maximum: 1 };
  const binding = { source: 'music.level', timeout_ms: 3000, fallback: 'hold' as const, map: { input_min: 0.2, input_max: 0.8, output_min: 0.1, output_max: 0.9 } };
  const view = render(<InputBindingControl declaration={declaration} binding={binding} value={0.5} publish={publish} sources={[temperature, voltage]} />);
  fireEvent.change(screen.getByLabelText('heat driver'), { target: { value: temperature.name } });
  expect(publish).toHaveBeenLastCalledWith({ ...binding, source: temperature.name, map: { ...binding.map, input_min: 20, input_max: 80 } });
  const heat = { ...binding, source: temperature.name, map: { ...binding.map, input_min: 30, input_max: 70 } };
  view.rerender(<InputBindingControl declaration={declaration} binding={heat} value={0.5} publish={publish} sources={[temperature, voltage]} />);
  fireEvent.change(screen.getByLabelText('heat driver'), { target: { value: voltage.name } });
  expect(publish).toHaveBeenLastCalledWith({ ...heat, source: voltage.name, map: { ...heat.map, input_min: 0, input_max: 6 } });
  fireEvent.change(screen.getByLabelText('heat driver'), { target: { value: 'music.bass' } });
  expect(publish).toHaveBeenLastCalledWith({ ...heat, source: 'music.bass', map: { ...heat.map, input_min: 0, input_max: 1 } });
});
it('changes palette source ranges without replacing chosen colours', () => {
  const publish = vi.fn();
  const binding = { source: 'music.level', palette: { input_min: 0, input_max: 1, from: 0x123456, to: 0xabcdef } };
  render(<InputBindingControl declaration={{ name: 'colour', type: 'rgb8', default: 0 }} binding={binding} value={0} publish={publish}
    sources={[{ name: 'board.temperature_c', origin: 'board', type: 'float', valid: true, value: 50, age_ms: 0, suggested_min: 20, suggested_max: 80 }]} />);
  fireEvent.change(screen.getByLabelText('colour driver'), { target: { value: 'board.temperature_c' } });
  expect(publish).toHaveBeenLastCalledWith({ ...binding, source: 'board.temperature_c', palette: { ...binding.palette, input_min: 20, input_max: 80 } });
});
