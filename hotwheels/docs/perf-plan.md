# Plan de rendimiento a bajo nivel (orquestador, 2026-10-03)

## Premisas medidas o inferidas
- Consola real: el usuario ve "bajos frames". Indirecto [consola]: el audio recibe el 61–64 % de las muestras en tiempo real, así que el guest corre a ~60 % de su velocidad (~24 de 40 Hz).
- El dynarec es Box86: ya tiene block linking directo, flags diferidos y memcpy/memset intrínsecos con NEON. Poco margen fácil en el traductor genérico.
- El juego es de 2000 y monohilo, con matemática en **x87** (vértices `float` en DrawPrimitive, `physics.dat` en floats). El x87 se emula con una pila de 8 registros sobre VFP: hay que sincronizar el TOP, cargar y guardar la pila al entrar y salir del bloque y convertir precisión.
- En modo `original`, el replay de D3D y el present corren en el hilo del guest (sólo `interp60` tiene `present_thread`).

## Principio (Amdahl)
Sin medir *dónde* está el guest no se optimiza: S = 1 / ((1-p) + p/k). La primera herramienta es un **perfilador por muestreo del EIP del guest**: es barato, O(1) por muestra, y en qemu y Vita3K da la *distribución relativa* de tiempo por función aunque el tiempo absoluto no sea el de la consola.

## Palancas, de mayor a menor impacto esperado
1. **HW-051 — perfilador de EIP guest (runtime).** Un hilo en otro core lee cada ~1 ms el EIP del bloque en ejecución (variable publicada por el dispatcher, atómica relajada) y arma un histograma por función usando los nombres de Ghidra (`shared/ghidra-proyectos/HotWheels`). Salida: top-30 con % por escena. También el % de muestras dentro de shims (host) contra el guest. Validar en qemu y Vita3K; en consola con la misma build.
2. **HW-052 — HLE de funciones calientes (opus).** Por cada función que pase el 3 % del perfil: reemplazarla por una implementación nativa ARM/NEON con la semántica exacta (memoria, flags y FPU afectados), enganchada en su dirección como hacen los intrínsecos de memcpy. Candidatos típicos: rasterización o blit por software 8bpp del menú, transformaciones de vértices, decodificación SFP/TGA, búsquedas en tablas (`sinus.sin`, `shade.tab`), y bucles de física. Cada HLE con test diferencial (traducción contra nativo sobre entradas aleatorias y reales) y bandera para desactivarlo.
3. **HW-053 — paralelismo de pipeline (render).** Sacar el replay D3D, el armado de texturas y el `vglSwap` del hilo del guest en *todos* los modos. Un productor (guest, core 0) graba al ring y un consumidor (core 1) dibuja. Buffer doble o triple con *fences*; la latencia sube un frame. Por la ley de Amdahl, si el render pesa r del frame, la velocidad crece ≈ 1/(1-r).
4. **HW-054 — x87 (runtime).** Auditar en Box86 la caché de la pila x87 (`fpu_pushcache`) en los bloques calientes del perfil. Si el TOP se vuelca en cada frontera de bloque, agrandar los superbloques (trazas más largas por *hot path*) y considerar precisión simple cuando el control word del juego lo permita (FLDCW con precisión de 24 bits es común en juegos D3D).
5. **HW-055 — caché de texturas por contenido (render).** Hash rápido (xxhash32 o CRC por NEON) de los bytes y la paleta de cada TexUpload; si coincide, se reutiliza la textura de GPU. Así se evitan subidas por frame. Medir uploads/frame antes y después.
6. **HW-056 — busy-wait del timer (runtime).** El juego espera activo hasta los 25 ms (`0x412df0`, QPC). Si el guest va por *delante* del tiempo, detectar el patrón (lectura de QPC en un bucle cerrado) y convertirlo en `sceKernelDelayThread` del tiempo que falta. Libera el core y el GIL para audio y render; baja consumo y temperatura.
7. **Sistema:** CPU a 444 MHz, GPU a 222, Xbar a 166 y bus a 222 (en curso); afinidad fija guest=core0, render=core1, audio=core2; prioridades sin inversión (audio > render > guest). El GIL se toma sólo en las fronteras de shim, nunca durante el replay.

## Método
Para cada palanca: hipótesis → medición antes (perfil + fps-window) → cambio → medición después, en legs intercalados (A/B como en D2Vita). Nivel de evidencia explícito. Se conserva sólo lo que mejora sin romper los tests diferenciales.
