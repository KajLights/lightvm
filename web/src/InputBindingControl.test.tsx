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
