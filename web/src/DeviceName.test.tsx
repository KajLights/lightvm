import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import { Provider } from 'react-redux';
import { afterEach, expect, it } from 'vitest';
import { DeviceName } from './DeviceName';
import { createAppStore, controllerActions } from './store';
import type { Description } from './types';

const description = { controller_id: 'board', boot_id: 'boot', board: 'Pico W', nickname: 'Shelf', metadata_revision: 1,
  features: ['device_metadata'], storage_available: true, storage_writable: true } as Description;
afterEach(cleanup);
function setup() {
  const store = createAppStore();
  store.dispatch(controllerActions.connections({ paths: [{ key: 'usb', label: 'USB' }], selectedPath: 'usb', description }));
  render(<Provider store={store}><DeviceName /></Provider>);
  return store;
}
it('uses UTF-8 byte capacity and allows clearing the nickname', () => {
  setup();
  fireEvent.change(screen.getByLabelText('Nickname'), { target: { value: 'é'.repeat(32) } });
  expect(screen.getByRole('button', { name: 'Save name' })).toBeDisabled();
  fireEvent.change(screen.getByLabelText('Nickname'), { target: { value: '' } });
  expect(screen.getByRole('button', { name: 'Save name' })).toBeEnabled();
});
it('keeps a draft but blocks saving after a device boot or metadata revision changes', () => {
  const store = setup();
  fireEvent.change(screen.getByLabelText('Nickname'), { target: { value: 'Draft' } });
  act(() => { store.dispatch(controllerActions.description({ ...description, metadata_revision: 2, nickname: 'Another browser' })); });
  expect(screen.getByLabelText('Nickname')).toHaveValue('Draft');
  expect(screen.getByRole('button', { name: 'Save name' })).toBeDisabled();
  fireEvent.click(screen.getByRole('button', { name: 'Reload name' }));
  expect(screen.getByLabelText('Nickname')).toHaveValue('Another browser');
  fireEvent.change(screen.getByLabelText('Nickname'), { target: { value: 'New draft' } });
  act(() => { store.dispatch(controllerActions.description({ ...description, boot_id: 'next-boot' })); });
  expect(screen.getByRole('button', { name: 'Save name' })).toBeDisabled();
});
