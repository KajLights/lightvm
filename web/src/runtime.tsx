import { createContext, useContext } from 'react';
import { store } from './store';
import { Compiler } from './compiler';
import { DeviceService } from './device-service';

export const runtime = { devices: new DeviceService(store), compiler: new Compiler(store) };
export const RuntimeContext = createContext(runtime);
export function useRuntime() { return useContext(RuntimeContext); }
