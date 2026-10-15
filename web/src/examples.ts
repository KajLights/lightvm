import rainbow from '../../samples/python/rainbow.py?raw';
import twinkle from '../../samples/python/twinkle.py?raw';
import color from '../../samples/python/color.py?raw';
import pattern from '../../samples/python/pattern.py?raw';
import wipe from '../../samples/python/wipe.py?raw';
import chase from '../../samples/python/chase.py?raw';

export const EXAMPLES = [
  { id: 'rainbow', filename: 'rainbow.py', label: 'rainbow.py', source: rainbow,
    description: 'Original picolights Rainbow: one colour cycle every 60 seconds. Lower duration in Preparation settings for faster motion.' },
  { id: 'twinkle', filename: 'twinkle.py', label: 'twinkle.py', source: twinkle,
    description: 'Stars appear at random positions and fade over the background colour.' },
  { id: 'color', filename: 'color.py', label: 'color.py', source: color,
    description: 'A steady colour across the whole strip.' },
  { id: 'pattern', filename: 'pattern.py', label: 'pattern.py', source: pattern,
    description: 'A steady colour pattern, stretched to fit the strip by default.' },
  { id: 'wipe', filename: 'wipe.py', label: 'wipe.py', source: wipe,
    description: 'Fills from both ends towards the centre, then completes and holds. At the default speed, 200 pixels fill in about 2 seconds. Apply settings & restart to watch it again.' },
  { id: 'chase', filename: 'chase.py', label: 'chase.py', source: chase,
    description: 'Fills from the first pixel to the last, keeping earlier pixels lit. At the default speed, 200 pixels fill in about 4 seconds; later cycles leave the strip solid. Apply settings & restart to watch it again.' },
];

export const DEFAULT_EXAMPLE = EXAMPLES[0];
export function exampleForSource(source: string) {
  return EXAMPLES.find(example => example.source === source);
}
