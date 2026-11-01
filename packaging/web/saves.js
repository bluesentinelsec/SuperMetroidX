// SDL keeps the game's save folder under /libsdl. In the browser that folder
// is IndexedDB storage, loaded before the game starts, so SRAM, settings, and
// save states outlast the page. The shell copies changes back as it saves.
Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(function () {
  FS.mkdir('/libsdl');
  FS.mount(IDBFS, {}, '/libsdl');
  addRunDependency('smx-saves');
  FS.syncfs(true, function (error) {
    if (error) {
      console.warn('SuperMetroidX: loading saves from IndexedDB failed', error);
    }
    removeRunDependency('smx-saves');
  });
});
