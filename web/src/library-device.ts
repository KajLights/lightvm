import type { LightVMClient } from '../protocol.mjs';
import { crc32 } from '../protocol.mjs';
import { programCompatibility } from './compiler';
import { bytesFor, deviceLayers, digestFor, programName, type Library, type PresetLayer } from './library';
import type { Procedure } from './types';

// Both the composer and the fader prepare the same saved, checked program
// versions. Upload only missing programs before admitting a scene candidate.
export async function prepareLibraryScene(client: LightVMClient, library: Library, layers: PresetLayer[]) {
  const recipe = deviceLayers(library, layers);
  const target = await client.describe();
  if (!target.scenes) throw new Error('Install firmware with scene composition support.');
  if (layers.length > target.scene_layer_limit) throw new Error('This output supports up to ' + target.scene_layer_limit + ' layers.');
  const animations = [...new Set(layers.map(layer => layer.animation))].map(id => library.animations.find(item => item.id === id)!);
  const stored = (await client.json('procedure_list', { type: 'animation' })) as Procedure[];
  for (const animation of animations) {
    const bytes = bytesFor(animation);
    if (await digestFor(bytes) !== animation.digest) throw new Error('The saved animation checksum differs. Import an intact library backup.');
    const problem = programCompatibility(target, animation.manifest);
    if (problem) throw new Error(animation.name + ': ' + problem);
    if (bytes.length > target.module_limit) throw new Error(animation.name + ' exceeds this device’s program limit.');
    const match = stored.find(item => item.name === programName(animation));
    if (match && (match.crc32 !== crc32(bytes) || match.size !== bytes.length)) throw new Error('A stored animation version differs from the library.');
    if (!match) {
      await client.saveProcedure(programName(animation), bytes);
      stored.push({ name: programName(animation), size: bytes.length, crc32: crc32(bytes) });
    }
  }
  return recipe;
}
