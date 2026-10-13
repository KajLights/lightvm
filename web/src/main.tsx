import { createRoot } from 'react-dom/client';
import { Provider } from 'react-redux';
import { HashRouter } from 'react-router-dom';
import { App } from './App';
import { store } from './store';
import { runtime } from './runtime';
import '../style.css';

runtime.devices.start();
window.addEventListener('beforeunload', () => { runtime.compiler.stop(); void runtime.devices.stop(); });
createRoot(document.getElementById('root')!).render(<Provider store={store}><HashRouter><App /></HashRouter></Provider>);
