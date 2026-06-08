# Space Station

Space Station is a lightweight toolbox shell with a Vue/Vite frontend and a Drogon-based C++ backend.

The current scaffold keeps only the JSON formatter feature. Other tool entries are represented as disabled placeholders so the app structure is ready for future modules without shipping unfinished behavior.

## Frontend

```powershell
cd frontend
npm install
npm run dev
```

## Backend

Build the frontend first if you want Drogon to serve the bundled UI from `/web/`.

```powershell
cd frontend
npm install
npm run build

cd ..\backend
cmake --preset x64-debug-vcpkg
cmake --build --preset x64-debug-vcpkg
.\out\build\x64-debug-vcpkg\space-station.exe
```

The backend listens on `127.0.0.1:34827` by default.
