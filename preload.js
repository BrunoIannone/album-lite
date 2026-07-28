const { contextBridge, ipcRenderer, webUtils } = require('electron');
const { pathToFileURL } = require('node:url');

contextBridge.exposeInMainWorld('api', {
  exportPDF: (opts) => ipcRenderer.invoke('export-pdf', opts),
  pathForFile: (file) => webUtils.getPathForFile(file),
  urlForPath: (p) => pathToFileURL(p).href,
});
