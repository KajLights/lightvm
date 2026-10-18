import { createElement, useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { decodePicoUf2 } from './PicoFirmware';
import { Picoboot } from './vendor/picoflash/picoboot.js';
import { Target } from './vendor/picoflash/target.js';

type PicoBuild = { board: 'pico' | 'pico_w'; name: string; path: string; sha256: string; firmware_end: number };
const firmwareRoot = new URL('firmware/', document.baseURI);
const espManifestUrl = new URL('manifest.json', firmwareRoot).href;

export function Firmware({ usbConnected }: { usbConnected: boolean }) {
  const [espBoards, setEspBoards] = useState<string[]>([]);
  const [picoBuilds, setPicoBuilds] = useState<PicoBuild[]>([]);
  const [selectedPico, setSelectedPico] = useState('');
  const [version, setVersion] = useState('');
  const [error, setError] = useState('');
  const [progress, setProgress] = useState('');
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    let active = true;
    Promise.allSettled([
      fetch(espManifestUrl).then(async response => {
        if (!response.ok) throw new Error('ESP images unavailable');
        const manifest = await response.json();
        if (!Array.isArray(manifest.builds)) throw new Error('Invalid ESP manifest');
        if (active) { setEspBoards(manifest.builds.map((build: { chipFamily: string }) => build.chipFamily)); setVersion(manifest.version); }
      }),
      fetch(new URL('pico.json', firmwareRoot)).then(async response => {
        if (!response.ok) throw new Error('Pico images unavailable');
        const manifest = await response.json();
        if (!Array.isArray(manifest.builds) || manifest.builds.some((build: PicoBuild) => !['pico', 'pico_w'].includes(build.board))) throw new Error('Invalid Pico manifest');
        if (active) { setPicoBuilds(manifest.builds); setVersion(manifest.version); }
      }),
    ]).then(results => { if (active && results.every(result => result.status === 'rejected')) setError('Firmware images are not included in this editor build.'); });
    return () => { active = false; };
  }, []);

  async function installPico() {
    const build = picoBuilds.find(item => item.board === selectedPico);
    if (!build || busy || usbConnected) return;
    setError('');
    setProgress('Choose the RP2040 board in BOOTSEL mode.');
    setBusy(true);
    let controller: Picoboot | undefined;
    try {
      // Request the controller immediately while the click still grants WebUSB permission.
      controller = await Picoboot.requestDevice([new Target('RP2040')]);
      setProgress('Checking the firmware image…');
      const response = await fetch(new URL(build.path, firmwareRoot));
      if (!response.ok) throw new Error('Firmware image could not be downloaded.');
      const uf2 = new Uint8Array(await response.arrayBuffer());
      const digest = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', uf2))).map(value => value.toString(16).padStart(2, '0')).join('');
      if (digest !== build.sha256) throw new Error('Firmware image checksum did not match; nothing was written.');
      const image = decodePicoUf2(uf2, build.firmware_end);
      const connection = await controller.connect();
      await connection.resetInterface();
      await connection.setExclusiveAccess(2);
      await connection.exitXip();
      setProgress('Erasing the firmware area…');
      await connection.flashErase(0x10000000, Math.ceil(image.length / 4096) * 4096);
      for (let offset = 0; offset < image.length; offset += 4096) {
        await connection.flashWrite(0x10000000 + offset, image.subarray(offset, offset + 4096));
        setProgress(`Writing firmware… ${Math.round(Math.min(offset + 4096, image.length) / image.length * 100)}%`);
      }
      for (let offset = 0; offset < image.length; offset += 4096) {
        const expected = image.subarray(offset, offset + 4096);
        const actual = await connection.flashRead(0x10000000 + offset, expected.length);
        if (actual.length !== expected.length || expected.some((value, index) => actual[index] !== value)) {
          throw new Error(`Flash verification failed at 0x${(0x10000000 + offset).toString(16)}. Keep the board in BOOTSEL and retry.`);
        }
        setProgress(`Verifying firmware… ${Math.round(Math.min(offset + 4096, image.length) / image.length * 100)}%`);
      }
      setProgress('Firmware verified. Restarting the board…');
      try {
        await connection.reboot(100);
        setProgress(`${build.name} firmware installed and verified. Reconnect it from Controllers.`);
      } catch {
        setProgress(`${build.name} firmware installed and verified. Unplug and reconnect it, then open Controllers.`);
      }
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause));
      setProgress('');
    } finally {
      if (controller) await controller.disconnect().catch(() => {});
      setBusy(false);
    }
  }

  return <section className="firmware-page card">
    <p className="eyebrow">CONTROLLER SETUP</p>
    <h1>Firmware installation</h1>
    <p>Choose the exact physical board model before flashing. The browser can detect an ESP chip family or an RP2040 bootloader, but cannot distinguish Pico from Pico W.</p>
    {version && <p className="helper">Firmware build {version}</p>}
    {usbConnected && <p className="stale-banner">Disconnect the current USB session before flashing, then choose the controller again here.</p>}
    {espBoards.length > 0 && <div className="firmware-section">
      <h2>Seeed XIAO ESP32</h2>
      <p className="helper">Available: {espBoards.map(board => 'XIAO ' + board).join(', ')}. Each installer image includes Wi-Fi, HTTP, WebSocket, and boot-sequence support.</p>
      <ol className="firmware-steps"><li>Connect the supported XIAO board by USB and close any serial monitor.</li><li>Choose <strong>Install XIAO firmware</strong> and select its serial port.</li><li>After it restarts, connect from Controllers.</li></ol>
      {createElement('esp-web-install-button', { manifest: espManifestUrl },
        createElement('button', { slot: 'activate', className: 'primary', disabled: usbConnected || busy }, 'Install XIAO firmware'),
        createElement('span', { slot: 'unsupported', className: 'helper' }, 'Use a desktop browser with Web Serial, such as Chrome or Edge.'),
        createElement('span', { slot: 'not-allowed', className: 'helper' }, 'Open the editor over HTTPS or localhost to use Web Serial.'),
      )}
      <p className="helper">For an existing board, keep its data when the installer offers an erase choice.</p>
    </div>}
    {picoBuilds.length > 0 && <div className="firmware-section">
      <h2>Raspberry Pi Pico</h2>
      <p>Select the model printed on the board. Both use the same RP2040 bootloader, so the browser cannot check this choice.</p>
      <label htmlFor="pico-model">Board model</label>
      <select id="pico-model" value={selectedPico} onChange={event => setSelectedPico(event.target.value)} disabled={busy}>
        <option value="">Choose Pico or Pico W</option>
        {picoBuilds.map(build => <option key={build.board} value={build.board}>{build.name}</option>)}
      </select>
      <ol className="firmware-steps"><li>Disconnect the Pico, hold <strong>BOOTSEL</strong>, and reconnect its USB cable.</li><li>Select the exact board model above, then choose <strong>Install Pico firmware</strong>.</li><li>Choose <strong>RP2 Boot</strong> in the browser prompt. After verification, connect from Controllers.</li></ol>
      <button className="primary" onClick={installPico} disabled={!selectedPico || usbConnected || busy || !('usb' in navigator) || !window.isSecureContext}>Install Pico firmware</button>
      {selectedPico && <p className="helper">Or <a href={new URL(picoBuilds.find(build => build.board === selectedPico)!.path, firmwareRoot).href} download={`${selectedPico}-lightvm.uf2`}>download the UF2</a> and copy it to the RPI-RP2 drive.</p>}
      {(!('usb' in navigator) || !window.isSecureContext) && <p className="helper">Direct installation needs Chrome or Edge on HTTPS or localhost. You can still download the UF2 above.</p>}
    </div>}
    {progress && <p role="status" className="firmware-progress">{progress}</p>}
    {error && <p role="alert" className="stale-banner">{error}</p>}
    {(espBoards.length > 0 || picoBuilds.length > 0) && <p className="helper"><a href={new URL('THIRD_PARTY_NOTICES.md', firmwareRoot).href}>Firmware licenses and notices</a></p>}
    <Link className="button secondary" to="/controllers">Choose a controller</Link>
  </section>;
}
