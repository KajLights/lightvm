import { createContext, useContext } from 'react';
import type { InputBindings } from './types';
import type { SettingsValues } from './program';

export const PreviewRecipe = createContext<{
  settings: SettingsValues;
  inputs: InputBindings;
  setInputs: (inputs: InputBindings) => void;
}>({ settings: {}, inputs: {}, setInputs: () => {} });
export const usePreviewRecipe = () => useContext(PreviewRecipe);
