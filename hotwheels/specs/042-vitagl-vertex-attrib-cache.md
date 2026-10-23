# HW-065 — cachear habilitación de atributos del vertex shader D3D

## Problema

`VitaGLBackend::draw()` llama a `glEnableVertexAttribArray(0..3)` para cada Draw. VitaGL guarda esos bits en el VAO y sólo cambia el layout al pasar de deshabilitado a habilitado; entre draws, las cuatro llamadas vuelven a entrar a la API aunque el estado siga activo. La fuente local de VitaGL confirma que `vglSwapBuffers()` no resetea el VAO. El presenter P8 sólo habilita atributos 0 y 1 y tampoco deshabilita 2/3.

## Contrato observable

En el contexto VitaGL compartido por D3D y P8, cada atributo utilizado por el shader D3D se habilita sólo si aún no está activo. Los punteros de atributo siguen actualizándose por Draw, porque apuntan a vectores CPU que pueden relocalizarse al crecer. Las capturas D3D y el blit P8 permanecen pixel-equivalentes.

## Criterios verificables

- Test host del cache puro modela D3D→P8→D3D: primer D3D habilita 0–3; P8 vuelve a habilitar 0/1 y cambia sus punteros sin deshabilitar bits; el siguiente D3D no vuelve a emitir enables y sí refresca los cuatro punteros. Un contexto nuevo arranca con enable mask vacío.
- Cubrir también P8→D3D frío: el cache D3D no recibe estado P8 y puede repetir enable de 0/1 en el primer draw; ese costo único de hasta dos llamadas es aceptado.
- Vita3K conserva capturas P8 de menú y D3D de carrera respecto del build base, y una salida/reentrada al menú muestra ambos modos después de la secuencia D3D→P8; los tests golden independientes D3D/P8 permanecen iguales.
- En Vita3K con `race_enter.script`, telemetría reporta habilitaciones reales y draws por corrida: pasa de 4 llamadas/Draw a como máximo 4 por contexto. El ahorro acumulado debe superar 99 % después del primer Draw.
- Build Vita y escena D3D/P8 visible sin crash; golden host D3D intacto. Reportar guest frames/s antes/después bajo la misma carga, sin afirmar causalidad si la variación supera el ahorro.

## Límites y no objetivos

No cachear `glVertexAttribPointer`: cambia con cada buffer/vector y puede relocalizarse. No tocar el VAO del presenter ni el código vendorizado de VitaGL. El código Hot Wheels no hace `glBindVertexArray` ni deshabilita atributos: el cache es propiedad del único contexto VitaGL creado junto al backend. Si una futura ruta modifica la máscara/VAO, esa transición deberá invalidar el cache antes de habilitar el atributo siguiente. El cache sólo lo consulta el caller serializado por `vita_gl_mutex`; no se comparte entre hilos sin ese lock.

## Razonamiento de ABI/estado

`glEnableVertexAttribArray` modifica estado persistente del VAO, no consume el puntero de vertex data ni publica memoria guest. Por eso su eliminación repetida no cambia el draw ABI; el descriptor actualizado por `glVertexAttribPointer` permanece necesario. VitaGL actualiza `vertex_layout_version` sólo cuando cambia el bit de enable. `VitaPresenter` comparte el contexto bajo el mismo mutex; su secuencia blit sólo habilita 0/1 y cambia sus punteros. La reducción es de llamadas/API-state checks en el hilo dueño de GL, no de tráfico de vértices ni de trabajo de GPU.
