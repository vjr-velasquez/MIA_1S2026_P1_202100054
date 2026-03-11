# MIA Proyecto 1 — Simulación EXT2 sobre archivos `.mia` (C++17 + HTTP + Frontend)

Proyecto académico de **Manejo e Implementación de Archivos (MIA)** que simula operaciones de disco y un sistema de archivos tipo **EXT2** dentro de archivos binarios `.mia`, con una arquitectura cliente-servidor:

- **Backend** en C++17 (CMake)
- **API HTTP** con `cpp-httplib` (`:8080`)
- **Frontend web** (HTML + JS) para ejecutar scripts `.smia`

---

## 1) Resumen del proyecto

Este repositorio implementa un intérprete de comandos para administrar discos/particiones y operaciones básicas sobre un EXT2 simulado.

### API disponible

- `GET /health` → responde `OK`
- `POST /execute` con:
  ```json
  { "commands": "mkdisk ...\nfdisk ...\n..." }
  ```
  responde:
  ```json
  { "output": "OK: ...\nERROR: ...\n" }
  ```

### Estilo de salida

El backend retorna mensajes con prefijo:

- `OK: ...`
- `ERROR: ...`

---

## 2) Estado por sprints (Sprint 1 → 4)

### Sprint 1–2
- `mkdisk`, `rmdisk`
- `fdisk` primaria / extendida / lógica (cadena EBR)
- `mount`, `mounted` (IDs tipo `vda1`, `vda2`, ...)
- `rep` (`mbr`, `disk`)

### Sprint 3
- Frontend web conectado a `/execute`
- CORS habilitado
- Carga y ejecución de scripts `.smia`

### Sprint 4 (100%)
- `mkfs` EXT2 mínimo:
  - superbloque
  - bitmaps
  - inodos/bloques
  - raíz `/`
  - `users.txt`
- `mkdir` (`-p`) con soporte multi-bloque en directorios
- `mkfile` (`-cont`)
- `cat`
- `chmod` (`-perm=###`)
- `chown` (`-uid`, `-gid` opcional)

---

## 3) Arquitectura y estructura de carpetas

```text
.
├── backend
│   ├── CMakeLists.txt
│   ├── include
│   │   ├── api
│   │   ├── parser
│   │   ├── commands
│   │   ├── disk
│   │   ├── fs
│   │   ├── reports
│   │   ├── structs
│   │   └── util
│   ├── src
│   │   ├── api
│   │   ├── parser
│   │   ├── commands
│   │   ├── disk
│   │   ├── fs
│   │   ├── reports
│   │   └── util
│   └── vendor
│       ├── httplib.h
│       └── json.hpp
├── frontend
│   ├── index.html
│   └── app.js
└── outputs
```

---

## 4) Requisitos

Instalar en Linux:

- `g++` con soporte C++17
- `cmake` (>= 3.16 recomendado)
- `python3` (para servir frontend estático)

Comandos de verificación:

```bash
g++ --version
cmake --version
python3 --version
```

---

## 5) Cómo compilar y ejecutar backend

Desde la raíz del repo:

```bash
cmake -S backend -B build
cmake --build build -j
./build/mia_backend
```

Servidor esperado:

- `http://localhost:8080`
- Health:
  - `GET /health` → `OK`

---

## 6) Cómo levantar frontend

En otra terminal:

```bash
cd frontend
python3 -m http.server 5500
```

Abrir en navegador:

- `http://localhost:5500`

En la UI:
- Configura `Backend URL` como `http://localhost:8080`
- Usa **Health**, carga `.smia`, ejecuta y revisa salida

---

## 7) Ejemplos de uso (para pegar en el frontend)

> Nota: el ID de mount **puede variar**. Ejecuta `mounted` para ver el ID real.

### Script base de flujo completo

```txt
# 1) Crear disco
mkdisk -size=20 -unit=M -fit=F -path="/tmp/mia/proyecto1.mia"

# 2) Crear partición primaria
fdisk -size=10 -unit=M -type=P -fit=W -path="/tmp/mia/proyecto1.mia" -name="prim1"

# 3) Montar partición y revisar ID
mount -path="/tmp/mia/proyecto1.mia" -name="prim1"
mounted

# 4) Formatear EXT2 (reemplaza vda1 según mounted)
mkfs -id=vda1

# 5) Directorios y archivos
mkdir -id=vda1 -path="/docs" -p
mkfile -id=vda1 -path="/docs/nota.txt" -cont="Hola MIA desde EXT2"

# 6) Lectura
cat -id=vda1 -path="/docs/nota.txt"

# 7) Permisos y propietario
chmod -id=vda1 -path="/docs/nota.txt" -perm=664
chown -id=vda1 -path="/docs/nota.txt" -uid=2 -gid=2

# 8) Verificar contenido sigue disponible
cat -id=vda1 -path="/docs/nota.txt"
```

---

## 8) Pruebas rápidas (smoke tests + errores)

### Smoke test mínimo

```txt
mkdisk -size=10 -unit=M -path="/tmp/mia/smoke.mia"
fdisk -size=5 -unit=M -type=P -path="/tmp/mia/smoke.mia" -name="p1"
mount -path="/tmp/mia/smoke.mia" -name="p1"
mounted
mkfs -id=vda1
mkdir -id=vda1 -path="/a/b" -p
mkfile -id=vda1 -path="/a/b/hola.txt" -cont="smoke ok"
cat -id=vda1 -path="/a/b/hola.txt"
chmod -id=vda1 -path="/a/b/hola.txt" -perm=600
chown -id=vda1 -path="/a/b/hola.txt" -uid=10
```

### Casos de error esperados

```txt
# sin mount válido
mkfs -id=vdz9

# path inexistente
cat -id=vda1 -path="/no/existe.txt"

# duplicado
mkfile -id=vda1 -path="/a/b/hola.txt" -cont="x"
mkfile -id=vda1 -path="/a/b/hola.txt" -cont="y"

# chmod/chown inválidos
chmod -id=vda1 -path="/a/b/hola.txt" -perm=99
chown -id=vda1 -path="/a/b/hola.txt" -uid=abc
```

---

## 9) Notas y limitaciones actuales

- No hay sistema de autenticación (`login`) todavía.
- `chmod` y `chown` son directos sobre inodo (sin validación de usuario activo).
- `mkfile` usa bloques directos de inodo (límite práctico según implementación actual).
- `cat` lee contenido desde bloques directos.
- La sintaxis acepta parámetros `-param=valor`, comentarios con `#`, y líneas vacías.
- Para rutas/valores con espacios, usar comillas.

---

## 10) Comandos soportados (resumen)

- Disco/particiones: `mkdisk`, `rmdisk`, `fdisk`, `mount`, `mounted`, `rep`
- FS EXT2 simulado: `mkfs`, `mkdir`, `mkfile`, `cat`, `chmod`, `chown`

---

## 11) Recomendación de uso en evaluación

1. Ejecutar `mounted` antes de `mkfs`/`mkdir`/`mkfile`/`cat`/`chmod`/`chown`.
2. Trabajar con rutas bajo `/tmp/mia/` para evitar problemas de permisos.
3. Revisar salida del backend siempre por prefijos `OK:` y `ERROR:`.
