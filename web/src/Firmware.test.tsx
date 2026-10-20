import { afterEach, describe, expect, it, vi } from 'vitest';
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { MemoryRouter } from 'react-router-dom';
import { Firmware } from './Firmware';

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

function showFirmware() {
  vi.stubGlobal('fetch', vi.fn(async (url: string | URL) => ({
    ok: String(url).endsWith('pico.json'),
    json: async () => ({ version: '1.0.0', builds: [
      { board: 'pico_w', chip: 'RP2040', name: 'Pico W', path: 'pico_w/lightvm.uf2', firmware_end: 0x1017e000, sha256: 'unused' },
      { board: 'pico2_w', chip: 'RP2350', name: 'Pico 2 W', path: 'pico2_w/lightvm.uf2', firmware_end: 0x1037e000, sha256: 'unused' },
    ] }),
  })));
  render(<MemoryRouter><Firmware usbConnected={false} /></MemoryRouter>);
}

describe('Pico firmware selection', () => {
  it('offers a board-matched Pico 2 W download and RP2350 instructions', async () => {
    showFirmware();
    await screen.findByRole('option', { name: 'Pico 2 W' });
    fireEvent.change(screen.getByLabelText('Board model'), { target: { value: 'pico2_w' } });
    const download = screen.getByRole('link', { name: 'Download the Pico 2 W UF2' });
    expect(download.getAttribute('href')).toMatch(/pico2_w\/lightvm.uf2$/);
    expect(download).toHaveAttribute('download', 'pico2_w-lightvm.uf2');
    expect(screen.getByText('RP2350', { selector: 'strong' })).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'Install Pico firmware' })).toBeNull();
  });

  it('retains the RP2040 installer and drive name when switching back to Pico W', async () => {
    showFirmware();
    await screen.findByRole('option', { name: 'Pico 2 W' });
    const model = screen.getByLabelText('Board model');
    fireEvent.change(model, { target: { value: 'pico2_w' } });
    fireEvent.change(model, { target: { value: 'pico_w' } });
    expect(screen.getByRole('button', { name: 'Install Pico firmware' })).toBeInTheDocument();
    expect(screen.getByRole('link', { name: 'Download the Pico W UF2' }).getAttribute('href')).toMatch(/pico_w\/lightvm.uf2$/);
    expect(screen.getByText(/and copy it to the RPI-RP2 drive/)).toBeInTheDocument();
  });
});
