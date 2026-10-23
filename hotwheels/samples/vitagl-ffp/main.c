/*
 * vitagl-ffp — minimal fixed-function (FFP) sample for HW-011.
 *
 * Exercises exactly the D3D6 fixed-function surface we depend on, mapped to
 * OpenGL through vitaGL:
 *   - immediate-mode textured triangle (GL_TRIANGLES + texcoords)
 *   - alpha test (D3D6 color key / D3DRS_ALPHATESTENABLE)
 *   - linear fog on eye-space Z (D3DRS_FOGENABLE / FOGTABLEMODE)
 *   - depth test
 *
 * No external assets: the RGBA texture is generated procedurally, with a hard
 * radial alpha edge so alpha test is visibly exercised.
 */
#include <vitasdk.h>
#include <vitaGL.h>
#include <stdio.h>
#include <string.h>

#define TEXW 64
#define TEXH 64
static uint32_t g_tex[TEXW * TEXH];

static void make_texture(void) {
	for (int y = 0; y < TEXH; y++) {
		for (int x = 0; x < TEXW; x++) {
			float dx = ((float)x - 31.5f) / 32.0f;
			float dy = ((float)y - 31.5f) / 32.0f;
			float r2 = dx * dx + dy * dy;
			uint8_t a = (r2 < 0.75f) ? 255 : 0;          /* hard edge -> alpha test */
			uint8_t c = (((x / 8) ^ (y / 8)) & 1) ? 255 : 64; /* checker */
			g_tex[y * TEXW + x] = ((uint32_t)a << 24) | ((uint32_t)c << 16) |
			                      ((uint32_t)c << 8) | (uint32_t)c;
		}
	}
}

int main(void) {
	sceClibPrintf("[vitagl-ffp] start\n");

	/* vitaGL returns whether it had to fall back to a smaller resolution;
	 * GL_FALSE is the normal successful initialization path. */
	GLboolean resolution_fallback = vglInit(0x100000); /* 1 MiB legacy pool */
	sceClibPrintf("[vitagl-ffp] vglInit ok (resolution_fallback=%u)\n",
	              (unsigned)resolution_fallback);

	glClearColor(0.05f, 0.05f, 0.07f, 1.0f);

	/* ---- texture ---- */
	make_texture();
	GLuint tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, 4, TEXW, TEXH, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glEnable(GL_TEXTURE_2D);

	/* ---- alpha test (color key) ---- */
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.5f);

	/* ---- fog ---- */
	glEnable(GL_FOG);
	glFogi(GL_FOG_MODE, GL_LINEAR);
	{
		const GLfloat fog_color[4] = { 0.05f, 0.05f, 0.07f, 1.0f };
		glFogfv(GL_FOG_COLOR, fog_color);
	}
	glFogf(GL_FOG_START, 2.0f);
	glFogf(GL_FOG_END, 6.0f);

	/* ---- projection / depth ---- */
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(45.0, 960.0 / 544.0, 0.5, 100.0);
	glMatrixMode(GL_MODELVIEW);

	sceClibPrintf("[vitagl-ffp] entering loop\n");

	uint32_t old_buttons = 0;
	for (;;) {
		SceCtrlData pad;
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if ((pad.buttons & SCE_CTRL_START) && !(old_buttons & SCE_CTRL_START))
			break;
		old_buttons = pad.buttons;

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glLoadIdentity();
		glTranslatef(0.0f, -0.2f, -4.0f);

		/* One textured triangle; vertices at different Z so fog blends. */
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		glBegin(GL_TRIANGLES);
			glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, -0.5f);
			glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f, -1.0f,  0.5f);
			glTexCoord2f(0.5f, 1.0f); glVertex3f( 0.0f,  1.0f,  1.5f);
		glEnd();

		vglSwapBuffers(GL_FALSE);
	}

	sceClibPrintf("[vitagl-ffp] bye\n");
	sceKernelExitProcess(0);
	return 0;
}
