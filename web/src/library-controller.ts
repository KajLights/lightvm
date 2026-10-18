import type { LightVMClient } from '../protocol.mjs';
import { crc32 } from '../protocol.mjs';
import { programCompatibility } from './compiler';
import { bytesFor, compositionLayers, digestFor, programName, type Library, type PresetLayer } from './library';
import type { Program } from './types';

// Both the composer and the fader prepare the same saved, checked program
// versions. Upload only missing programs before admitting a composition candidate.
export async function prepareLibraryComposition(client: LightVMClient, library: Library, layers: PresetLayer[]) {
  const recipe = compositionLayers(library, layers);
  const target = await client.describe();
  if (!target.compositions) throw new Error('Install firmware with composition support.');
  if (layers.length > target.composition_layer_limit) throw new Error('This output supports up to ' + target.composition_layer_limit + ' layers.');
  const animations = [...new Set(layers.map(layer => layer.animation))].map(id => library.animations.find(item => item.id === id)!);
  const stored = (await client.json('program_list', { type: 'animation' })) as Program[];
  for (const animation of animations) {
    const bytes = bytesFor(animation);
    if (await digestFor(bytes) !== animation.digest) throw new Error('The saved animation checksum differs. Import an intact library backup.');
    const problem = programCompatibility(target, animation.manifest);
    if (problem) throw new Error(animation.name + ': ' + problem);
    if (bytes.length > target.module_limit) throw new Error(animation.name + ' exceeds this controller’s program limit.');
    const match = stored.find(item => item.name === programName(animation));
    if (match && (match.crc32 !== crc32(bytes) || match.size !== bytes.length)) throw new Error('A stored animation version differs from the library.');
    if (!match) {
      await client.saveProgram(programName(animation), bytes);
      stored.push({ name: programName(animation), size: bytes.length, crc32: crc32(bytes) });
    }
  }
  return recipe;
}
