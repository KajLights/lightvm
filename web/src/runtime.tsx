import { createContext, useContext } from 'react';
import { store } from './store';
import { Compiler } from './compiler';
import { ControllerService } from './controller-service';

export const runtime = { controllers: new ControllerService(store), compiler: new Compiler(store) };
export const RuntimeContext = createContext(runtime);
export function useRuntime() { return useContext(RuntimeContext); }
