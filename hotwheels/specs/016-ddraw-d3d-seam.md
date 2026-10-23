# Seam DDraw → D3D (HW-026 / HW-010)

DDraw conserva la ruta P8 cuando no se registran hooks D3D. Hooks opcionales retornan el objeto D3D3 externo, Texture2 para una superficie COM canónica Surface4 y si Flip fue consumido por D3D. DDraw expone render_target, texture_source e is_primary para ComHostOps; rechaza handles inválidos. El dueño de ComBridge controla su lifetime y lo instala después de DDraw y antes de Bridge::link.

Las texturas DDSCAPS_TEXTURE se crean con su formato solicitado (PAL8, RGB565, ARGB1555, ARGB4444 o ARGB8888), pitch y memoria guest. Los formatos RGB incompatibles se rechazan. Lookup devuelve píxeles/paleta prestados, colorkey completo y generación incrementada sólo por Unlock exitoso. QI Texture2 de una superficie no textura falla. Flip D3D consumido no llama P8 ni intercambia buffers software; si no se consume conserva el comportamiento P8.

Aceptación: tests COM prueban lookup válido/inválido, QI externo, textura1555 con Lock/Unlock/generación/colorkey, y prioridad/fallback de Flip. Rebuild host y tests anteriores verdes. El seam no instala renderer ni demuestra carrera real: install_all/backend pertenece a integ y ComBridge a render. No cambia winx86 ni audio.

Los callbacks externos de QI devuelven una referencia COM adquirida; DDraw no modifica los refcounts ajenos. SetEntries también invalida generación de texturas PAL8 para impedir uploads de paleta obsoletos. SetDisplayMode admite modos observados320×240,640×480,800×600 en8/16bpp. Prueba real ComBridge+RefRaster instalada tras DDraw atraviesa QI/CreateDevice/Texture2/Flip.

render_target sólo acepta la primaria activa o su backbuffer asociado. Una primaria de un modo cerrado por RestoreDisplayMode no puede convertirse en target D3D, aunque su objeto COM todavía tenga referencias.
