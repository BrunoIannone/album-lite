const { app, BrowserWindow, ipcMain, dialog } = require('electron');
const path = require('node:path');
const fs = require('node:fs/promises');

let win;

app.whenReady().then(() => {
  win = new BrowserWindow({
    width: 1500,
    height: 950,
    backgroundColor: '#1b1d21',
    webPreferences: { preload: path.join(__dirname, 'preload.js') },
  });
  win.loadFile('album.html');
});

app.on('window-all-closed', () => app.quit());

// ponytail: export = print the live DOM to PDF. Print CSS hides the UI and sizes each
// spread via @page, so images embed at full source resolution with zero compositing code.
ipcMain.handle('export-pdf', async (_e, { suggestedName }) => {
  const { canceled, filePath } = await dialog.showSaveDialog(win, {
    defaultPath: suggestedName || 'album.pdf',
    filters: [{ name: 'PDF', extensions: ['pdf'] }],
  });
  if (canceled || !filePath) return null;
  const pdf = await win.webContents.printToPDF({
    printBackground: true,
    preferCSSPageSize: true,
    margins: { marginType: 'none' },
  });
  await fs.writeFile(filePath, pdf);
  return filePath;
});
